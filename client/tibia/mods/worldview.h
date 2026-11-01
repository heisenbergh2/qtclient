#pragma once

#include "renderer/worldgeometry.h"

#include <client/position.h>
#include <framework/util/point.h>

#include <QImage>
#include <QPoint>
#include <QQuickWindow>
#include <QSGNode>

#include <cstdint>
#include <vector>

class ThingType;
class Tile;
class Thing;

namespace tibia::mods {

/// Optional product lighting that replaces/augments CIP classic light for a frame.
/// Cached once at mod bootstrap — hot path is a null check then virtual calls only when set.
/// Packs (e.g. product raycast) implement this; stock never links product lighting code.
class WorldLightingDriver
{
public:
    virtual ~WorldLightingDriver() = default;

    virtual bool enabled() const = 0;
    virtual bool frameActive() const = 0;
    /// False until setFieldAmbient filled the pack's classic ambient grid (first raycast frame).
    virtual bool hasFieldAmbient() const { return true; }
    virtual qint64 gatherNs() const { return 0; }
    virtual qint64 roofsNs() const { return 0; }

    virtual void setFieldAmbient(QImage ambient) = 0;

    virtual bool beginFrame(const Position& camera, int drawWidth, int drawHeight, int firstFloor,
                            int lastFloor) = 0;
    virtual void beginFloor(int z) = 0;
    virtual void resetShade(Tile& tile, int ix, int iy) = 0;
    virtual void endFrame(const qmlcomponents::WorldGeometry* geometry) = 0;

    virtual void addCenterLight(const Point& center, uint8_t intensity, uint8_t color) = 0;
    virtual void addFrame(ThingType& type, const Point& anchor, uint32_t spriteId, int xPattern) = 0;
    virtual int stampPhase(int animationPhase) const = 0;

    virtual void beginThing(Thing& thing, uint8_t lift) = 0;
    virtual void endThing() = 0;

    class ThingScope
    {
    public:
        ThingScope(WorldLightingDriver* driver, Thing& thing, uint8_t lift)
            : m_driver(driver)
        {
            if (m_driver)
                m_driver->beginThing(thing, lift);
        }
        ~ThingScope()
        {
            if (m_driver)
                m_driver->endThing();
        }
        ThingScope(const ThingScope&) = delete;
        ThingScope& operator=(const ThingScope&) = delete;
        ThingScope(ThingScope&& other) noexcept
            : m_driver(other.m_driver)
        {
            other.m_driver = nullptr;
        }
        ThingScope& operator=(ThingScope&&) = delete;

    private:
        WorldLightingDriver* m_driver = nullptr;
    };

    [[nodiscard]] ThingScope thing(Thing& thing, uint8_t lift) { return ThingScope(this, thing, lift); }
};

/// Pack-owned scene-graph overlays after world sprites. Each factory owns its node pointer.
/// Hot path: wantsNode + sync only (no ModLoader / registry scans).
class WorldOverlayFactory
{
public:
    virtual ~WorldOverlayFactory() = default;
    virtual int order() const { return 100; }
    /// Insert/update/remove this factory's node under `camera` after `afterSprites`.
    virtual void updateScene(QQuickWindow* window, QSGNode* camera, QSGNode* afterSprites) = 0;
    /// Host is about to destroy the camera tree — drop dangling QSGNode*s.
    virtual void abandonNode() {}
};

struct WorldFrameHooks {
    bool requestOutdoorGather = false;
};

void setWorldLightingDriver(WorldLightingDriver* driver);
WorldLightingDriver* worldLightingDriver();

void addWorldOverlayFactory(WorldOverlayFactory* factory);
void clearWorldOverlayFactories();
void abandonWorldOverlayNodes();
const std::vector<WorldOverlayFactory*>& worldOverlayFactories();

void setWorldFrameHooks(WorldFrameHooks hooks);
const WorldFrameHooks& worldFrameHooks();

} // namespace tibia::mods
