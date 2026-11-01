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

#include "configmanager.h"
#include "tibia/runtime/host.h"
#include "resourcemanager.h"

#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

ConfigManager g_configs;

namespace
{
    std::string trimIni(std::string text)
    {
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
            text.erase(text.begin());
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
            text.pop_back();
        return text;
    }

    class IniFile
    {
    public:
        explicit IniFile(std::string_view text)
        {
            std::string section;
            std::istringstream input{ std::string{ text } };
            std::string line;
            while (std::getline(input, line)) {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                line = trimIni(line);
                if (line.empty() || line.front() == ';' || line.front() == '#')
                    continue;
                if (line.front() == '[') {
                    const auto end = line.find(']');
                    section = end == std::string::npos ? std::string{} : line.substr(1, end - 1);
                    continue;
                }
                const auto eq = line.find('=');
                if (eq == std::string::npos)
                    continue;
                m_values[section + "." + trimIni(line.substr(0, eq))] = trimIni(line.substr(eq + 1));
            }
        }

        int parseError() const { return m_values.empty() && false ? -1 : 0; }

        std::string get(const std::string& section, const std::string& key, const std::string& fallback) const
        {
            const auto it = m_values.find(section + "." + key);
            return it == m_values.end() ? fallback : it->second;
        }

        int getInteger(const std::string& section, const std::string& key, int fallback) const
        {
            const auto it = m_values.find(section + "." + key);
            if (it == m_values.end())
                return fallback;
            try {
                return std::stoi(it->second);
            } catch (...) {
                return fallback;
            }
        }

        bool getBoolean(const std::string& section, const std::string& key, bool fallback) const
        {
            const auto value = get(section, key, {});
            if (value.empty())
                return fallback;
            return value == "1" || value == "true" || value == "yes" || value == "on";
        }

    private:
        std::unordered_map<std::string, std::string> m_values;
    };

    constexpr std::array<std::string_view, 8> RENDER_BACKENDS{
        "auto", "d3d11", "d3d12", "vulkan", "metal", "opengl", "webgpu", "webgl"
    };
}

void ConfigManager::init() {
    m_settings = std::make_shared<Config>();

    // Workdir is mounted at PHYSFS root (gamecore discoverWorkDir); use an
    // absolute virtual path so resolvePath does not warn under QTCLIENT_LUA=0.
    loadPublicConfig("/config.ini");
    loadRenderBackendOverride();
}

bool ConfigManager::isKnownRenderBackend(const std::string_view backend)
{
    return std::ranges::find(RENDER_BACKENDS, backend) != RENDER_BACKENDS.end();
}

void ConfigManager::loadRenderBackendOverride()
{
    if (!isKnownRenderBackend(m_publicConfig.graphics.renderBackend)) {
        g_logger.warning("[render] unknown renderBackend '{}' in config.ini - using 'auto'",
                         m_publicConfig.graphics.renderBackend);
        m_publicConfig.graphics.renderBackend = "auto";
    }

    const auto writeDir = tibia::files::data().resolveUserWriteDir(tibia::host::app().getCompactName() + "/");
    if (!writeDir.empty())
        m_renderBackendFile = writeDir + "graphics.ini";

    std::ifstream in(m_renderBackendFile);
    if (in) {
        std::stringstream content;
        content << in.rdbuf();
        const auto text = content.str();
        IniFile reader(text);
        if (reader.parseError() >= 0) {
            const auto backend = reader.get("graphics", "renderBackend", "");
            if (isKnownRenderBackend(backend))
                m_publicConfig.graphics.renderBackend = backend;
            else if (!backend.empty())
                g_logger.warning("[render] ignoring unknown renderBackend '{}' in {}", backend, m_renderBackendFile);
        }
    }

    m_renderBackendChoice = m_publicConfig.graphics.renderBackend;
    m_renderBackendStartup = m_renderBackendChoice;

    static constexpr std::string_view SESSION_FLAG = "--graphics-engine=";
    const auto options = tibia::host::app().getStartupOptions();
    if (const auto pos = options.find(SESSION_FLAG); pos != std::string::npos) {
        const auto start = pos + SESSION_FLAG.size();
        const auto engine = options.substr(start, options.find(' ', start) - start);
        if (isKnownRenderBackend(engine))
            m_renderBackendStartup = engine;
    }
}

bool ConfigManager::setRenderBackend(const std::string& backend)
{
    if (!isKnownRenderBackend(backend)) {
        g_logger.error("[render] unknown graphics engine '{}'", backend);
        return false;
    }

    if (m_renderBackendFile.empty()) {
        g_logger.error("[render] no user directory to save the graphics engine choice in");
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(m_renderBackendFile).parent_path(), ec);

    std::ofstream out(m_renderBackendFile, std::ios::trunc);
    if (!out) {
        g_logger.error("[render] unable to write {}", m_renderBackendFile);
        return false;
    }

    out << "; Graphics engine chosen in Options > Graphics. Read at startup.\n"
        << "[graphics]\n"
        << "renderBackend = " << backend << "\n";

    m_renderBackendChoice = backend;
    return true;
}

void ConfigManager::terminate()
{
    if (m_settings) {
        // ensure settings are saved
        m_settings->save();

        m_settings->unload();
        m_settings = nullptr;
    }

    for (auto config : m_configs) {
        config->unload();
        config = nullptr;
    }

    m_configs.clear();
}

ConfigPtr ConfigManager::getSettings()
{
    return m_settings;
}

ConfigPtr ConfigManager::get(const std::string& file)
{
    for (const auto& config : m_configs) {
        if (config->getFileName() == file) {
            return config;
        }
    }
    return nullptr;
}

ConfigPtr ConfigManager::loadSettings(const std::string& file)
{
    if (file.empty()) {
        g_logger.error("Must provide a configuration file to load.");
    } else {
        if (m_settings->load(file)) {
            return m_settings;
        }
    }
    return nullptr;
}

ConfigPtr ConfigManager::create(const std::string& file)
{
    auto config = load(file);
    if (!config) {
        config = std::make_shared<Config>();

        config->load(file);
        config->save();

        m_configs.emplace_back(config);
    }
    return config;
}

ConfigPtr ConfigManager::load(const std::string& file)
{
    if (file.empty()) {
        g_logger.error("Must provide a configuration file to load.");
        return nullptr;
    }
    auto config = get(file);
    if (!config) {
        config = std::make_shared<Config>();

        if (config->load(file)) {
            m_configs.emplace_back(config);
        } else {
            // cannot load config
            config = nullptr;
        }
    }
    return config;
}

bool ConfigManager::unload(const std::string& file)
{
    if (auto config = get(file)) {
        config->unload();
        remove(config);
        config = nullptr;
        return true;
    }
    return false;
}

void ConfigManager::remove(const ConfigPtr& config) { m_configs.remove(config); }

void ConfigManager::saveSettings()
{
    if (m_settings)
        m_settings->save();
}

void ConfigManager::loadPublicConfig(const std::string& fileName) {
    try {
        auto content = tibia::files::data().readFileContents(fileName);
        IniFile reader(content);

        m_publicConfig.graphics.maxAtlasSize = std::max<int>(2048, reader.getInteger("graphics", "maxAtlasSize", m_publicConfig.graphics.maxAtlasSize));
        m_publicConfig.graphics.mapAtlasSize = reader.getInteger("graphics", "mapAtlasSize", m_publicConfig.graphics.mapAtlasSize);
        m_publicConfig.graphics.foregroundAtlasSize = reader.getInteger("graphics", "foregroundAtlasSize", m_publicConfig.graphics.foregroundAtlasSize);
        m_publicConfig.graphics.renderBackend = reader.get("graphics", "renderBackend", m_publicConfig.graphics.renderBackend);

        m_publicConfig.font.widget = reader.get("font", "widget", m_publicConfig.font.widget);
        m_publicConfig.font.staticText = reader.get("font", "static-text", m_publicConfig.font.staticText);
        m_publicConfig.font.animatedText = reader.get("font", "animated-text", m_publicConfig.font.animatedText);
        m_publicConfig.font.creatureText = reader.get("font", "creature-text", m_publicConfig.font.creatureText);
        m_publicConfig.font.itemCount = reader.get("font", "item-count", m_publicConfig.font.itemCount);
        m_publicConfig.debug.memoryLog = reader.getBoolean("debug", "memoryLog", m_publicConfig.debug.memoryLog);
    } catch (const std::exception& e) {
        g_logger.error("Failed to parse public config '{}': {}", fileName, e.what());
    }
}