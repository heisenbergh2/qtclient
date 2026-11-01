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

#include "gameevents.h"

#include <algorithm>

GameEvents g_gameEvents;

std::string GameEvents::key(const std::string_view scope, const std::string_view field)
{
    std::string k;
    k.reserve(scope.size() + field.size() + 1);
    k.append(scope).append(".").append(field);
    return k;
}

int GameEvents::subscribe(const std::string_view scope, const std::string_view field, Handler handler)
{
    const int id = m_nextId++;
    m_handlers[key(scope, field)].push_back({ id, std::move(handler) });
    return id;
}

void GameEvents::unsubscribe(const int id)
{
    for (auto it = m_handlers.begin(); it != m_handlers.end();) {
        std::erase_if(it->second, [id](const Entry& e) { return e.id == id; });
        it = it->second.empty() ? m_handlers.erase(it) : std::next(it);
    }
}

bool GameEvents::wants(const std::string_view scope, const std::string_view field) const
{
    return !m_handlers.empty() && m_handlers.contains(key(scope, field));
}

std::any GameEvents::publish(const std::string_view scope, const std::string_view field, LuaObject* self, const Args& args)
{
    const auto it = m_handlers.find(key(scope, field));
    if (it == m_handlers.end())
        return {};
    std::any result;
    // Copy: a handler may subscribe or unsubscribe.
    const auto entries = it->second;
    for (const auto& entry : entries) {
        if (std::any r = entry.handler(self, args); r.has_value())
            result = std::move(r);
    }
    return result;
}

#include <framework/luaengine/luaobject.h>
#include <framework/stdext/demangle.h>

int16_t g_luaThreadId = -1;

std::string LuaObject::getClassName()
{
#ifdef _MSC_VER
    return stdext::demangle_name(typeid(*this).name()) + 6;
#else
    return stdext::demangle_name(typeid(*this).name());
#endif
}

int LuaObject::getUseCount()
{
    try {
        const auto self = shared_from_this();
        return static_cast<int>(self.use_count()) - 1;
    } catch (...) {
        return 0;
    }
}
