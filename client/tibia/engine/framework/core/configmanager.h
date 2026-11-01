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

#include "config.h"
#include <string>

struct GraphicsConfig
{
    uint16_t maxAtlasSize = 8192;
    int16_t  mapAtlasSize = 0;
    int16_t foregroundAtlasSize = 2048;

    // Graphics engine requested at startup: auto | d3d11 | d3d12 | vulkan | metal | opengl |
    // webgpu | webgl. Read once, before the window exists; changing it needs a restart.
    std::string renderBackend = "auto";
};

struct FontConfig
{
    std::string widget;
    std::string staticText;
    std::string animatedText;
    std::string creatureText;
    std::string itemCount;
};

struct DebugConfig
{
    // Periodic [mem]/[gc] diagnostics. Off by default; set [debug] memoryLog=1 in config.ini.
    bool memoryLog = false;
};

struct PublicConfig
{
    GraphicsConfig graphics;
    FontConfig font;
    DebugConfig debug;
};

// @bindsingleton g_configs
class ConfigManager
{
public:
    void init();
    void terminate();

    ConfigPtr getSettings();
    ConfigPtr get(const std::string& file);

    ConfigPtr create(const std::string& file);
    ConfigPtr loadSettings(const std::string& file);
    ConfigPtr load(const std::string& file);

    bool unload(const std::string& file);
    void remove(const ConfigPtr& config);

    void saveSettings();

    const PublicConfig& getPublicConfig() const { return m_publicConfig; }
    void loadPublicConfig(const std::string& file);

    // The saved graphics engine choice (what the next start will request). The engine the
    // current session runs on is g_graphics.getActiveBackend().
    const std::string& getRenderBackend() const { return m_renderBackendChoice; }
    bool setRenderBackend(const std::string& backend);

    // What this session requests: the saved choice, unless the process was started with
    // --graphics-engine=<id> (used to restart on OpenGL after a device loss without
    // overwriting the saved choice).
    // @dontbind
    const std::string& getStartupRenderBackend() const { return m_renderBackendStartup; }

    static bool isKnownRenderBackend(std::string_view backend);

protected:
    ConfigPtr m_settings;

private:
    void loadRenderBackendOverride();

    std::list<ConfigPtr> m_configs;
    PublicConfig m_publicConfig;

    // Per-user graphics.ini in the user write dir. Resolved once at startup so the file read
    // before init.lua and the file written later are always the same one.
    std::string m_renderBackendFile;
    std::string m_renderBackendChoice{ "auto" };
    std::string m_renderBackendStartup{ "auto" };
};

extern ConfigManager g_configs;
