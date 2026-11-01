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

#include "attachedeffect.h"
#include <framework/graphics/drawstub.h>

#include "client/animator.h"
#include "client/gameconfig.h"
#include "tibia/assets/thingtype.h"
#include "tibia/assets/things.h"
#include "framework/core/clock.h"
#include "framework/graphics/animatedtexture.h"
#include "framework/graphics/texture.h"
#include "framework/graphics/texturemanager.h"

AttachedEffectPtr AttachedEffect::create(const uint16_t thingId, const ThingCategory category) {
    if (!g_things.isValidDatId(thingId, category)) {
        g_logger.error("AttachedEffectManager::getInstance({}, {}): invalid thing with id or category.", thingId, static_cast<uint8_t>(category));
        return nullptr;
    }

    const auto& obj = std::make_shared<AttachedEffect>();
    obj->m_thingId = thingId;
    obj->m_thingCategory = category;
    return obj;
}

AttachedEffectPtr AttachedEffect::clone()
{
    auto obj = std::make_shared<AttachedEffect>();
    *(obj.get()) = *this;

    obj->m_frame = 0;
    obj->m_animationTimer.restart();
    obj->m_bounce.timer.restart();
    obj->m_pulse.timer.restart();
    obj->m_fade.timer.restart();

    if (!obj->m_texturePath.empty()) {
        if ((obj->m_texture = g_textures.getTexture(obj->m_texturePath, obj->m_smooth))) {
            if (obj->m_texture->isAnimatedTexture()) {
                const auto& animatedTexture = std::static_pointer_cast<AnimatedTexture>(obj->m_texture);
                animatedTexture->setOnMap(true);
                animatedTexture->restart();
            }
        }
    }

    return obj;
}

int getBounce(const Bounce& bounce) {
    const auto minHeight = bounce.minHeight * g_drawPool.getScaleFactor();
    const auto height = bounce.height * g_drawPool.getScaleFactor();
    return minHeight + (height - std::abs(height - static_cast<int>(bounce.timer.ticksElapsed() / (bounce.speed / 100.f)) % static_cast<int>(height * 2)));
}

void AttachedEffect::draw(const Point& dest, const bool isOnTop, LightSink* lightView, const bool drawThing) {
    (void)dest; (void)isOnTop; (void)lightView; (void)drawThing;
}


bool AttachedEffect::getDrawFrame(const bool isOnTop, int& animationPhase, Point& offset, float& opacity)
{
    if (m_transform || m_texture || !m_texturePath.empty())
        return false;
    auto* thingType = getThingType();
    if (!thingType || thingType->isNull() || thingType->getAnimationPhases() == 0)
        return false;
    const auto& dirControl = m_offsetDirections[m_direction];
    if (dirControl.onTop != isOnTop)
        return false;

    const int animation = getCurrentAnimationPhase();
    if (m_loop > -1 && animation != m_lastAnimation) {
        m_lastAnimation = animation;
        if (animation == 0 && --m_loop == 0)
            return false;
    }

    const auto bounceAt = [](const Bounce& bounce) {
        return bounce.minHeight + (bounce.height - std::abs(bounce.height - static_cast<int>(bounce.timer.ticksElapsed() / (bounce.speed / 100.f)) % static_cast<int>(bounce.height * 2)));
    };
    offset = Point() - dirControl.offset;
    if (!m_toPoint.isNull()) {
        const float fraction = std::min<float>(m_animationTimer.ticksElapsed() / static_cast<float>(m_duration), 1.f);
        offset += m_toPoint * fraction;
    }
    if (m_bounce.height > 0 && m_bounce.speed > 0)
        offset -= bounceAt(m_bounce);
    opacity = m_opacity < 100 ? getOpacity() : 1.f;
    if (m_fade.height > 0 && m_fade.speed > 0)
        opacity = std::clamp<float>(bounceAt(m_fade) / 100.f, 0.f, 1.f);
    animationPhase = animation;
    return true;
}

void AttachedEffect::drawLight(const Point& dest, LightSink* lightView) {
    (void)dest; (void)lightView;
}


int AttachedEffect::getCurrentAnimationPhase()
{
    if (m_texture) {
        if (m_texture->isAnimatedTexture())
            std::static_pointer_cast<AnimatedTexture>(m_texture)->get(m_frame, m_animationTimer);
        return m_frame;
    }

    const auto thingType = getThingType();
    if (!thingType) return 0;

    const auto* animator = thingType->getIdleAnimator();
    if (!animator && thingType->isAnimateAlways())
        animator = thingType->getAnimator();

    if (animator)
        return animator->getPhaseAt(m_animationTimer, getSpeed());

    if (thingType->isEffect()) {
        const int animationPhases = thingType->getAnimationPhases();
        const float speed = getSpeed();
        if (animationPhases <= 0 || speed <= 0.f) return 0;

        const int lastPhase = animationPhases - 1;
        const int effectTicksPerFrame = g_gameConfig.getEffectTicksPerFrame();
        const int ticksPerFrame = std::max<int>(1, static_cast<int>(effectTicksPerFrame / speed));
        const int phase = std::min<int>(static_cast<int>(m_animationTimer.ticksElapsed() / ticksPerFrame), lastPhase);
        if (phase == lastPhase) m_animationTimer.restart();
        return phase;
    }

    if (thingType->isCreature() && thingType->isAnimateAlways()) {
        const int animationPhases = thingType->getAnimationPhases();
        const float speed = getSpeed();
        if (animationPhases <= 0 || speed <= 0.f) return 0;

        const int ticksPerFrame = std::max<int>(1, static_cast<int>(std::round((1000.0 / animationPhases) / speed)));
        const long long animationPeriod = static_cast<long long>(ticksPerFrame) * animationPhases;
        if (animationPeriod <= 0) return 0;

        return static_cast<int>((g_clock.millis() % animationPeriod) / ticksPerFrame);
    }

    return 0;
}

void AttachedEffect::setShader(const std::string_view name) { m_shader = std::string{ name }; }

void AttachedEffect::move(const Position& fromPosition, const Position& toPosition) {
    m_toPoint = Point(toPosition.x - fromPosition.x, toPosition.y - fromPosition.y) * g_gameConfig.getSpriteSize();
    m_animationTimer.restart();
}

ThingType* AttachedEffect::getThingType() const {
    return m_thingId > 0 ? g_things.getRawThingType(m_thingId, m_thingCategory) : nullptr;
}
