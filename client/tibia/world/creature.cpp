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

#include "creature.h"

#include "client/animator.h"
#include "attachedeffect.h"
#include "client/const.h"
#include "game.h"
#include "client/gameconfig.h"
#include "localplayer.h"
#include "map.h"
#include "framework/graphics/texturemanager.h"
#include "client/protocolcodes.h"
#include "client/statictext.h"
#include "tibia/assets/thingtype.h"
#include "tibia/assets/things.h"
#include "tile.h"
#include "paperdoll.h"
#include "framework/core/clock.h"
#include "framework/core/eventdispatcher.h"
#include "framework/core/scheduledevent.h"
#include "framework/ui/uiwidget.h"
#include <framework/core/graphicalapplication.h>
#include <framework/util/stats.h>

double Creature::speedA = 0;
double Creature::speedB = 0;
double Creature::speedC = 0;

Creature::Creature() :m_type(Proto::CreatureTypeUnknown)
{
    g_stats.addCreature();
    m_name.setAlign(Fw::AlignTopCenter);
    m_typingIconTexture = g_textures.getTexture(g_gameConfig.getTypingIcon());
}

Creature::~Creature() {
    setWidgetInformation(nullptr);
    g_stats.removeCreature();
}

bool Creature::isHidden() const {
    if (m_type == Proto::CreatureTypeHidden)
        return true;
    // Old protocol (< 1273): healthPercent=0 means health is hidden, not that the creature is dead
    return g_game.getClientVersion() < 1273 && m_healthPercent == 0;
}

void Creature::onCreate() {
    callLuaField("onCreate");
}

void Creature::draw(const Point& dest, const bool drawThings, LightSink* /*lightView*/)
{
}


void Creature::drawLight(const Point& dest, LightSink* lightView) {
    (void)dest; (void)lightView;
}


void Creature::draw(const Rect& destRect, const uint8_t size, const bool center)
{
}


void Creature::drawInformation(const MapPosInfo& mapRect, const Point& dest, int drawFlags)
{
    (void)mapRect; (void)dest; (void)drawFlags;
}


void Creature::internalDraw(Point dest, const Color& color)
{
    (void)dest; (void)color;
}


void Creature::turn(const Otc::Direction direction)
{
    // schedules to set the new direction when walk ends
    if (m_walking) {
        m_walkTurnDirection = direction;
        return;
    }

    // if is not walking change the direction right away
    setDirection(direction);
}

void Creature::walk(const Position& oldPos, const Position& newPos)
{
    if (oldPos == newPos)
        return;

    // get walk direction
    m_lastStepDirection = oldPos.getDirectionFromPosition(newPos);
    m_lastStepFromPosition = oldPos;
    m_lastStepToPosition = newPos;

    // set current walking direction
    setDirection(m_lastStepDirection);

    // starts counting walk
    m_walking = true;
    m_walkTimer.restart();
    m_walkedPixels = 0;

    // no direction need to be changed when the walk ends
    m_walkTurnDirection = Otc::InvalidDirection;

    if (m_walkFinishAnimEvent) {
        m_walkFinishAnimEvent->cancel();
        m_walkFinishAnimEvent = nullptr;
    }

    // starts updating walk
    nextWalkUpdate();
}

void Creature::stopWalk()
{
    if (!m_walking)
        return;

    // stops the walk right away
    terminateWalk();
}

void Creature::jump(const int height, const int duration)
{
    if (!m_jumpOffset.isNull())
        return;

    m_jumpTimer.restart();
    m_jumpHeight = height;
    m_jumpDuration = duration;

    updateJump();
}

void Creature::updateJump()
{
    if (m_jumpTimer.ticksElapsed() >= m_jumpDuration) {
        m_jumpOffset = PointF();
        return;
    }

    const int t = m_jumpTimer.ticksElapsed();
    const double a = -4 * m_jumpHeight / (m_jumpDuration * m_jumpDuration);
    const double b = +4 * m_jumpHeight / m_jumpDuration;
    const double height = a * t * t + b * t;

    const int roundHeight = std::round(height);
    const int halfJumpDuration = m_jumpDuration / 2;

    m_jumpOffset = PointF(height, height);

    if (isCameraFollowing()) {
        g_map.notificateCameraMove(m_walkOffset);
    }

    int nextT = 0;
    int diff = 0;
    int i = 1;
    if (m_jumpTimer.ticksElapsed() < halfJumpDuration)
        diff = 1;
    else if (m_jumpTimer.ticksElapsed() > halfJumpDuration)
        diff = -1;

    do {
        nextT = std::round((-b + std::sqrt(std::max<double>(b * b + 4 * a * (roundHeight + diff * i), 0.0)) * diff) / (2 * a));
        ++i;

        if (nextT < halfJumpDuration)
            diff = 1;
        else if (nextT > halfJumpDuration)
            diff = -1;
    } while (nextT - m_jumpTimer.ticksElapsed() == 0 && i < 3);

    // schedules next update
    const auto self = static_self_cast<Creature>();
    tibia::schedule::after([self] {
        self->updateJump();
    }, nextT - m_jumpTimer.ticksElapsed());
}

void Creature::onPositionChange(const Position& newPos, const Position& oldPos)
{
    callLuaFieldUnchecked("onPositionChange", newPos, oldPos);
}

void Creature::onAppear()
{
    // cancel any disappear event
    if (m_disappearEvent) {
        m_disappearEvent->cancel();
        m_disappearEvent = nullptr;
    }

    if (isCameraFollowing() && m_position != m_oldPosition) {
        g_map.notificateCameraMove(m_walkOffset);
    }

    // creature appeared the first time or wasn't seen for a long time
    if (m_removed) {
        stopWalk();
        m_removed = false;
        callLuaField("onAppear");
    } // walk
    else if (m_oldPosition != m_position && m_oldPosition.isInRange(m_position, 1, 1) && m_allowAppearWalk) {
        m_allowAppearWalk = false;
        walk(m_oldPosition, m_position);
        callLuaField("onWalk", m_oldPosition, m_position);
    } // teleport
    else if (m_oldPosition != m_position) {
        stopWalk();
        callLuaField("onDisappear");
        callLuaField("onAppear");
    } // else turn
}

void Creature::onDisappear()
{
    if (m_disappearEvent)
        m_disappearEvent->cancel();

    m_oldPosition = m_position;

    // a pair onDisappear and onAppear events are fired even when creatures walks or turns,
    // so we must filter
    const auto self = static_self_cast<Creature>();
    m_disappearEvent = tibia::schedule::post([self] {
        self->m_removed = true;
        self->stopWalk();

        self->callLuaField("onDisappear");

        // invalidate this creature position
        if (!self->isLocalPlayer())
            self->setPosition(Position());

        self->m_oldPosition = {};
        self->m_disappearEvent = nullptr;

        if (g_game.getAttackingCreature() == self)
            g_game.cancelAttack();
        else if (g_game.getFollowingCreature() == self)
            g_game.cancelFollow();
    });

    Thing::onDisappear();
}

void Creature::onDeath()
{
    callLuaField("onDeath");
}

void Creature::updateWalkAnimation()
{
    if (!m_outfit.isCreature())
        return;

    const auto mountType = getMountThingType();
    int footAnimPhases = mountType ? mountType->getAnimationPhases() : getAnimationPhases();
    if (!g_game.getFeature(Otc::GameEnhancedAnimations) && footAnimPhases > 2) {
        --footAnimPhases;
    }

    // looktype has no animations
    if (footAnimPhases == 0)
        return;

    // diagonal walk is taking longer than the animation, thus why don't animate continously
    if (m_walkTimer.ticksElapsed() < getStepDuration() && m_walkedPixels == g_gameConfig.getSpriteSize()) {
        m_walkAnimationPhase = 0;
        return;
    }

    int minFootDelay = 20;
    const int maxFootDelay = footAnimPhases > 2 ? 80 : 205;
    int footAnimDelay = footAnimPhases;

    if (g_game.getFeature(Otc::GameEnhancedAnimations) && footAnimPhases > 2) {
        minFootDelay += 10;
        if (footAnimDelay > 1)
            footAnimDelay /= 1.5;
    }

    const auto walkSpeed = m_walkingAnimationSpeed > 0 ? m_walkingAnimationSpeed : m_stepCache.getDuration(m_lastStepDirection);
    const int footDelay = std::clamp<int>(walkSpeed / footAnimDelay, minFootDelay, maxFootDelay);

    if (m_footTimer.ticksElapsed() >= footDelay) {
        if (m_walkAnimationPhase == footAnimPhases) m_walkAnimationPhase = 1;
        else ++m_walkAnimationPhase;

        m_footTimer.restart();
    }
}

void Creature::updateWalkOffset(const uint8_t totalPixelsWalked)
{
    m_walkOffset = {};
    if (m_direction == Otc::North || m_direction == Otc::NorthEast || m_direction == Otc::NorthWest)
        m_walkOffset.y = g_gameConfig.getSpriteSize() - totalPixelsWalked;
    else if (m_direction == Otc::South || m_direction == Otc::SouthEast || m_direction == Otc::SouthWest)
        m_walkOffset.y = totalPixelsWalked - g_gameConfig.getSpriteSize();

    if (m_direction == Otc::East || m_direction == Otc::NorthEast || m_direction == Otc::SouthEast)
        m_walkOffset.x = totalPixelsWalked - g_gameConfig.getSpriteSize();
    else if (m_direction == Otc::West || m_direction == Otc::NorthWest || m_direction == Otc::SouthWest)
        m_walkOffset.x = g_gameConfig.getSpriteSize() - totalPixelsWalked;
}

void Creature::updateWalkingTile()
{
    // determine new walking tile
    TilePtr newWalkingTile;

    const auto displacementX = g_game.getFeature(Otc::GameNegativeOffset) ? 0 : getDisplacementX();
    const auto displacementY = g_game.getFeature(Otc::GameNegativeOffset) ? 0 : getDisplacementY();

    const Rect virtualCreatureRect(g_gameConfig.getSpriteSize() + (m_walkOffset.x - displacementX),
        g_gameConfig.getSpriteSize() + (m_walkOffset.y - displacementY),
        g_gameConfig.getSpriteSize(), g_gameConfig.getSpriteSize());

    for (int xi = -1; xi <= 1 && !newWalkingTile; ++xi) {
        for (int yi = -1; yi <= 1 && !newWalkingTile; ++yi) {
            Rect virtualTileRect((xi + 1) * g_gameConfig.getSpriteSize(), (yi + 1) * g_gameConfig.getSpriteSize(), g_gameConfig.getSpriteSize(), g_gameConfig.getSpriteSize());

            // only render creatures where bottom right is inside tile rect
            if (virtualTileRect.contains(virtualCreatureRect.bottomRight()))
                newWalkingTile = g_map.getOrCreateTile(getPosition().translated(xi, yi, 0));
        }
    }

    // NW - for the effect of going behind the object west of it, walking creature will be drawn in front of the object for the half of the way, and after behind
    // SE - for the effect of going in front the object south of it, walking creature will be drawn behind the object for the half of the way, and after in front
    if (m_walkedPixels < g_gameConfig.getSpriteSize() / 2) {
        if (m_direction == Otc::Direction::NorthWest)
            newWalkingTile = m_walkingTile ? m_walkingTile : getTile();
        else if (m_direction == Otc::Direction::SouthEast)
            newWalkingTile = g_map.getTile(getPosition().translated(-1, -1, 0));
    }

    if (newWalkingTile == m_walkingTile) return;

    const auto& self = static_self_cast<Creature>();

    if (m_walkingTile)
        m_walkingTile->removeWalkingCreature(self);

    if (newWalkingTile) {
        newWalkingTile->addWalkingCreature(self);
        if (isCameraFollowing())
            g_map.notificateTileUpdate(newWalkingTile->getPosition(), self, Otc::OPERATION_CLEAN);
    }

    m_walkingTile = newWalkingTile;
}

void Creature::nextWalkUpdate()
{
    // remove any previous scheduled walk updates
    if (m_walkUpdateEvent)
        m_walkUpdateEvent->cancel();

    // do the update
    updateWalk();
    onWalking();

    if (!m_walking) return;

    auto action = [self = static_self_cast<Creature>()] {
        self->m_walkUpdateEvent = nullptr;
        self->nextWalkUpdate();
    };

    m_walkUpdateEvent = isCameraFollowing() ? tibia::schedule::post(action) : tibia::schedule::after(action, m_stepCache.walkDuration);
}

void Creature::advanceWalkTween()
{
    if (m_walking)
        updateWalk();
}

void Creature::updateWalk()
{
    const auto stepDuration = getStepDuration(true);
    if (stepDuration == 0)
        return;

    const float walkTicksPerPixel = stepDuration / static_cast<float>(g_gameConfig.getSpriteSize());
    if (walkTicksPerPixel <= 0.f)
        return;

    const int totalPixelsWalked = std::min<int>(m_walkTimer.ticksElapsed() / walkTicksPerPixel, g_gameConfig.getSpriteSize());

    // needed for paralyze effect
    m_walkedPixels = std::max<int>(m_walkedPixels, totalPixelsWalked);

    const auto oldWalkOffset = m_walkOffset;

    updateWalkAnimation();
    updateWalkOffset(m_walkedPixels);
    updateWalkingTile();

    if (isCameraFollowing() && oldWalkOffset != m_walkOffset) {
        g_map.notificateCameraMove(m_walkOffset);
    }

    if (m_walkedPixels == g_gameConfig.getSpriteSize()) {
        terminateWalk();
    }
}

void Creature::terminateWalk()
{
    // remove any scheduled walk update
    if (m_walkUpdateEvent) {
        m_walkUpdateEvent->cancel();
        m_walkUpdateEvent = nullptr;
    }

    // now the walk has ended, do any scheduled turn
    if (m_walkTurnDirection != Otc::InvalidDirection) {
        setDirection(m_walkTurnDirection);
        m_walkTurnDirection = Otc::InvalidDirection;
    }

    if (m_walkingTile) {
        m_walkingTile->removeWalkingCreature(static_self_cast<Creature>());
        m_walkingTile = nullptr;
    }

    m_walkedPixels = 0;
    m_walkOffset = {};
    m_walking = false;

    const auto self = static_self_cast<Creature>();
    m_walkFinishAnimEvent = tibia::schedule::after([self] {
        self->m_walkAnimationPhase = 0;
        self->m_walkFinishAnimEvent = nullptr;
    }, g_game.getServerBeat());
}

void Creature::setHealthPercent(const uint8_t healthPercent)
{
    static constexpr Color
        COLOR1(0x00, 0xBC, 0x00),
        COLOR2(0x50, 0xA1, 0x50),
        COLOR3(0xA1, 0xA1, 0x00),
        COLOR4(0xBF, 0x0A, 0x0A),
        COLOR5(0x91, 0x0F, 0x0F),
        COLOR6(0x85, 0x0C, 0x0C);

    if (m_healthPercent == healthPercent) return;

    if (healthPercent > 92)
        m_informationColor = COLOR1;
    else if (healthPercent > 60)
        m_informationColor = COLOR2;
    else if (healthPercent > 30)
        m_informationColor = COLOR3;
    else if (healthPercent > 8)
        m_informationColor = COLOR4;
    else if (healthPercent > 3)
        m_informationColor = COLOR5;
    else
        m_informationColor = COLOR6;

    const uint8_t oldHealthPercent = m_healthPercent;
    m_healthPercent = healthPercent;

    callLuaField("onHealthPercentChange", healthPercent, oldHealthPercent);

    // qtclient-death clamps HP to 0 for vitals without a real death packet / death UI.
    if (isDead()) {
        if (isLocalPlayer() && static_self_cast<LocalPlayer>()->isSoftDead())
            return;
        onDeath();
    }
}

void Creature::setDirection(const Otc::Direction direction)
{
    if (direction == Otc::InvalidDirection)
        return;

    m_direction = direction;

    // xPattern => creature direction
    if (direction == Otc::NorthEast || direction == Otc::SouthEast)
        m_numPatternX = Otc::East;
    else if (direction == Otc::NorthWest || direction == Otc::SouthWest)
        m_numPatternX = Otc::West;
    else
        m_numPatternX = direction;

    setAttachedEffectDirection(static_cast<Otc::Direction>(m_numPatternX));
    setPaperdollsDirection(static_cast<Otc::Direction>(m_numPatternX));
}

void Creature::setOutfit(const Outfit& outfit, bool fireEvent)
{
    if (m_outfit == outfit)
        return;

    Outfit newOutfit = outfit;
    if (newOutfit.isInvalid()) {
        newOutfit.setCategory(newOutfit.getAuxId() > 0 ? ThingCategoryItem : ThingCategoryCreature);
    }

    const Outfit oldOutfit = m_outfit;

    m_outfit = newOutfit;
    m_numPatternZ = 0;
    m_exactSize = 0;
    if (m_walkingAnimationSpeed == 0) {
        m_walkAnimationPhase = 0; // might happen when player is walking and outfit is changed.
    }

    if (m_outfit.isInvalid())
        m_outfit.setCategory(m_outfit.getAuxId() > 0 ? ThingCategoryItem : ThingCategoryCreature);

    const auto thingType = getThingType();
    if (!thingType) {
        g_logger.error("Creature::setOutfit - Invalid thing type for creature {}.", getId());
        m_outfit = oldOutfit;
        return;
    }

    m_clientId = thingType->getId();

    if (m_outfit.hasMount()) {
        m_numPatternZ = std::min<int>(1, getNumPatternZ() - 1);
    }

    if ((g_game.getFeature(Otc::GameWingsAurasEffectsShader))) {
        m_outfit.setWing(0);
        m_outfit.setAura(0);
        m_outfit.setEffect(0);
        m_outfit.setShader("Outfit - Default");
    }

    if (const auto& tile = getTile())
        tile->checkForDetachableThing();

    if (fireEvent)
        callLuaField("onOutfitChange", m_outfit, oldOutfit);
}

void Creature::setSpeed(uint16_t speed)
{
    if (speed == m_speed)
        return;

    const uint16_t oldSpeed = m_speed;
    m_speed = speed;

    // Cache for stepSpeed Law
    if (hasSpeedFormula()) {
        speed *= 2;

        if (speed > -speedB) {
            m_calculatedStepSpeed = std::max<int>(1, floor((speedA * log((speed / 2.) + speedB) + speedC) + .5));
        } else m_calculatedStepSpeed = 1;
    }

    if (isLocalPlayer())
        static_self_cast<LocalPlayer>()->registerAdjustInvalidPosEvent();

    // speed can change while walking (utani hur, paralyze, etc..)
    if (m_walking)
        nextWalkUpdate();

    callLuaField("onSpeedChange", m_speed, oldSpeed);
}

void Creature::setBaseSpeed(const uint16_t baseSpeed)
{
    if (m_baseSpeed == baseSpeed)
        return;

    const uint16_t oldBaseSpeed = m_baseSpeed;
    m_baseSpeed = baseSpeed;

    callLuaField("onBaseSpeedChange", baseSpeed, oldBaseSpeed);
}

void Creature::setType(const uint8_t v) { if (m_type != v) callLuaField("onTypeChange", m_type = v); }
void Creature::setIcon(const uint8_t v) { if (m_icon != v) callLuaField("onIconChange", m_icon = v); }
void Creature::setIcons(const std::vector<std::tuple<uint8_t, uint8_t, uint16_t>>& icons)
{
    if (!m_icons) {
        m_icons = std::make_unique<IconRenderData>();
        m_icons->numberText.setAlign(Fw::AlignCenter);
    }

    m_icons->atlasGroups.clear();
    m_icons->iconEntries = icons;

    for (const auto& [icon, category, count] : icons) {
        callLuaField("onIconsChange", icon, category, count);
    }
}

void Creature::setEchoRaidColor(const int8_t color)
{
    if (m_echoRaidColor == color)
        return;

    // CIP uses name fill/outline (see drawInformation), not static squares.
    if (m_echoRaidColor >= 0 && m_showStaticSquare)
        hideStaticSquare();

    m_echoRaidColor = color;
    callLuaField("onEchoRaidColorChange", color);
}

void Creature::setSkull(const uint8_t v) { if (m_skull != v) callLuaField("onSkullChange", m_skull = v); }
void Creature::setShield(const uint8_t v) { if (m_shield != v) callLuaField("onShieldChange", m_shield = v); }
void Creature::setEmblem(const uint8_t v) { if (m_emblem != v) callLuaField("onEmblemChange", m_emblem = v); }

void Creature::setTypeTexture(const std::string& filename) { m_typeTexture = g_textures.getTexture(filename); }
void Creature::setIconTexture(const std::string& filename) { m_iconTexture = g_textures.getTexture(filename); }
void Creature::clearIconsAtlas()
{
    if (m_icons) {
        m_icons->atlasGroups.clear();
    }
}

void Creature::setIconsTexture(const std::string& filename, const Rect& clip, const uint16_t count)
{
    if (!m_icons) {
        m_icons = std::make_unique<IconRenderData>();
        m_icons->numberText.setAlign(Fw::AlignCenter);
    }

    m_icons->atlasGroups.emplace_back(IconRenderData::AtlasIconGroup{ g_textures.getTexture(filename), clip, count });
}
void Creature::setSkullTexture(const std::string& filename) { m_skullTexture = g_textures.getTexture(filename); }
void Creature::setEmblemTexture(const std::string& filename) { m_emblemTexture = g_textures.getTexture(filename); }

void Creature::setShieldTexture(const std::string& filename, const bool blink)
{
    m_shieldTexture = g_textures.getTexture(filename);
    m_showShieldTexture = true;

    if (blink && !m_shieldBlink) {
        auto self = static_self_cast<Creature>();
        tibia::schedule::after([self] {
            self->updateShield();
        }, g_gameConfig.getShieldBlinkTicks());
    }

    m_shieldBlink = blink;
}

void Creature::addTimedSquare(const uint8_t color)
{
    m_showTimedSquare = true;
    m_timedSquareColor = Color::from8bit(color != 0 ? color : 1);

    // schedule removal
    const auto self = static_self_cast<Creature>();
    tibia::schedule::after([self] {
        self->removeTimedSquare();
    }, g_gameConfig.getVolatileSquareDuration());
}

void Creature::updateShield()
{
    m_showShieldTexture = !m_showShieldTexture;

    if (m_shield != Otc::ShieldNone && m_shieldBlink) {
        auto self = static_self_cast<Creature>();
        tibia::schedule::after([self] {
            self->updateShield();
        }, g_gameConfig.getShieldBlinkTicks());
    } else if (!m_shieldBlink)
        m_showShieldTexture = true;
}

int getSmoothedElevation(const Creature* creature, const int currentElevation, const float factor) {
    const auto& fromPos = creature->getLastStepFromPosition();
    const auto& toPos = creature->getLastStepToPosition();
    const auto& fromTile = g_map.getTile(fromPos);
    const auto& toTile = g_map.getTile(toPos);

    if (!fromTile || !toTile) {
        return currentElevation;
    }

    const int fromElevation = fromTile->getDrawElevation();
    const int toElevation = toTile->getDrawElevation();

    return fromElevation != toElevation ? fromElevation + factor * (toElevation - fromElevation) : currentElevation;
}

int Creature::getDrawElevation() {
    if (m_walkingTile) {
        int elevation = m_walkingTile->getDrawElevation();

        if (g_game.getFeature(Otc::GameSmoothWalkElevation)) {
            const auto stepDuration = m_stepCache.getDuration(m_lastStepDirection);
            if (stepDuration > 0) {
                const float factor = std::clamp<float>(getWalkTicksElapsed() / static_cast<float>(stepDuration), .0f, 1.f);
                elevation = getSmoothedElevation(this, elevation, factor);
            }
        }

        return elevation;
    }

    if (const auto& tile = g_map.getTile(getPosition()))
        return tile->getDrawElevation();

    return 0;
}

bool Creature::hasSpeedFormula() { return g_game.getFeature(Otc::GameNewSpeedLaw) && speedA != 0 && speedB != 0 && speedC != 0; }

uint16_t Creature::getStepDuration(const bool ignoreDiagonal, const Otc::Direction dir)
{
    if (m_speed < 1)
        return 0;

    const auto& tilePos = dir == Otc::InvalidDirection ?
        m_lastStepToPosition : getPosition().translatedToDirection(dir);

    const auto& tile = g_map.getTile(tilePos.isValid() ? tilePos : getPosition());

    const int serverBeat = g_game.getServerBeat();

    int groundSpeed = 0;
    if (tile) groundSpeed = tile->getGroundSpeed();
    if (groundSpeed == 0)
        groundSpeed = 150;

    if (groundSpeed != m_stepCache.groundSpeed || m_speed != m_stepCache.speed) {
        m_stepCache.speed = m_speed;
        m_stepCache.groundSpeed = groundSpeed;

        uint32_t stepDuration = 1000 * groundSpeed;
        if (hasSpeedFormula()) {
            stepDuration /= m_calculatedStepSpeed;
        } else stepDuration /= m_speed;

        if (g_gameConfig.isForcingNewWalkingFormula() || g_game.getClientVersion() >= 860) {
            stepDuration = ((stepDuration + serverBeat - 1) / serverBeat) * serverBeat;
        }

        m_stepCache.duration = stepDuration;

        m_stepCache.walkDuration = std::min<int>(stepDuration / g_gameConfig.getSpriteSize(), 16);

        m_stepCache.diagonalDuration = stepDuration *
            (g_game.getClientVersion() > 810 || g_gameConfig.isForcingNewWalkingFormula()
                ? (isPlayer() ? g_gameConfig.getPlayerDiagonalWalkSpeed() : g_gameConfig.getCreatureDiagonalWalkSpeed())
                : 2);
    }

    auto duration = ignoreDiagonal ? m_stepCache.duration : m_stepCache.getDuration(m_lastStepDirection);

    if (isCameraFollowing() && isLocalPlayer()) {
        const auto& localPlayer = static_self_cast<LocalPlayer>();
        duration += 10 * std::max<int>(1, localPlayer->getPreWalkingSize());
    }

    return duration;
}

Point Creature::getDisplacement() const
{
    if (m_outfit.isEffect())
        return { 8 };

    if (m_outfit.isItem())
        return {};

    return Thing::getDisplacement();
}

int Creature::getDisplacementX() const
{
    if (m_outfit.isEffect())
        return 8;

    if (m_outfit.isItem())
        return 0;

    if (const auto mountType = getMountThingType(); mountType)
        return mountType->getDisplacementX();

    return Thing::getDisplacementX();
}

int Creature::getDisplacementY() const
{
    if (m_outfit.isEffect())
        return 8;

    if (m_outfit.isItem())
        return 0;

    if (const auto mountType = getMountThingType(); mountType)
        return mountType->getDisplacementY();

    return Thing::getDisplacementY();
}

const Light& Creature::getLight() const
{
    const auto& light = Thing::getLight();
    return m_light.color > 0 && m_light.intensity >= light.intensity ? m_light : light;
}

ThingType* Creature::getThingType() const {
    return g_things.getRawThingType(m_outfit.isCreature() ? m_outfit.getId() : m_outfit.getAuxId(), m_outfit.getCategory());
}

ThingType* Creature::getMountThingType() const {
    return m_outfit.hasMount() ? g_things.getRawThingType(m_outfit.getMount(), ThingCategoryCreature) : nullptr;
}

uint16_t Creature::getCurrentAnimationPhase(const bool mount)
{
    if (!canAnimate()) return 0;

    const auto thingType = mount ? getMountThingType() : getThingType();
    if (!thingType) return 0;

    if (const auto idleAnimator = thingType->getIdleAnimator()) {
        if (m_walkAnimationPhase == 0) return idleAnimator->getPhase();
        return m_walkAnimationPhase + idleAnimator->getAnimationPhases() - 1;
    }

    if (thingType->isAnimateAlways()) {
        if (const auto animator = thingType->getAnimator()) {
            return static_cast<uint16_t>(thingType->getIdleAnimationPhases() + animator->getPhase());
        }

        const int animationPhases = thingType->getAnimationPhases();
        if (animationPhases <= 0) return 0;

        const int ticksPerFrame = std::max<int>(1, static_cast<int>(std::round(1000.0 / animationPhases)));
        const long long animationPeriod = static_cast<long long>(ticksPerFrame) * animationPhases;
        if (animationPeriod <= 0) return 0;

        // When a static idle frame group exists (no idleAnimator but idle sprites are present),
        // skip over the idle phase(s) so animateAlways only cycles through the moving frames.
        const int idlePhases = thingType->getIdleAnimationPhases();
        return static_cast<uint16_t>(idlePhases + (g_clock.millis() % animationPeriod) / ticksPerFrame);
    }

    return isDisabledWalkAnimation() ? 0 : m_walkAnimationPhase;
}

uint16_t Creature::peekAnimationPhase(const bool mount)
{
    if (!canAnimate())
        return 0;

    const auto thingType = mount ? getMountThingType() : getThingType();
    if (!thingType)
        return 0;

    if (const auto idleAnimator = thingType->getIdleAnimator()) {
        if (m_walkAnimationPhase == 0)
            return static_cast<uint16_t>(idleAnimator->peekPhase());
        return m_walkAnimationPhase + idleAnimator->getAnimationPhases() - 1;
    }

    if (thingType->isAnimateAlways()) {
        if (const auto animator = thingType->getAnimator())
            return static_cast<uint16_t>(thingType->getIdleAnimationPhases() + animator->peekPhase());

        const int animationPhases = thingType->getAnimationPhases();
        if (animationPhases <= 0)
            return 0;

        const int ticksPerFrame = std::max<int>(1, static_cast<int>(std::round(1000.0 / animationPhases)));
        const long long animationPeriod = static_cast<long long>(ticksPerFrame) * animationPhases;
        if (animationPeriod <= 0)
            return 0;

        const int idlePhases = thingType->getIdleAnimationPhases();
        return static_cast<uint16_t>(idlePhases + (g_clock.millis() % animationPeriod) / ticksPerFrame);
    }

    return isDisabledWalkAnimation() ? 0 : m_walkAnimationPhase;
}

int Creature::getExactSize(int layer, int /*xPattern*/, int yPattern, int zPattern, int /*animationPhase*/)
{
    if (m_exactSize > 0)
        return m_exactSize;

    uint8_t exactSize = 0;
    if (m_outfit.isCreature()) {
        const int layers = getLayers();

        zPattern = m_outfit.hasMount() ? 1 : 0;

        if (yPattern > 0) {
            for (int pattern = 0; pattern < yPattern; ++pattern) {
                if (pattern > 0 && !(m_outfit.getAddons() & (1 << (yPattern - 1))))
                    continue;

                for (layer = 0; layer < layers; ++layer)
                    exactSize = std::max<int>(exactSize, Thing::getExactSize(layer, 0, yPattern, zPattern, 0));
            }
        } else {
            for (layer = 0; layer < layers; ++layer)
                exactSize = std::max<int>(exactSize, Thing::getExactSize(layer, 0, yPattern, zPattern, 0));
        }
    } else {
        exactSize = getThingType()->getExactSize();
    }

    return m_exactSize = std::max<uint8_t>(exactSize, g_gameConfig.getSpriteSize());
}

void Creature::setMountShader(const std::string_view name) {
    m_mountShaderId = name.empty() ? 0 : 1;
}

void Creature::setTypingIconTexture(const std::string& filename)
{
    m_typingIconTexture = g_textures.getTexture(filename);
}

void Creature::setTyping(const bool typing)
{
    m_typing = typing;
}

void Creature::sendTyping() {
    g_game.sendTyping(m_typing);
}

void Creature::onStartAttachEffect(const AttachedEffectPtr& effect) {
    if (effect->isDisabledWalkAnimation()) {
        setDisableWalkAnimation(true);
    }

    if (effect->getThingType() && (effect->getThingType()->isCreature() || effect->getThingType()->isMissile()))
        effect->m_direction = getDirection();
}

void Creature::onDispatcherAttachEffect(const AttachedEffectPtr& effect) {
    if (effect->isTransform() && effect->getThingType()) {
        const auto& outfit = getOutfit();
        if (outfit.isTemp())
            return;

        effect->m_outfitOwner = outfit;

        Outfit newOutfit = outfit;
        newOutfit.setTemp(true);
        newOutfit.setCategory(effect->getThingType()->getCategory());
        if (newOutfit.isCreature())
            newOutfit.setId(effect->getThingType()->getId());
        else
            newOutfit.setAuxId(effect->getThingType()->getId());

        setOutfit(newOutfit);
    }
}

void Creature::onStartDetachEffect(const AttachedEffectPtr& effect) {
    if (effect->isDisabledWalkAnimation())
        setDisableWalkAnimation(false);

    if (effect->isTransform() && !effect->m_outfitOwner.isInvalid()) {
        setOutfit(effect->m_outfitOwner);
    }
}

void Creature::setStaticWalking(const uint16_t v) {
    if (!canDraw())
        return;

    if (m_walkUpdateEvent) {
        m_walkUpdateEvent->cancel();
        m_walkUpdateEvent = nullptr;
    }

    m_walkingAnimationSpeed = v;

    if (v == 0) {
        m_walkAnimationPhase = 0;
        return;
    }

    m_walkUpdateEvent = tibia::schedule::every([self = static_self_cast<Creature>()] {
        self->updateWalkAnimation();
        if (self.use_count() == 1) {
            self->m_walkUpdateEvent->cancel();
        }
    }, std::min<int>(v / g_gameConfig.getSpriteSize(), 16));
}

void Creature::setWidgetInformation(const UIWidgetPtr& info) {
    if (m_widgetInformation == info)
        return;

    m_widgetInformation = info;
}

void Creature::setName(const std::string_view name) {
    if (name == m_name.getText())
        return;

    const auto& oldName = m_name.getText();
    m_name.setText(name);
    callLuaField("onChangeName", name, oldName);
}

void Creature::setCovered(bool covered) {
    if (m_isCovered == covered)
        return;

    const auto oldCovered = m_isCovered;
    m_isCovered = covered;

    tibia::schedule::post([self = static_self_cast<Creature>(), covered, oldCovered] {
        self->callLuaField("onCovered", covered, oldCovered);
    });
}

void Creature::setText(const std::string& text, const Color& color)
{
    if (!m_text) {
        m_text = std::make_shared<StaticText>();
    }
    m_text->setText(text);
    m_text->setColor(color);
}

std::string Creature::getText()
{
    if (!m_text) {
        return "";
    }
    return m_text->getText();
}

bool Creature::canShoot(int distance)
{
    return getTile() ? getTile()->canShoot(distance) : false;
}

bool Creature::hasPaperdoll(uint16_t id) {
    for (const auto& pd : m_paperdolls) {
        if (pd->m_id == id)
            return true;
    }

    return false;
}

void Creature::attachPaperdoll(const PaperdollPtr& obj) {
    if (!obj) return;

    obj->m_direction = getDirection();

    uint_fast8_t i = 0;
    for (const auto& pd : m_paperdolls) {
        if (obj->m_priority < pd->m_priority)
            break;
        ++i;
    }

    m_paperdolls.insert(m_paperdolls.begin() + i, obj);

    tibia::schedule::post([paperdoll = obj, self = static_self_cast<Thing>()] {
        paperdoll->callLuaField("onAttach", self->asLuaObject());
    });
}

bool Creature::detachPaperdollById(uint16_t id) {
    const auto it = std::find_if(m_paperdolls.begin(), m_paperdolls.end(),
                                 [id](const PaperdollPtr& obj) { return obj->getId() == id; });

    if (it == m_paperdolls.end())
        return false;

    onDetachPaperdoll(*it);
    m_paperdolls.erase(it);

    return true;
}

bool Creature::detachPaperdollByPriority(uint8_t priority) {
    bool finded = false;
    for (auto it = m_paperdolls.begin(); it != m_paperdolls.end();) {
        const auto& obj = *it;
        if (obj->getPriority() == priority) {
            onDetachPaperdoll(obj);
            it = m_paperdolls.erase(it);
            finded = true;
        } else ++it;
    }

    return finded;
}

void Creature::onDetachPaperdoll(const PaperdollPtr& paperdoll) {
    paperdoll->callLuaField("onDetach", asLuaObject());
}

void Creature::clearPaperdolls() {
    for (const auto& e : m_paperdolls)
        onDetachPaperdoll(e);
    m_paperdolls.clear();
}

PaperdollPtr Creature::getPaperdollById(uint16_t id) {
    const auto it = std::find_if(m_paperdolls.begin(), m_paperdolls.end(),
                                 [id](const PaperdollPtr& obj) { return obj->getId() == id; });

    if (it == m_paperdolls.end())
        return nullptr;

    return *it;
}

void Creature::setPaperdollsDirection(Otc::Direction dir) const
{
    for (const auto& paperdoll : m_paperdolls) {
        if (paperdoll->m_thingType)
            paperdoll->m_direction = dir;
    }
}
