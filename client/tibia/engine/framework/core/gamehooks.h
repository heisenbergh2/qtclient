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

// Definitions of GameEvents::callGlobalField, kept out of gameevents.h because
// they need the Lua interface, which itself depends on luaobject.h.

#include "gameevents.h"

#include <framework/core/logger.h>

#include <mutex>
#include <unordered_set>

namespace tibia::gameevents_detail {

inline void warnOrphanOnce(const std::string_view global, const std::string_view field)
{
    if (global != "g_game")
        return;
    static std::mutex mu;
    static std::unordered_set<std::string> warned;
    const std::string key{ field };
    std::lock_guard lock(mu);
    if (!warned.insert(key).second)
        return;
    g_logger.warning("[g_game] orphan publish '{}' (no subscribers this session)", key);
}

} // namespace tibia::gameevents_detail

template<typename... T>
void GameEvents::callGlobalField(const std::string_view global, const std::string_view field, const T&... args)
{
    if (wants(global, field))
        publish(global, field, nullptr, Args{ std::any(args)... });
    else
        tibia::gameevents_detail::warnOrphanOnce(global, field);
}

template<typename R, typename... T>
R GameEvents::callGlobalField(const std::string_view global, const std::string_view field, const T&... args)
{
    R result{};
    if (wants(global, field)) {
        const std::any out = publish(global, field, nullptr, Args{ std::any(args)... });
        if (const R* value = std::any_cast<R>(&out))
            result = *value;
    } else {
        tibia::gameevents_detail::warnOrphanOnce(global, field);
    }
    return result;
}
