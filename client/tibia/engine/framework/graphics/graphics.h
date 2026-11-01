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

#pragma once

#include "declarations.h"

class IRenderBackend;

 // @bindsingleton g_graphics
class Graphics
{
public:
    Graphics();
    ~Graphics();

    // Resolves the configured graphics engine into the list to try. Runs before the window
    // exists, because the window must know whether to create a GL context at all.
    // @dontbind
    void selectEngine();
    // @dontbind
    void init();
    // @dontbind
    void terminate();

    void resize(const Size& size);

    int getMaxTextureSize() const { return m_maxTextureSize; }
    const Size& getViewportSize() { return m_viewportSize; }

    std::string getVendor() { return m_vendor; }
    std::string getRenderer() { return m_renderer; }
    std::string getVersion() { return m_version; }
    std::string getExtensions() { return m_extensions; }

    bool ok() const { return m_ok; }

    // The engine id actually running ("opengl", "metal", "d3d12", ...), after any fallback.
    std::string getActiveBackend() const { return m_activeEngine; }
    // Engine ids this build offers on this platform, "auto" excluded.
    std::vector<std::string> getAvailableBackends() const;
    // Display name for an engine id, e.g. "DirectX 12".
    std::string getBackendName(const std::string& engine) const;

    // True while OpenGL renders. Every raw GL call outside the GL backend is gated on this.
    bool isGL() const { return m_gl; }

    // @dontbind
    IRenderBackend* getBackend() const { return m_backend.get(); }

    // Called by a non-GL backend once its device exists.
    // @dontbind
    void setDeviceInfo(std::string vendor, std::string renderer, std::string version, int maxTextureSize);

private:
    bool initGL();
    bool initEngine(const std::string& engine);

    bool m_ok{ false };
    // tibia hosts use Qt QRhi; engine GL stays off until a real GL backend loads.
    bool m_gl{ false };

    std::string m_vendor;
    std::string m_renderer;
    std::string m_version;
    std::string m_extensions;

    std::string m_activeEngine;
    std::vector<std::string> m_candidates;
    std::unique_ptr<IRenderBackend> m_backend;

    int m_maxTextureSize{ -1 };
    int m_alphaBits{ 0 };

    Size m_viewportSize;
};

extern Graphics g_graphics;
