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

#include "missile.h"

#include "animation/animator.h"
#include "client/client.h"
#include "client/gameconfig.h"
#include "map.h"
#include "tibia/assets/thingtype.h"
#include "tibia/assets/things.h"
#include "framework/core/eventdispatcher.h"

void Missile::draw(const Point& dest, const bool drawThings, LightSink* lightView)
{
    (void)dest; (void)drawThings; (void)lightView;
}


Point Missile::getFlightOffset() const
{
    const tibia::animation::TWorldMapLinearMovementTween tween(m_delta, m_duration);
    return tween.offsetAt(static_cast<float>(m_animationTimer.ticksElapsed()));
}

void Missile::setPath(const Position& fromPosition, const Position& toPosition)
{
    m_position = fromPosition;
    m_delta = Point(toPosition.x - fromPosition.x, toPosition.y - fromPosition.y);

    const float deltaLength = m_delta.length();
    if (deltaLength == 0) {
        tibia::schedule::post([self = asMissile()] {
            g_map.removeThing(self);
        });
        return;
    }

    setDirection(fromPosition.getDirectionFromPosition(toPosition));

    m_duration = (g_gameConfig.getMissileTicksPerFrame() * 2) * std::sqrt(deltaLength);
    m_delta *= g_gameConfig.getSpriteSize();
    m_animationTimer.restart();
    m_distance = fromPosition.distance(toPosition);

    // schedule removal
    tibia::schedule::after([self = asMissile()] { g_map.removeThing(self); }, m_duration);
}

void Missile::setDirection(const Otc::Direction dir) {
    m_direction = dir;

    if (m_direction == Otc::NorthWest) {
        m_numPatternX = 0;
        m_numPatternY = 0;
    } else if (m_direction == Otc::North) {
        m_numPatternX = 1;
        m_numPatternY = 0;
    } else if (m_direction == Otc::NorthEast) {
        m_numPatternX = 2;
        m_numPatternY = 0;
    } else if (m_direction == Otc::East) {
        m_numPatternX = 2;
        m_numPatternY = 1;
    } else if (m_direction == Otc::SouthEast) {
        m_numPatternX = 2;
        m_numPatternY = 2;
    } else if (m_direction == Otc::South) {
        m_numPatternX = 1;
        m_numPatternY = 2;
    } else if (m_direction == Otc::SouthWest) {
        m_numPatternX = 0;
        m_numPatternY = 2;
    } else if (m_direction == Otc::West) {
        m_numPatternX = 0;
        m_numPatternY = 1;
    } else {
        m_numPatternX = 1;
        m_numPatternY = 1;
    }
}

void Missile::setId(uint32_t id)
{
    if (!g_things.isValidDatId(id, ThingCategoryMissile))
        id = 0;

    m_clientId = id;
}

ThingType* Missile::getThingType() const {
    return g_things.getRawThingType(m_clientId, ThingCategoryMissile);
}