#pragma once

#include "renderer/weatherframe.h"
#include "renderer/lightmodel.h"
#include "mods/worldview.h"
#include "renderer/spritetexturecache.h"
#include "renderer/worldgeometry.h"

#include <client/declarations.h>
#include <client/position.h>
#include <framework/util/point.h>

#include <QImage>
#include <QObject>
#include <QPoint>

#include <array>
#include <cstdint>
#include <vector>

namespace tibia::qmlcomponents {

/// One sprite of the world, in draw order (CIP: the provider's 0x30-byte records).
struct SpriteRecord
{
    /// One past the sprite's bottom-right pixel: things are anchored at their
    /// bottom-right corner, so the sprite's size is not needed to place it.
    QPoint anchor;
    SpriteKey sprite;
    float opacity = 1.f;
    /// Highlight drawn over the sprite (CIP: the record's byte at +0x15).
    enum Highlight : uint8_t { NoHighlight, MouseTarget, AttackTarget, FollowTarget };
    Highlight highlight = NoHighlight;
};

/// Turns the game model into the world's frame (CIP: TWorldMapRenderProvider):
/// the sprite list, the light map and the creatures' HUD anchors. Built on the
/// GUI thread once per frame; the WorldMap reads it during the scene graph sync.
class TWorldMapRenderProvider : public QObject
{
    Q_OBJECT
public:
    /// A creature whose HUD (name, bars) is shown (CIP: TCreatureHUDRenderProvider's records).
    struct CreatureHud
    {
        CreaturePtr creature;
        /// Map pixel of the creature's top-left, walk and elevation included; the
        /// HUD stands on it, centred one field to the right.
        QPoint point;
    };

    struct Frame
    {
        /// False while there is no world to show; everything else is then empty.
        bool valid = false;
        std::vector<SpriteRecord> sprites;
        std::vector<CreatureHud> creatures;
        /// One pixel per walked field (CIP: TLightMap), multiplied over the world.
        /// Pixel (0, 0) is the field at map pixel (0, 0).
        QImage light;
        /// Map pixel shown at the WorldMap's top-left.
        QPoint camera;
        /// How strongly highlighted sprites take the highlight colour; pulses
        /// with the game clock.
        float highlightStrength = 0.f;
        /// Visible FOV in fields (OTUI MapViewport / setup visible-sqm).
        int visibleWidth = 19;
        int visibleHeight = 11;
        /// CIP map scale (snapped height / 352). Overlays place with this, centered.
        double viewScale = 1;
        /// Shared roof / outdoor gather (weather + lighting). Empty when unused.
        WorldGeometry geometry;
    };

    /// Fallback FOV before the map pane reports a size (CIP classic was 15×11).
    static constexpr int DefaultVisibleWidth = 19;
    static constexpr int DefaultVisibleHeight = 11;
    static constexpr int FieldSize = 32;

    int visibleWidth() const { return m_visibleWidth; }
    int visibleHeight() const { return m_visibleHeight; }
    int drawWidth() const { return m_visibleWidth + 3; }
    int drawHeight() const { return m_visibleHeight + 3; }
    int centerX() const { return drawWidth() / 2 - 1; }
    int centerY() const { return drawHeight() / 2 - 1; }

    /// product: short-axis zoom from setup.otml visible-sqm; long axis follows pane aspect
    /// (same as OTUI MapViewport) so cover-scale never crops below that short axis.
    void setVisibleFields(int width, int height);
    void setViewScale(double scale);

    const Frame& frame() const { return m_frame; }
    /// The player's field of the last frame.
    const Position& camera() const { return m_camera; }
    /// The field at the top-left of the walked area of floor `z`.
    Position firstField(int z) const;

    /// Light at a map pixel, 0 (dark) to 1, bilinear between field centres
    /// (CIP: TLightMap::v7).
    float lightnessAt(const QPoint& mapPixel) const;
    /// QColor::lightness of a field's light, 0 outside the light map and 255
    /// without one (CIP: TWorldMapRenderProvider::v14, TLightMap 0x10107ad30).
    int fieldLightness(const Position& field) const;

    /// Floors of the last frame, top-down: the picker looks through them in
    /// this order.
    uint8_t firstFloor() const { return m_firstFloor; }
    uint8_t lastFloor() const { return m_lastFloor; }
    /// The field of floor `z` drawn at a map pixel of the last frame.
    Position fieldAt(const QPoint& mapPixel, int z) const;
    /// Map pixel of a field's top-left corner.
    QPoint mapPixelOf(const Position& field) const;

    /// The thing under the mouse, highlighted unless it is a creature (CIP: v17).
    void setMouseTarget(const ThingPtr& thing) { m_mouseTarget = thing; }
    const ThingPtr& mouseTarget() const { return m_mouseTarget; }
    /// Field under the cursor for the highlight baseplate (CIP: v21 / +0xe8).
    void setMouseHighlightField(const Position& field) { m_mouseHighlightField = field; }
    const Position& mouseHighlightField() const { return m_mouseHighlightField; }
    /// Option mouseHighlightTarget: highlight the mouse target; without it a
    /// creature under the mouse gets a white mark instead (CIP: DAT_1077b2288).
    void setMouseHighlightEnabled(bool enabled) { m_mouseHighlight = enabled; }
    /// Option gameWindowShowAttackAnimation: highlight the attack and follow targets.
    void setTargetHighlightEnabled(bool enabled) { m_targetHighlight = enabled; }
    /// What the last advance() took (full build including light/raycast).
    qint64 buildNs() const { return m_buildNs; }
    /// Phase costs from the last build, for QTCLIENT_PERF / QTCLIENT_PERF.
    qint64 tilesNs() const { return m_tilesNs; }
    qint64 lightNs() const { return m_lightNs; }
    /// beginFrame / beginFloor / endFrame setup (excludes gather inside the tile walk).
    qint64 raycastNs() const { return m_raycastNs; } // pack lighting phase
    /// addFrame / resetShade during the tile walk.
    qint64 raycastGatherNs() const { return m_raycastGatherNs; }
    /// updateRoofs inside endFrame.
    qint64 raycastRoofsNs() const { return m_raycastRoofsNs; }
    /// Shared roof / outdoor geometry for the last frame (weather + lighting).
    const WorldGeometry& geometry() const { return m_frame.geometry; }
    /// Weather overlay inputs for the WorldMap node (empty when weather is off).
    WeatherFrame weatherFrame() const;

    explicit TWorldMapRenderProvider(QObject* parent = nullptr);

public Q_SLOTS:
    /// Builds the next frame from the game model; emits frameAdvanced.
    void advance();

Q_SIGNALS:
    void frameAdvanced();
    /// Sprite list and camera pixel are unchanged. Highlight nodes still pulse.
    void highlightAdvanced();
    /// HUD, speech, and light overlays.
    void overlaysAdvanced();
    void visibleFieldsChanged();

private:
    enum Order { Ground, GroundBorder, Common, OnTop, OrderCount };

    /// Which creature mark frames show (CIP: FUN_1009095e0 copies the options
    /// into DAT_102734bd8..bda).
    struct MarkOptions
    {
        /// Option 0x4b gameWindowShowTragetFrame: attack and follow target frames.
        bool target = true;
        /// Option 0x49 combatShowPvpFrames: the server's permanent marks.
        bool pvp = true;
        /// Option 0x3f combatShowFrames: the server's timed marks.
        bool timed = true;
    };

    bool build();
    void buildLight();
    /// Shared outdoor / roof gather when weather or raycast needs it.
    void updateWorldGeometry();
    /// Cool outdoor cells from active weather (D).
    void applyWeatherAmbientTint();
    void loadLightOptions();
    void loadMarkOptions();
    /// Frames around a creature, outer to inner (CIP: TCreature v0x258 ->
    /// FUN_100a45910); all 0 when it has none.
    std::array<uint32_t, 3> creatureMarks(Creature& creature) const;
    uint8_t firstVisibleFloor() const;
    uint8_t lastVisibleFloor() const;
    Point toScreen(const Position& pos) const;
    /// The field drawn at light map cell (x, y) of floor z.
    Position lightField(int x, int y, int z) const;

    /// Draws a field, then the creatures and top objects deferred to it.
    void addTile(const TilePtr& tile, const Point& dest);
    void flushLyingDeferred();
    void addTileBelow(const TilePtr& tile, const Point& dest, uint8_t& elevation);
    void addTileCreatures(const TilePtr& tile, const Point& dest, uint8_t elevation);
    void addTileEffects(const TilePtr& tile, const Point& dest);
    void addTileTop(const TilePtr& tile, const Point& dest);
    /// The field with a lying object whose draw slot `tile`'s creatures and top
    /// objects take (CIP: FUN_101057d80), or null.
    const Tile* lyingObjectField(const Tile& tile) const;
    void addThing(const ThingPtr& thing, const Point& dest, uint8_t& elevation);
    void addItem(Order order, Item& item, const Point& dest);
    /// Draw the show-off socket (podium) creature/outfit on top of the socket item.
    void addPodiumOutfit(Order order, Item& item, const Point& dest, SpriteRecord::Highlight highlight);
    void addCreature(Order order, Creature& creature, const Point& dest);
    void addEffect(Order order, Effect& effect, const Point& dest);
    void addMissile(Missile& missile);
    /// The opacity option of an effect's source, 1 for sourceless ones (CIP:
    /// 0x100aca180, read when the effect arrives).
    static float effectOpacity(Otc::MagicEffectSources source);
    /// Effects below this opacity are not shown at all (CIP: 0x100aca160).
    static constexpr float MinEffectOpacity = 0.01f;
    void addSprites(Order order, ThingType* type, const Point& dest, int xPattern, int yPattern, int zPattern,
                    int animationPhase, bool colorize = false, uint32_t colors = 0,
                    SpriteRecord::Highlight highlight = SpriteRecord::NoHighlight, float opacity = 1.f, uint8_t tint = 0);
    /// The creature's attached effects on one side of its outfit (product
    /// Creature::internalDraw): at its walked field, or where it is drawn
    /// (`jumped`) for those following it.
    void addAttachedEffects(Order order, Creature& creature, const Point& walked, const Point& jumped, bool onTop);
    /// Paperdoll layers beside the outfit (they used to be drawn only from Creature::draw).
    void addPaperdolls(Order order, Creature& creature, const Point& at, bool mount, bool onTop, SpriteRecord::Highlight highlight);
    SpriteRecord::Highlight highlightOf(Creature& creature) const;
    void addLight(const Point& center, uint8_t intensity, uint8_t color);
    /// CIP TWorldMapRenderProvider::v21: animated field square under the mouse.
    void updateHighlightBaseplate();

    Frame m_frame;
    Position m_camera;
    int m_visibleWidth = DefaultVisibleWidth;
    int m_visibleHeight = DefaultVisibleHeight;
    double m_viewScale = 1;
    int m_floor = 0;
    uint8_t m_firstFloor = 0;
    uint8_t m_lastFloor = 0;
    ThingPtr m_mouseTarget;
    Position m_mouseHighlightField;
    bool m_mouseHighlight = true;
    bool m_targetHighlight = true;
    MarkOptions m_markOptions;
    uint32_t m_attackTarget = 0;
    uint32_t m_followTarget = 0;
    std::array<std::vector<SpriteRecord>, OrderCount> m_orders;
    /// Tile + draw-grid screen dest (ix/iy × FieldSize) for walk-creature deferral.
    struct Pending
    {
        TilePtr tile;
        Point dest;
    };
    std::vector<Pending> m_pending;
    /// A field drawn in the slot of the lying object down-right of it.
    struct Deferred
    {
        const Tile* lyingField;
        TilePtr tile;
        Point dest;
        uint8_t elevation;
    };
    std::vector<Deferred> m_lyingDeferred;
    /// Fields with a lying object already drawn on the current floor.
    std::vector<const Tile*> m_drawnLying;
    std::vector<light::PointLight> m_lights;
    light::LightOptions m_lightOptions;
    uint32_t m_coverRevision = 0;
    qint64 m_buildNs = 0;
    qint64 m_tilesNs = 0;
    qint64 m_lightNs = 0;
    qint64 m_raycastNs = 0;
    qint64 m_raycastGatherNs = 0;
    qint64 m_raycastRoofsNs = 0;
    /// Why this build ran (for slow-tile / ~1 Hz dumps).
    bool m_cameraTileMoved = false;
    bool m_coverDirty = false;
    /// Cached first/last floor while camera tile and cover revision hold.
    uint8_t m_cachedFirstFloor = 0;
    uint8_t m_cachedLastFloor = 0;
    bool m_floorsCached = false;
    /// Classic light before weather tint (D); tint reapplies each frame from this.
    QImage m_lightUntinted;
    /// Cache key for shared world geometry.
    Position m_geometryCamera;
    uint32_t m_geometryRevision = 0;
    int m_geometryWidth = 0;
    int m_geometryHeight = 0;
    uint8_t m_geometryFirst = 0;
    uint8_t m_geometryLast = 0;
    int m_geometryOrigin = -1;
    /// Cached at advance() start — null when no pack registered a driver.
    tibia::mods::WorldLightingDriver* m_lighting = nullptr;
};

}
