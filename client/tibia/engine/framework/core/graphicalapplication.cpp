/*
 * Copyright (c) 2024-2026 qtclient contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "graphicalapplication.h"
#include "tibia/runtime/host.h"

#include <framework/core/gamehooks.h>

#include "asyncdispatcher.h"
#include "clock.h"
#include "eventdispatcher.h"
#include "framework/graphics/declarations.h"
#include "tibia/world/game.h"
#include "tibia/assets/things.h"
#include "client/client.h"
#include "framework/graphics/graphics.h"
#include "framework/graphics/render/irenderbackend.h"
#include "framework/graphics/image.h"
#include "framework/graphics/texture.h"
#include "framework/graphics/texturemanager.h"
#include "framework/input/mouse.h"
#include <framework/util/stats.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <algorithm>
#if QTCLIENT_NATIVE_WINDOW
#include <framework/platform/browserwindow.h>
#endif
#endif
#include <framework/platform/platformwindow.h>

GraphicalApplication g_app;

void GraphicalApplication::init(std::vector<std::string>& args, ApplicationContext* context)
{
    Application::init(args, context);

    auto graphicalContext = static_cast<GraphicalApplicationContext*>(context);
    setDrawEvents(graphicalContext->getDrawEvents());

    // the graphics engine decides whether the window gets a GL context or a native surface
    g_graphics.selectEngine();

    // setup platform window
    g_window.init();
    g_window.hide();

    // set the window title color
    g_window.setTitleBarColor(Color::black);

    g_window.setOnResize([this](auto&& PH1) {
        if (!m_running)
            resize(PH1);
        else {
            // Copy Size: window state can change before the queued callback runs.
            const Size size = PH1;
            tibia::schedule::post([this, size] { resize(size); });
        }
    });

    g_window.setOnInputEvent([this](auto&& PH1) {
        if (!m_running)
            inputEvent(PH1);
        else {
            // Copy InputEvent: m_inputEvent is overwritten by later motions/presses.
            const InputEvent event = PH1;
            tibia::schedule::post([this, event] { inputEvent(event); });
        }
    });

    g_window.setOnClose([this] { tibia::schedule::post([this] { close(); }); });

    g_mouse.init();


    // initialize graphics
    g_graphics.init();

    // fire first resize event
    resize(g_window.getSize());

    // Paced whatever the frame limit: unpaced, the map thread spins polling for work. The
    // counter keeps 1 ms of slack per period, so 500 lets it run about 1000 times a second;
    // the map itself is only redrawn when something changes.
    m_mapProcessFrameCounter.setTargetFps(500u);
    m_mapProcessFrameCounter.init();
    m_graphicFrameCounter.init();
}

void GraphicalApplication::deinit()
{
    // hide the window because there is no render anymore
    g_window.hide();

    Application::deinit();
}

void GraphicalApplication::terminate()
{
    Application::terminate();
    m_terminated = false;

    g_mouse.terminate();

    g_graphics.terminate();
    g_window.terminate();

    m_terminated = true;
}

#ifdef __EMSCRIPTEN__
void GraphicalApplication::mainLoop() {
    if (m_stopping) {
        emscripten_cancel_main_loop();
        MAIN_THREAD_EM_ASM({ window.location.reload(); });
        return;
    }
    mainPoll();

    if (!g_window.isVisible()) {
        stdext::millisleep(10);
        return;
    }

    if (m_graphicFrameCounter.update()) {
        tibia::schedule::post([this, fps = m_graphicFrameCounter.getFps()] {
            g_gameEvents.callGlobalField("g_app", "onFps", fps);
        });
    }
}
#endif

bool GraphicalApplication::canDrawMap() const {
    return m_drawEvents && m_drawEvents->canDraw(DrawPoolType::MAP);
}

void GraphicalApplication::run()
{
    // run the first poll
    mainPoll();
    poll();

    // show window
    g_window.show();

    // run the second poll
    mainPoll();
    poll();

    g_gameEvents.callGlobalField("g_app", "onRun");

    // THREAD - POOL & MAP
    const auto& mapThread = g_asyncDispatcher->submit_task([this] {
        BS::multi_future<void> tasks;

        g_luaThreadId = g_eventThreadId = stdext::getThreadId();
        while (!m_stopping) {
            AutoStat frame(STATS_RENDER, "MapThreadFrame");
            poll();

            if (!g_window.isVisible()) {
                stdext::millisleep(10);
                continue;
            }

            if (g_game.isOnline()) {
                AutoStat s(STATS_RENDER, "DrawPreload");
                m_drawEvents->preLoad();
            }

            bool canDrawForeground = m_drawEvents->canDraw(DrawPoolType::FOREGROUND);

            if (!g_game.isOnline() && canDrawForeground) {
            } else if (canDrawMap()) {

                static constexpr std::array<DrawPoolType, 2> types{ DrawPoolType::LIGHT, DrawPoolType::FOREGROUND_MAP };
                for (const auto type : types) {
                    if (m_drawEvents->canDraw(type)) {
                        tasks.emplace_back(g_asyncDispatcher->submit_task([this, type] {
                            AutoStat s(STATS_RENDER, type == DrawPoolType::LIGHT ? "DrawLight" : "DrawForegroundMap");
                            m_drawEvents->draw(type);
                        }));
                    }
                }

                {
                    AutoStat s(STATS_RENDER, "DrawMap");
                    m_drawEvents->draw(DrawPoolType::MAP);
                }

                {
                    AutoStat s(STATS_RENDER, "WaitDrawTasks");
                    tasks.wait();
                }
                tasks.clear();
            }

            m_mapProcessFrameCounter.update();
        }
    });

#ifdef __EMSCRIPTEN__
    m_running = true;
    emscripten_set_main_loop(([] { tibia::host::app().mainLoop(); }), 0, 1);
#else
    m_running = true;
    while (!m_stopping) {
        mainPoll();

        if (!g_window.isVisible()) {
            stdext::millisleep(10);
            continue;
        }

        // update screen pixels (a non-GL backend presents inside its render)
        if (g_graphics.isGL()) {
            AutoStat s(STATS_RENDER, "SwapBuffers");
            g_window.swapBuffers();
        }

        if (m_graphicFrameCounter.update()) {
            tibia::schedule::post([this, fps = m_graphicFrameCounter.getFps()] {
                g_gameEvents.callGlobalField("g_app", "onFps", fps);
            });
        }
    }
#endif
    mapThread.wait();

    m_running = false;
    m_stopping = false;

    g_luaThreadId = g_eventThreadId = -1;
}

void GraphicalApplication::poll()
{

    Application::poll();

    if (!g_window.isVisible()) {
        g_textDispatcher.poll();
    }
}
void GraphicalApplication::mainPoll()
{
    AutoStat s(STATS_MAIN, "MainPoll");
    {
        AutoStat s2(STATS_MAIN, "ClockUpdate");
        g_clock.update();
    }
    {
        AutoStat s2(STATS_MAIN, "DispatcherPoll");
        g_mainDispatcher.poll();
    }
    {
        AutoStat s2(STATS_MAIN, "TextureDeletion");
        Texture::flushDeletedTextures();
    }
    {
        AutoStat s2(STATS_MAIN, "WindowPoll");
        g_window.poll();
    }
    {
        AutoStat s2(STATS_MAIN, "TexturePoll");
        g_textures.poll();
    }
}

void GraphicalApplication::close()
{
    m_onInputEvent = true;
    Application::close();
    m_onInputEvent = false;
}

static constexpr bool USE_FRAMEBUFFER = false;
void GraphicalApplication::resize(const Size& size)
{
    g_graphics.resize(size);
}

void GraphicalApplication::inputEvent(const InputEvent& event)
{
    (void)event;
}

bool GraphicalApplication::isLoadingAsyncTexture() { return m_loadingAsyncTexture || (m_drawEvents && m_drawEvents->isLoadingAsyncTexture()); }
bool GraphicalApplication::isScaled()
{
    return g_window.getDisplayDensity() != 1.f;
}
void GraphicalApplication::setLoadingAsyncTexture(bool v) {
    if (m_drawEvents && m_drawEvents->isUsingProtobuf())
        v = true;
    else if (isEncrypted())
        v = false;

    m_loadingAsyncTexture = v;

    if (m_drawEvents)
        m_drawEvents->onLoadingAsyncTextureChanged(v);
}

void GraphicalApplication::setHdMode(const bool enabled)
{
    if (m_hdMode == enabled)
        return;

    m_hdMode = enabled;
    // Rebuild thing plates under the new scale so draws pick up xBR4 (or native) textures.
    g_things.unloadTextures();
}

void GraphicalApplication::doScreenshot(std::string file)
{
    if (file.empty()) {
        file = "screenshot.png";
    }

    tibia::schedule::post([file] {
        if (!g_graphics.isGL()) {
            g_graphics.getBackend()->readTexture(0, [file](const ImagePtr& image) {
                if (!image) {
                    g_logger.error("Can't do screenshot: the graphics engine cannot read back the screen");
                    return;
                }
                g_asyncDispatcher->detach_task([image, file] {
                    try {
                        image->setOpacity(255);
                        image->savePNG(file);
                    } catch (stdext::exception& e) {
                        g_logger.error(std::string("Can't do screenshot: ") + e.what());
                    }
                });
            });
            return;
        }

        auto resolution = g_graphics.getViewportSize();
        const int width = resolution.width();
        const int height = resolution.height();
        auto pixels = std::make_shared<std::vector<uint8_t>>(width * height * 4 * sizeof(GLubyte), 0);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels->data());

        g_asyncDispatcher->detach_task([resolution, pixels, file] {
            try {
                Image image(resolution, 4, pixels->data());
                image.flipVertically();
                image.setOpacity(255);
                image.savePNG(file);
            } catch (stdext::exception& e) {
                g_logger.error(std::string("Can't do screenshot: ") + e.what());
            }
        });
    });
}

void GraphicalApplication::doMapScreenshot(std::string fileName)
{
    if (m_drawEvents) m_drawEvents->doMapScreenshot(fileName);
}

float GraphicalApplication::getHUDScale() const
{
    return g_window.getDisplayDensity();
}

void GraphicalApplication::setHUDScale(const float v)
{
    // Upper bound covers 1.5x HUD on 3x+ device pixel ratios.
    const float scale = std::clamp(v > 0.f ? v : 1.f, 0.35f, 8.f);

#if defined(__EMSCRIPTEN__) && QTCLIENT_NATIVE_WINDOW
    // Same as desktop: density owns HUD scale; canvas stays 1:1 with the play box.
    g_window.setDisplayDensity(scale);

    int lw = 0;
    int lh = 0;
    MAIN_THREAD_EM_ASM({
        if (typeof window.otcApplyUiScale === "function") {
            window.otcApplyUiScale($0);
        }
        setValue($1, (window.QTCLIENT_UI_LAYOUT_W | 0), "i32");
        setValue($2, (window.QTCLIENT_UI_LAYOUT_H | 0), "i32");
    }, scale, &lw, &lh);

    g_browserWindow.refreshPlayBoxFromShell();

    // Always resize: play-box size often unchanged while density changes, so a
    // canvas sync alone would skip onResize and leave UI/FBO at the old density.
    Size size = (lw > 1 && lh > 1) ? Size(lw, lh) : g_window.getSize();
    if (!size.isValid() || size.isEmpty())
        size = g_graphics.getViewportSize();
    if (lw > 1 && lh > 1)
        g_browserWindow.requestCanvasSync(lw, lh);
    resize(size);
#else
    g_window.setDisplayDensity(scale);
    Size size = g_window.getSize();
    if (!size.isValid() || size.isEmpty())
        size = g_graphics.getViewportSize();
    resize(size);
#endif
}

#if defined(__EMSCRIPTEN__) && QTCLIENT_NATIVE_WINDOW
void GraphicalApplication::requestShellLayout()
{
    g_browserWindow.applyShellLayout();
}

int GraphicalApplication::getPlayBoxWidth() const
{
    return g_browserWindow.getPlayBoxWidth();
}

int GraphicalApplication::getPlayBoxHeight() const
{
    return g_browserWindow.getPlayBoxHeight();
}
#endif
