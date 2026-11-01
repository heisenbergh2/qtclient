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

#include "effect.h"
#include "tibia/runtime/host.h"

#include "animation/animator.h"
#include "client/animator.h"
#include "client/client.h"
#include "game.h"
#include "client/gameconfig.h"
#include "map.h"
#include "tibia/assets/thingtype.h"
#include "tibia/assets/things.h"
#include "framework/core/eventdispatcher.h"

bool Effect::getDrawFrame(int& animationPhase, int& xPattern, int& yPattern)
{
    if (!canDraw() || isHided())
        return false;

    // It only starts to draw when the first effect as it is about to end.
    if (m_animationTimer.ticksElapsed() < m_timeToStartDrawing)
        return false;

    animationPhase = 0;
    if (canAnimate()) {
        if (g_game.getFeature(Otc::GameEnhancedAnimations)) {
            const tibia::animation::TAnimator cipAnimator(getThingType()->getIdleAnimator());
            if (!cipAnimator.animator())
                return false;

            // This requires a separate getPhaseAt method as using getPhase would make all magic effects use the same phase regardless of their appearance time
            animationPhase = cipAnimator.phaseAt(m_animationTimer);
        } else {
            // hack to fix some animation phases duration, currently there is no better solution
            int ticks = g_gameConfig.getEffectTicksPerFrame();
            if (m_clientId == 33) {
                ticks <<= 2;
            }

            animationPhase = std::min<int>(static_cast<int>(m_animationTimer.ticksElapsed() / ticks), getAnimationPhases() - 1);
        }
    }

    const int offsetX = m_position.x - g_map.getCentralPosition().x;
    const int offsetY = m_position.y - g_map.getCentralPosition().y;

    xPattern = static_cast<unsigned>(offsetX) % getNumPatternX();
    xPattern = 1 - xPattern - getNumPatternX();
    if (xPattern < 0) xPattern += getNumPatternX();

    yPattern = static_cast<unsigned>(offsetY) % getNumPatternY();

    if (g_game.getFeature(Otc::GameMapOldEffectRendering)) {
        xPattern = offsetX % getNumPatternX();
        if (xPattern < 0)
            xPattern += getNumPatternX();

        yPattern = offsetY % getNumPatternY();
        if (yPattern < 0)
            yPattern += getNumPatternY();
    }

    const auto* thingType = getThingType();
    return thingType && !thingType->isNull() && thingType->getAnimationPhases() > 0;
}

void Effect::draw(const Point& dest, const bool drawThings, LightSink* lightView)
{
    (void)dest; (void)drawThings; (void)lightView;
}


void Effect::onAppear()
{
    if (g_game.getFeature(Otc::GameEnhancedAnimations)) {
        const auto* animator = getThingType()->getIdleAnimator();
        if (!animator)
            return;

        m_duration = animator->getTotalDuration();
        if (animator->isInfiniteLoop()) {
            m_permanent = true;
        }
    } else {
        m_duration = g_gameConfig.getEffectTicksPerFrame();

        // hack to fix some animation phases duration, currently there is no better solution
        if (m_clientId == 33) {
            m_duration <<= 2;
        }

        m_duration *= getAnimationPhases();
    }

    m_animationTimer.restart();

    // Permanent loop effects stay until the server removes them; on a frozen death
    // scene they still play once and expire so they do not loop forever.
    if (!m_permanent || g_game.isSceneFrozen()) {
        tibia::schedule::after([self = asEffect()] { g_map.removeThing(self); }, m_duration);
    }
}

bool Effect::waitFor(const EffectPtr& effect)
{
    const ticks_t ticksElapsed = effect->m_animationTimer.ticksElapsed();
    uint16_t minDuration = getIdleAnimator() ? getIdleAnimator()->getMinDuration() : g_gameConfig.getEffectTicksPerFrame();
    minDuration = minDuration * std::max<uint8_t>(getAnimationPhases() / 3, 1);

    if (ticksElapsed <= minDuration)
        return false;

    const int duration = effect->m_duration / (tibia::host::app().mustOptimize() || tibia::host::app().isForcedEffectOptimization() ? 1.5 : 3);
    m_timeToStartDrawing = std::max<int>(0, duration - effect->m_animationTimer.ticksElapsed());

    return true;
}

void Effect::setId(const uint32_t id)
{
    if (!g_things.isValidDatId(id, ThingCategoryEffect))
        return;

    m_clientId = id;
}

void Effect::setPosition(const Position& position, const uint8_t stackPos)
{
    if (m_clientId == 0)
        return;

    Thing::setPosition(position, stackPos);
    int pattern_x = getNumPatternX();
    int pattern_y = getNumPatternY();
    if (pattern_x == 0 || pattern_y == 0) {
        g_logger.warning("Failed to send magic effect {}. No sprites declared.", m_clientId);
        return;
    }

    m_numPatternX = m_position.x % pattern_x;
    m_numPatternY = m_position.y % pattern_y;
}

ThingType* Effect::getThingType() const {
    return g_things.getRawThingType(m_clientId, ThingCategoryEffect);
}