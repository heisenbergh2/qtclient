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

#include "paperdoll.h"
#include "client/animator.h"
#include "tibia/assets/thingtype.h"

#include <framework/core/clock.h>

PaperdollPtr Paperdoll::clone()
{
    auto obj = std::make_shared<Paperdoll>();
    *(obj.get()) = *this;

    return obj;
}

void Paperdoll::draw(const Point& dest, uint16_t animationPhase, bool mount, bool isOnTop, bool drawThings, const Color& color, LightSink* lightView) {
    (void)dest; (void)animationPhase; (void)mount; (void)isOnTop; (void)drawThings; (void)color; (void)lightView;
}


void Paperdoll::drawLight(const Point& dest, bool mount, LightSink* lightView) {
    (void)dest; (void)mount; (void)lightView;
}


int Paperdoll::getCurrentAnimationPhase()
{
    if (!m_thingType)
        return 0;

    const auto* animator = m_thingType->getIdleAnimator();
    if (!animator && m_thingType->isAnimateAlways())
        animator = m_thingType->getAnimator();

    if (animator)
        return animator->getPhaseAt(m_animationTimer, getSpeed());

    if (m_thingType->isCreature() && m_thingType->isAnimateAlways()) {
        const int animationPhases = m_thingType->getAnimationPhases();
        const float speed = getSpeed();
        if (animationPhases <= 0 || speed <= 0.f) return 0;

        const int ticksPerFrame = std::max<int>(1, static_cast<int>(std::round((1000.0 / animationPhases) / speed)));
        const long long animationPeriod = static_cast<long long>(ticksPerFrame) * animationPhases;
        if (animationPeriod <= 0) return 0;

        return static_cast<int>((g_clock.millis() % animationPeriod) / ticksPerFrame);
    }

    return 0;
}

void Paperdoll::setShader(const std::string_view name) { m_shader = std::string{ name }; }

void Paperdoll::reset() {
    m_onlyAddon = false;
    m_canDrawOnUI = true;

    m_addons = 0;
    m_sizeFactor = 1.f;
    for (auto& pattern : m_offsetDirections)
        pattern.fill(DirControl());
}

void Paperdoll::setOnTop(bool onTop) {
    for (auto& pattern : m_offsetDirections)
        for (auto& control : pattern)
            control.onTop = onTop;
}

void Paperdoll::setOffset(int16_t x, int16_t y) {
    for (auto& control : m_offsetDirections[0])
        control.offset = { x, y };
}

void Paperdoll::setOnTopByDir(Otc::Direction direction, bool onTop) {
    m_offsetDirections[0][direction].onTop = onTop;
}

void Paperdoll::setMountOffset(int16_t x, int16_t y) {
    for (auto& control : m_offsetDirections[1])
        control.offset = { x, y };
}

void Paperdoll::setMountOnTopByDir(Otc::Direction direction, bool onTop) {
    m_offsetDirections[1][direction].onTop = onTop;
}
