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
#include "client/declarations.h"
#include "client/staticdata.h"
#include "framework/core/inputevent.h"
#include "framework/ui/declarations.h"

#include <atomic>
#include <functional>
#include <memory>
#include <unordered_set>

namespace tibia::minimap {
class TWaypointCosts;
}

class TileBlock
{
public:
    TileBlock() { m_tiles.fill(nullptr); }

    const TilePtr& create(const Position& pos);
    const TilePtr& getOrCreate(const Position& pos);
    const TilePtr& get(const Position& pos) { return m_tiles[getTileIndex(pos)]; }
    void remove(const Position& pos) { m_tiles[getTileIndex(pos)] = nullptr; }

    uint32_t getTileIndex(const Position& pos) { return ((pos.y % BLOCK_SIZE) * BLOCK_SIZE) + (pos.x % BLOCK_SIZE); }

    const std::array<TilePtr, BLOCK_SIZE* BLOCK_SIZE>& getTiles() const { return m_tiles; }

private:
    std::array<TilePtr, BLOCK_SIZE* BLOCK_SIZE> m_tiles;
};

struct PathFindResult
{
    Otc::PathFindResult status = Otc::PathFindResultNoWay;
    std::vector<Otc::Direction> path;
    int complexity = 0;
    Position start;
    Position destination;
};
using PathFindResult_ptr = std::shared_ptr<PathFindResult>;

struct Node
{
    float cost;
    float totalCost;
    Position pos;
    Node* prev;
    int distance;
    int unseen;
};

/// Engine-side listener for the fields the server describes (the Qt client's
/// minimap). Called on the parsing thread, once per field object change.
class MapObserver
{
public:
    virtual ~MapObserver() = default;
    /// An item or creature was added to or removed from the field, or the
    /// field was cleaned by a description. Not called for fields dropped
    /// because they left the aware range.
    virtual void onTileChanged(const Position& pos) = 0;
    /// A map description or move message is fully parsed (central position set).
    /// `fullDescription`: the whole window of the central floor was described
    /// (a map or central-floor description, or a floor change), not a step.
    virtual void onWindowChanged(bool fullDescription) = 0;
};

//@bindsingleton g_map
class Map
{
public:
    void init();
    void terminate();

    void notificateTileUpdate(const Position& pos, const ThingPtr& thing, Otc::Operation operation);
    void notificateWindowChanged(bool fullDescription) const { if (m_observer) m_observer->onWindowChanged(fullDescription); }
    void notificateCameraMove(const Point& offset) const;
    void setObserver(MapObserver* observer) { m_observer = observer; }
    /// Diagnostic counter: opaque add/remove / field clean. Cover *caches* are
    /// cleared per-cell via invalidateCoverCachesFrom — not by this revision.
    uint32_t getCoverRevision() const { return m_coverRevision.load(std::memory_order_relaxed); }
    /// Drop isCompletelyCovered / isCovered bits for tiles that look up through `pos`.
    void invalidateCoverCachesFrom(const Position& pos);
    /// One worldmap build: memoize opaque / top-ground probes shared across FOV cover walks.
    void beginCoverProbeBatch();
    void endCoverProbeBatch();
    /// Bumped when an item is added to or removed from a field, or a field is cleaned.
    uint32_t getItemRevision() const { return m_itemRevision.load(std::memory_order_relaxed); }
    /// Creatures, effects, and missiles added or removed. Walk offsets do not bump it.
    uint32_t getDynamicRevision() const { return m_dynamicRevision.load(std::memory_order_relaxed); }
    /// Drop pending static dirty (cover-flood coalescing counter). Full emit remits
    /// the whole FOV, so positions are not consumed by the renderer.
    /// Returns how many were discarded (for PERF).
    size_t clearStaticDirtyTiles();
    void notificateKeyRelease(const InputEvent& inputEvent) const;

#ifdef FRAMEWORK_EDITOR
    bool loadOtcm(const std::string& fileName);
    void saveOtcm(const std::string& fileName);

    void loadOtbm(const std::string& fileName);
    void saveOtbm(const std::string& fileName);

    // otbm attributes (description, size, etc.)
    void setHouseFile(const std::string& file) { m_houseFile = file; }
    void setSpawnFile(const std::string& file) { m_spawnFile = file; }
    void setDescription(const std::string& desc) { m_desc = desc; }

    void clearDescriptions() { m_desc.clear(); }
    std::vector<std::string> getDescriptions() { return stdext::split(m_desc, "\n"); }
    std::string getHouseFile() { return m_houseFile; }
    std::string getSpawnFile() { return m_spawnFile; }

    // tile zone related
    void setShowZone(tileflags_t zone, bool show);
    void setShowZones(bool show);
    void setZoneColor(tileflags_t zone, const Color& color);
    void setZoneOpacity(float opacity) { m_zoneOpacity = opacity; }

    float getZoneOpacity() { return m_zoneOpacity; }
    Color getZoneColor(tileflags_t flag);
    tileflags_t getZoneFlags() { return static_cast<tileflags_t>(m_zoneFlags); }
    bool showZones() { return m_zoneFlags != 0; }
    bool showZone(tileflags_t zone) { return (m_zoneFlags & zone) == zone; }

    void setForceShowAnimations(bool force);
    bool isShowingAnimations() { return (m_animationFlags & Animation_Show) == Animation_Show; }
    bool isForcingAnimations() { return (m_animationFlags & Animation_Force) == Animation_Force; }

    void setShowAnimations(bool show);
#endif

    void setWidth(const uint16_t w) { m_width = w; }
    void setHeight(const uint16_t h) { m_height = h; }
    Size getSize() { return { m_width, m_height }; }

    void clean();
    void cleanDynamicThings();
    void cleanTexts() { m_animatedTexts.clear(); m_staticTexts.clear(); }

    // thing related
    ThingPtr getThing(const Position& pos, int16_t stackPos);
    void addThing(const ThingPtr& thing, const Position& pos, int16_t stackPos = -1);
    bool removeThing(const ThingPtr& thing);
    bool removeThingByPos(const Position& pos, int16_t stackPos);

    void addStaticText(const StaticTextPtr& txt, const Position& pos);
    bool removeStaticText(const StaticTextPtr& txt);

    void addAnimatedText(const AnimatedTextPtr& txt, const Position& pos);
    bool removeAnimatedText(const AnimatedTextPtr& txt);

    bool isWidgetAttached(const UIWidgetPtr& widget) const;
    void addAttachedWidgetToObject(const UIWidgetPtr& widget, const AttachableObjectPtr& object);
    bool removeAttachedWidgetFromObject(const UIWidgetPtr& widget);

    void colorizeThing(const ThingPtr& thing, const Color& color);
    void removeThingColor(const ThingPtr& thing);

    StaticTextPtr getStaticText(const Position& pos) const;

    // tile related
    const TilePtr& createTile(const Position& pos);
    template <typename... Items>
    const TilePtr& createTileEx(const Position& pos, const Items&... items);
    const TilePtr& getOrCreateTile(const Position& pos);
    const TilePtr& getTile(const Position& pos);
    TileList getTiles(int8_t floor = -1);
    void cleanTile(const Position& pos);

    void beginGhostMode(float opacity);
    void endGhostMode();

    stdext::map<Position, ItemPtr, Position::Hasher> findItemsById(uint16_t clientId, uint32_t max);
    std::vector<ItemPtr> findItemsInRange(const Position& center, int rangeX, int rangeY, const std::vector<uint16_t>& clientIds);

    CreaturePtr getCreatureById(uint32_t id);
    void addCreature(const CreaturePtr& creature);
    void removeCreatureById(uint32_t id);

    std::vector<CreaturePtr> getSpectators(const Position& centerPos, const bool multiFloor)
    {
        return getSpectatorsInRangeEx(centerPos, multiFloor, m_awareRange.left, m_awareRange.right, m_awareRange.top, m_awareRange.bottom);
    }

    std::vector<CreaturePtr> getSightSpectators(const Position& centerPos, const bool multiFloor)
    {
        return getSpectatorsInRangeEx(centerPos, multiFloor, m_awareRange.left - 1, m_awareRange.right - 2, m_awareRange.top - 1, m_awareRange.bottom - 2);
    }

    std::vector<CreaturePtr> getSpectatorsInRange(const Position& centerPos, const bool multiFloor, const int32_t xRange, const int32_t yRange)
    {
        return getSpectatorsInRangeEx(centerPos, multiFloor, xRange, xRange, yRange, yRange);
    }

    std::vector<CreaturePtr> getSpectatorsInRangeEx(const Position& centerPos, bool multiFloor, int32_t minXRange, int32_t maxXRange, int32_t minYRange, int32_t maxYRange);

    void setLight(const Light& light);

    void setCentralPosition(const Position& centralPosition);

    bool isLookPossible(const Position& pos);
    bool isCovered(const Position& pos, uint8_t firstFloor = 0) {
        bool isLoading = false;
        return isCovered(pos, isLoading, firstFloor);
    }

    bool isCompletelyCovered(const Position& pos, uint8_t firstFloor = 0) {
        bool isLoading = false;
        return isCompletelyCovered(pos, isLoading, firstFloor);
    }

    bool isCovered(const Position& pos, bool& isLoading, uint8_t firstFloor = 0);
    bool isCompletelyCovered(const Position& pos, bool& isLoading, uint8_t firstFloor = 0);
    /// Wipe every resident tile's cover bits (stream-flood coalesce).
    void invalidateAllCoverCaches();
    bool isAwareOfPosition(const Position& pos) const { return isAwareOfPosition(pos, m_awareRange); }
    bool isAwareOfPosition(const Position& pos, const AwareRange& awareRange) const;

    void resetLastCamera() const;

    void setAwareRange(const AwareRange& range);
    void resetAwareRange();
    AwareRange getAwareRange() const { return m_awareRange; }

    Light getLight() const { return m_light; }
    Position getCentralPosition() { return m_centralPosition; }
    uint8_t getFirstAwareFloor() const;
    uint8_t getLastAwareFloor() const;
    const std::vector<MissilePtr>& getFloorMissiles(const uint8_t z) { return m_floors[z].missiles; }

    std::vector<AnimatedTextPtr> getAnimatedTexts() { return m_animatedTexts; }
    std::vector<StaticTextPtr> getStaticTexts() { return m_staticTexts; }

    std::tuple<std::vector<Otc::Direction>, Otc::PathFindResult> findPath(const Position& start, const Position& goal,
                                                                          int maxComplexity, int flags = 0);
    /// `pathMode`: Minimap (strict Soft), Cyclopedia (Soft 0xfe fog), Travel (fog + soft blockers).
    PathFindResult_ptr newFindPath(const Position& start, const Position& goal,
                                   const std::shared_ptr<std::list<Node*>>& visibleNodes,
                                   const std::shared_ptr<tibia::minimap::TWaypointCosts>& softCosts = nullptr,
                                   Otc::AutoWalkPathMode pathMode = Otc::AutoWalkPathMinimap);
    void findPathAsync(const Position& start, const Position& goal,
                       const std::function<void(PathFindResult_ptr)>& callback,
                       Otc::AutoWalkPathMode pathMode = Otc::AutoWalkPathMinimap);

    /// Soft pathfinder avoid set (CIP minimap avoid): skipped by newFindPath.
    void setPathAvoid(const Position& pos, bool avoid);
    void clearPathAvoids();
    bool isPathAvoided(const Position& pos) const;

    /// Soft Indexed8 waypoint snapshot factory (CIP TWaypointCostMap). Set from
    /// TGameClient while the CipSoft minimap is live; cleared on world leave.
    using WaypointCostFactory = std::function<std::shared_ptr<tibia::minimap::TWaypointCosts>()>;
    void setWaypointCostFactory(WaypointCostFactory factory);
    void clearWaypointCostFactory();

    void setFloatingEffect(const bool enable) { m_floatingEffect = enable; }
    bool isDrawingFloatingEffects() { return m_floatingEffect; }

    std::map<std::string, std::tuple<int, int, int, std::string>> findEveryPath(const Position& start, int maxDistance, const std::map<std::string, std::string>& params);
    std::vector<CreaturePtr> getSpectatorsByPattern(const Position& centerPos, const std::string& pattern, Otc::Direction direction);

    int getMinimapColor(const Position& pos);
    bool isSightClear(const Position& fromPos, const Position& toPos);

    const auto& getCreatures() const { return m_knownCreatures; }

private:
    struct FloorData
    {
        std::vector<MissilePtr> missiles;
        std::unordered_map<uint32_t, TileBlock > tileBlocks;
    };

    void removeUnawareThings();
    /// Packed cover-probe flags for one build (see beginCoverProbeBatch).
    uint8_t probeCoverTile(const Position& pos);

    uint16_t getBlockIndex(const Position& pos) { return ((pos.y / BLOCK_SIZE) * (65536 / BLOCK_SIZE)) + (pos.x / BLOCK_SIZE); }

    std::vector<FloorData> m_floors;

    std::vector<AnimatedTextPtr> m_animatedTexts;
    std::vector<StaticTextPtr> m_staticTexts;

    std::unordered_map<uint32_t, CreaturePtr> m_knownCreatures;

    std::unordered_map<UIWidgetPtr, AttachableObjectPtr> m_attachedObjectWidgetMap;

#ifdef FRAMEWORK_EDITOR
    std::unordered_map<Position, std::string, Position::Hasher> m_waypoints;
    std::unordered_map<uint32_t, Color> m_zoneColors;

    std::string m_houseFile;
    std::string m_spawnFile;
    std::string m_desc;

    uint8_t m_animationFlags{ 0 };
    uint32_t m_zoneFlags{ 0 };

    float m_zoneOpacity{ 1.f };
#endif

    uint16_t m_width{ 0 };
    uint16_t m_height{ 0 };

    Light m_light;
    Position m_centralPosition;

    AwareRange m_awareRange;

    MapObserver* m_observer{ nullptr };
    bool m_removingUnawareThings{ false };
    std::atomic<uint32_t> m_coverRevision{ 0 };
    std::atomic<uint32_t> m_itemRevision{ 0 };
    std::atomic<uint32_t> m_dynamicRevision{ 0 };
    std::vector<Position> m_staticDirty;
    static constexpr size_t MaxStaticDirty = 4096;
    /// Opaque stream hit MaxStaticDirty — defer one full cover wipe instead of N column walks.
    bool m_coverInvalidateFlood{ false };

    /// Frame-local memo: seen|empty|opaque|topGround|loading (see map.cpp).
    bool m_coverProbeActive{ false };
    std::unordered_map<Position, uint8_t, Position::Hasher> m_coverProbe;

    bool m_floatingEffect{ true };

    /// Soft avoid tiles for newFindPath (CIP waypoint/avoid pathfinder subset).
    std::unordered_set<Position, Position::Hasher> m_pathAvoids;
    WaypointCostFactory m_waypointCostFactory;
};

extern Map g_map;
