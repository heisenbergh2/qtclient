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

#include "client.h"

#include "tibia/world/game.h"
#include "gameconfig.h"
#include "tibia/world/map.h"
#include "minimap.h"
#include "tibia/assets/things.h"
#include "satellitemap.h"
#include "framework/core/eventdispatcher.h"
#ifdef FRAMEWORK_EDITOR
#include "creatures.h"
#endif
#include "tibia/world/paperdollmanager.h"

Client g_client;

Client::Client()
{
    m_effectAlphas.fill(1.0f);
}

void Client::init(std::vector<std::string>& /*args*/)
{

    g_gameConfig.init();
    g_map.init();
    g_minimap.init();
    g_satelliteMap.init();
    g_game.init();
    g_things.init();
}

void Client::terminate()
{
#ifdef FRAMEWORK_EDITOR
    g_creatures.terminate();
#endif
    g_game.terminate();
    g_map.terminate();
    g_minimap.terminate();
    g_satelliteMap.terminate();
    g_things.terminate();
    g_paperdolls.clear();
    g_gameConfig.terminate();
}


