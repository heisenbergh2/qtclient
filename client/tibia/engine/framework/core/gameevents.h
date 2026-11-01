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

#include <any>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>


class LuaObject;

/// Engine hooks for frontends that are not Lua (the Qt app).
/// Every engine-to-script hook is published here by scope and field: global
/// hooks use the Lua global ("g_game", "onGameStart"); object hooks
/// (LuaObject::callLuaField) use scope ObjectScope and pass the object, which
/// handlers cast (Lua resolves them through the class chain the same way).
/// Publishes engine hooks for the tibia (non-Lua) frontend.
class GameEvents
{
public:
    using Args = std::vector<std::any>;
    /// Returns an empty std::any unless it handles a hook that expects a result.
    using Handler = std::function<std::any(LuaObject* self, const Args& args)>;
    static constexpr std::string_view ObjectScope = "object";

    int subscribe(std::string_view scope, std::string_view field, Handler handler);
    void unsubscribe(int id);

    bool wants(std::string_view scope, std::string_view field) const;
    /// Calls every handler; returns the last non-empty result.
    std::any publish(std::string_view scope, std::string_view field, LuaObject* self, const Args& args);

    template<typename... T>
    void callGlobalField(std::string_view global, std::string_view field, const T&... args);

    template<typename R, typename... T>
    R callGlobalField(std::string_view global, std::string_view field, const T&... args);

    template<typename... T>
    std::any publishObject(LuaObject* self, std::string_view field, const T&... args)
    {
        if (!wants(ObjectScope, field))
            return {};
        return publish(ObjectScope, field, self, Args{ std::any(args)... });
    }

private:
    struct Entry
    {
        int id;
        Handler handler;
    };
    static std::string key(std::string_view scope, std::string_view field);

    std::unordered_map<std::string, std::vector<Entry>> m_handlers;
    int m_nextId = 1;
};

extern GameEvents g_gameEvents;
