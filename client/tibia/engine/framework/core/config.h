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
#include <framework/luaengine/luaobject.h>

#include <string>
#include <unordered_map>
#include <vector>

 // @bindclass
class Config final : public LuaObject
{
public:
    Config();

    bool load(const std::string& file);
    bool unload();
    bool save();
    void clear();
    void setValue(const std::string& key, const std::string& value);
    void setList(const std::string& key, const std::vector<std::string>& list);
    std::string getValue(const std::string& key);
    std::vector<std::string> getList(const std::string& key);

    bool exists(const std::string& key);
    void remove(const std::string& key);

    std::string getFileName();
    bool isLoaded() const;

    // @dontbind
    ConfigPtr asConfig() { return static_self_cast<Config>(); }

private:
    std::string m_fileName;
    std::unordered_map<std::string, std::string> m_values;
    std::unordered_map<std::string, std::vector<std::string>> m_lists;
};
