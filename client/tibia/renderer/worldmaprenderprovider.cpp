#include "worldmaprenderprovider.h"
#include "tibia/runtime/host.h"

#include "appearances/appearanceinstance.h"
#include "appearances/spriteinfo.h"
#include "appearances/worldmaphighlightbaseplate.h"
#include "app/clientoptions.h"
#include "mods/registry.h"
#include "mods/weather_driver.h"
#include "mods/worldview.h"
#include "sprites/sprite.h"
#include "renderer/lightmodel.h"
#include "renderer/rendervolumeprovider.h"
#include "renderer/worldgeometry.h"
#include "tibia/world/world.h"
#include "worldmap/lightmap.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QLoggingCategory>

#include <client/animator.h>
#include <tibia/world/attachedeffect.h>
#include <client/client.h>
#include <tibia/world/creature.h>
#include <tibia/world/effect.h>
#include <tibia/world/game.h>
#include <client/gameconfig.h>
#include <client/item.h>
#include <tibia/world/localplayer.h>
#include <tibia/world/map.h>
#include <client/mapviewport.h>
#include <tibia/world/missile.h>
#include <tibia/assets/thingtype.h>
#include <tibia/assets/things.h>
#include <tibia/world/tile.h>
#include <framework/core/clock.h>
#include <framework/core/graphicalapplication.h>

#include <QColor>

#include <algorithm>
#include <array>
#include <optional>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <vector>

namespace tibia::qmlcomponents {

Q_LOGGING_CATEGORY(lcWorldMapBuild, "tibia.worldmap.build", QtWarningMsg)
Q_LOGGING_CATEGORY(lcWeatherFrame, "tibia.weather.frame", QtWarningMsg)
Q_LOGGING_CATEGORY(lcWeatherPerf, "tibia.weather.perf", QtWarningMsg)

namespace {

constexpr int FieldSize = TWorldMapRenderProvider::FieldSize;

uint32_t outfitColors(const Outfit& outfit)
{
    return outfit.getHead() | outfit.getBody() << 8 | outfit.getLegs() << 16 | static_cast<uint32_t>(outfit.getFeet()) << 24;
}

/// Thing subclasses hide getThingType(); Thing exposes it.
ThingType* typeOf(const Thing& thing)
{
    return thing.getThingType();
}

bool walksDiagonallyAcross(Creature& creature)
{
    const auto dir = creature.getDirection();
    return dir == Otc::NorthEast || dir == Otc::SouthWest;
}

bool hasLyingObject(Tile& tile)
{
    for (const ThingPtr& thing : tile.getThings()) {
        if (thing->isItem() && thing->isLyingCorpse())
            return true;
    }
    return false;
}

}

TWorldMapRenderProvider::TWorldMapRenderProvider(QObject* parent)
    : QObject(parent)
{
    loadLightOptions();
    loadMarkOptions();
    connect(app::ClientOptionsNotifier::instance(), &app::ClientOptionsNotifier::optionChanged, this, [this](const QString& name) {
        if (name.startsWith(QLatin1String("light")))
            loadLightOptions();
        else if (name.startsWith(QLatin1String("combatShow")) || name == QLatin1String("gameWindowShowTragetFrame"))
            loadMarkOptions();
    });
}

void TWorldMapRenderProvider::setVisibleFields(const int width, const int height)
{
    const int w = std::max(3, width);
    const int h = std::max(3, height);
    if (w == m_visibleWidth && h == m_visibleHeight)
        return;
    m_visibleWidth = w;
    m_visibleHeight = h;
    Q_EMIT visibleFieldsChanged();
}

void TWorldMapRenderProvider::setViewScale(const double scale)
{
    m_viewScale = scale > 0 ? scale : 1;
}

void TWorldMapRenderProvider::loadMarkOptions()
{
    const QJsonObject options = app::ClientOptions::load().options();
    m_markOptions.target = options.value(QLatin1String("gameWindowShowTragetFrame")).toBool(true);
    m_markOptions.pvp = options.value(QLatin1String("combatShowPvpFrames")).toBool(true);
    m_markOptions.timed = options.value(QLatin1String("combatShowFrames")).toBool(true);
}

/// The target frame is red for the attack target and green for the follow
/// target, lighter under the mouse, white for any other creature under the
/// mouse (FUN_100a42110/42270/42420, colors from FUN_100a45df0). Then the
/// server's permanent mark (vt 0x230) and timed mark (vt 0x240).
std::array<uint32_t, 3> TWorldMapRenderProvider::creatureMarks(Creature& creature) const
{
    std::array<uint32_t, 3> marks{};
    size_t count = 0;
    if (m_markOptions.target) {
        const uint32_t id = creature.getId();
        const bool hovered = m_mouseTarget.get() == &creature;
        uint32_t target = 0;
        if (id == m_attackTarget)
            target = hovered ? 0xFFFF8080 : 0xFFFF0000;
        else if (id == m_followTarget)
            target = hovered ? 0xFF80FF80 : 0xFF00FF00;
        else if (hovered && !m_mouseHighlight)
            target = 0xFFFFFFFF;
        if (target)
            marks[count++] = target;
    }
    const auto argb = [](const Color& color) { return 0xFF000000u | color.r() << 16 | color.g() << 8 | color.b(); };
    if (m_markOptions.pvp && creature.isStaticSquareVisible())
        marks[count++] = argb(creature.getStaticSquareColor());
    if (m_markOptions.timed && creature.isTimedSquareVisible())
        marks[count++] = argb(creature.getTimedSquareColor());
    return marks;
}

void TWorldMapRenderProvider::loadLightOptions()
{
    m_lightOptions = light::loadOptions();
}

void TWorldMapRenderProvider::advance()
{
    m_lighting = tibia::mods::worldLightingDriver();

    QElapsedTimer cost;
    cost.start();
    m_frame.valid = build();
    m_buildNs = cost.nsecsElapsed();
    if (m_frame.valid)
        updateHighlightBaseplate();
    if (!m_frame.valid) {
        m_frame.sprites.clear();
        m_frame.creatures.clear();
    }
    if (m_tilesNs > 5'000'000) {
        // Always visible under QTCLIENT_PERF (category default is Warning).
        // tilesUs excludes gather; sum tiles+gather ≈ tile-walk wall time.
        qCWarning(lcWorldMapBuild) << "slow tiles us" << m_tilesNs / 1000
                                   << "floors" << int(m_firstFloor) << ".." << int(m_lastFloor)
                                   << "sprites" << m_frame.sprites.size()
                                   << "gather us" << m_raycastGatherNs / 1000 << "light us" << m_lightNs / 1000
                                   << "roofs us" << m_raycastRoofsNs / 1000 << "raycast us" << m_raycastNs / 1000
                                   << "cameraMoved" << m_cameraTileMoved << "coverDirty" << m_coverDirty;
    } else if (lcWorldMapBuild().isDebugEnabled()) {
        static QElapsedTimer logged;
        if (!logged.isValid() || logged.elapsed() > 1000) {
            logged.start();
            qCDebug(lcWorldMapBuild) << "build us" << m_buildNs / 1000 << "tiles us" << m_tilesNs / 1000
                                     << "gather us" << m_raycastGatherNs / 1000 << "light us" << m_lightNs / 1000
                                     << "roofs us" << m_raycastRoofsNs / 1000 << "raycast us" << m_raycastNs / 1000
                                     << "sprites" << m_frame.sprites.size()
                                     << "cameraMoved" << m_cameraTileMoved << "coverDirty" << m_coverDirty;
        }
    }
    // CIP rebuilds the sprite list every frame; QSG reconciles in place.
    if (m_frame.valid) {
        Q_EMIT frameAdvanced();
        Q_EMIT overlaysAdvanced();
    }
}

bool TWorldMapRenderProvider::build()
{
    m_lighting = tibia::mods::worldLightingDriver();

    Frame& frame = m_frame;
    const int DrawWidth = drawWidth();
    const int DrawHeight = drawHeight();
    const int CenterX = centerX();
    const int CenterY = centerY();
    frame.visibleWidth = m_visibleWidth;
    frame.visibleHeight = m_visibleHeight;
    frame.viewScale = m_viewScale;
    const size_t previousSprites = frame.sprites.size();
    const size_t previousCreatures = frame.creatures.size();
    m_tilesNs = m_lightNs = m_raycastNs = m_raycastGatherNs = m_raycastRoofsNs = 0;
    m_cameraTileMoved = false;
    m_coverDirty = false;
    const LocalPlayerPtr player = tibia::world::player();
    if (!player || !g_game.isOnline() || !player->getPosition().isValid()) {
        frame.sprites.clear();
        frame.creatures.clear();
        m_lights.clear();
        return false;
    }

    // Cover caches invalidate per opaque cell (Map::invalidateCoverCachesFrom).
    const uint32_t coverRevision = tibia::world::fields().getCoverRevision();
    const Position camera = player->getPosition();
    const bool cameraTileMoved = m_camera != camera;
    const bool coverDirty = coverRevision != m_coverRevision;
    m_cameraTileMoved = cameraTileMoved;
    m_coverDirty = coverDirty;
    m_coverRevision = coverRevision;
    m_camera = camera;
    const Point walk = tibia::world::motion().offset(*player);
    frame.camera = QPoint((CenterX - (m_visibleWidth - 1) / 2) * FieldSize + walk.x,
                          (CenterY - (m_visibleHeight - 1) / 2) * FieldSize + walk.y);

    // firstVisibleFloor on camera step or opaque cover change (FUN_101060450).
    // No hysteresis — delayed hide left upper floors stuck on.
    uint8_t first;
    uint8_t last;
    if (!m_floorsCached || cameraTileMoved || coverDirty) {
        first = firstVisibleFloor();
        last = std::max(first, lastVisibleFloor());
        m_cachedFirstFloor = first;
        m_cachedLastFloor = last;
        m_floorsCached = true;
    } else {
        first = m_cachedFirstFloor;
        last = m_cachedLastFloor;
    }
    m_firstFloor = first;
    m_lastFloor = last;

    frame.highlightStrength = static_cast<float>((std::sin(g_clock.millis() / 100.0) * 0.15 + 0.65) * 0.75);
    const CreaturePtr& attacked = g_game.getAttackingCreature();
    const CreaturePtr& followed = g_game.getFollowingCreature();
    m_attackTarget = attacked ? attacked->getId() : 0;
    m_followTarget = followed ? followed->getId() : 0;

    QElapsedTimer phase;
    phase.start();
    // CIP every-frame full emit (classic and raycast). Drain map dirty for cover-flood
    // coalescing; positions are unused because the whole FOV is remitted.
    tibia::world::fields().clearStaticDirtyTiles();

    frame.sprites.clear();
    frame.creatures.clear();
    frame.sprites.reserve(std::max(previousSprites, static_cast<size_t>(DrawWidth * DrawHeight)));
    if (previousCreatures)
        frame.creatures.reserve(previousCreatures);
    m_lights.clear();

    const bool gatherRaycast = (m_lighting && m_lighting->enabled())
        && m_lighting->beginFrame(m_camera, DrawWidth, DrawHeight, first, last);

    // Share opaque/top-ground probes across FOV cover walks (cold-cache cliffs).
    tibia::world::fields().beginCoverProbeBatch();
    for (int z = last; z >= first; --z) {
        m_floor = z;
        {
            QElapsedTimer rayFloor;
            rayFloor.start();
            if (m_lighting) m_lighting->beginFloor(z);
            m_raycastNs += rayFloor.nsecsElapsed();
        }
        for (auto& order : m_orders) {
            const size_t kept = order.capacity();
            order.clear();
            if (kept)
                order.reserve(kept);
        }
        m_drawnLying.clear();

        // CIP volume masks keep the visible window plus a two-field skirt
        // (draw size is visible + 3). Hoisted out of the per-cell loop.
        const int left = CenterX - (m_visibleWidth - 1) / 2;
        const int top = CenterY - (m_visibleHeight - 1) / 2;
        const int skirtLeft = left - 2;
        const int skirtTop = top - 2;
        const int skirtRight = left + m_visibleWidth + 2;
        const int skirtBottom = top + m_visibleHeight + 2;
        const int diagLimit = DrawWidth + DrawHeight - 1;

        for (int diagonal = 0; diagonal < diagLimit; ++diagonal) {
            const int advance = diagonal >= DrawHeight ? diagonal - DrawHeight : 0;
            for (int iy = diagonal - advance, ix = advance; iy >= 0 && ix < DrawWidth; --iy, ++ix) {
                if (ix < skirtLeft || iy < skirtTop || ix >= skirtRight || iy >= skirtBottom)
                    continue;
                Position pos = m_camera.translated(ix - CenterX, iy - CenterY);
                pos.coveredUp(m_camera.z - z);
                const TilePtr& tile = tibia::world::fields().getTile(pos);
                // Cover cache keys off Map cover revision — do not pass coverDirty
                // (that reset every cell every opaque stream frame).
                if (!tile || !tile->isDrawable() || tile->isCompletelyCovered(first, false))
                    continue;
                // Shade grid only for tiles that will draw (skip covered / empty).
                if (gatherRaycast)
                    if (m_lighting) m_lighting->resetShade(*tile, ix, iy);

                // Draw-grid cell == toScreen after coveredUp (ix/iy × FieldSize).
                const Point dest{ ix * FieldSize, iy * FieldSize };
                // A field is drawn after the one up-right of it while a creature walks
                // there, so creatures walking down-left stay above it.
                m_pending.push_back({ tile, dest });
                const TilePtr& upRight = tibia::world::fields().getTile(pos.translated(1, -1));
                if (upRight && upRight->hasWalkingCreature())
                    continue;
                for (auto it = m_pending.rbegin(); it != m_pending.rend(); ++it)
                    addTile(it->tile, it->dest);
                m_pending.clear();
            }
        }
        for (auto it = m_pending.rbegin(); it != m_pending.rend(); ++it)
            addTile(it->tile, it->dest);
        m_pending.clear();
        flushLyingDeferred();

        for (const MissilePtr& missile : tibia::world::fields().getFloorMissiles(z))
            addMissile(*missile);

        for (auto& order : m_orders) {
            frame.sprites.insert(frame.sprites.end(), std::make_move_iterator(order.begin()),
                                 std::make_move_iterator(order.end()));
            order.clear();
        }
    }
    tibia::world::fields().endCoverProbeBatch();
    const qint64 walkNs = phase.nsecsElapsed();
    m_raycastGatherNs = m_lighting ? m_lighting->gatherNs() : 0;
    m_tilesNs = std::max<qint64>(0, walkNs - m_raycastGatherNs);

    phase.restart();
    buildLight();
    m_lightNs = phase.nsecsElapsed();

    phase.restart();
    if (m_lighting) m_lighting->endFrame(m_frame.geometry.valid() ? &m_frame.geometry : nullptr);
    m_raycastRoofsNs = m_lighting ? m_lighting->roofsNs() : 0;
    m_raycastNs += phase.nsecsElapsed();
    return true;
}

WeatherFrame TWorldMapRenderProvider::weatherFrame() const
{
    QElapsedTimer cost;
    cost.start();
    WeatherFrame out;
    auto* weather = tibia::mods::weatherDriver();
    if (!weather || !weather->enabled() || !m_frame.valid)
        return out;
    const auto state = weather->display();
    if (!state.hasAtmosphere && weather->lightningFlash() < 0.001f)
        return out;
    if (!m_frame.geometry.valid())
        return out;
    if (m_camera.z > g_gameConfig.getMapSeaFloor())
        return out;
    out.active = true;
    out.kind = state.kind;
    out.intensity = state.intensity;
    out.fog = state.fog;
    out.clouds = state.clouds;
    out.windX = state.windX;
    out.windY = state.windY;
    out.time = static_cast<float>(g_clock.millis()) * 0.001f;
    out.lightning = weather->lightningFlash();
    // Midday isometric SE fall (map px). Kept in stock so weather overlays work
    // without linking the IdleOT raycast pack.
    out.fallX = 0.70710678f;
    out.fallY = 0.70710678f;
    out.outdoorFraction = m_frame.geometry.outdoorFraction();
    out.outdoorMask = m_frame.geometry.outdoorMask;
    out.ceilingMap = m_frame.geometry.ceilingMap;
    out.shapeMap = m_frame.geometry.shapeMap;
    out.mapSize = QSize(drawWidth() * FieldSize, drawHeight() * FieldSize);
    const Position originField = m_camera.translated(-centerX(), -centerY());
    out.worldOrigin = QPointF(double(originField.x) * FieldSize, double(originField.y) * FieldSize);
    const qint64 frameUs = cost.nsecsElapsed();

    // Throttled geometry + cost feedback (~1 Hz).
    if (lcWeatherFrame().isDebugEnabled() || lcWeatherPerf().isDebugEnabled()) {
        static qint64 s_lastLogMs = 0;
        static qint64 s_frameUsSum = 0;
        static int s_frameSamples = 0;
        s_frameUsSum += frameUs;
        ++s_frameSamples;
        const qint64 now = g_clock.millis();
        if (now - s_lastLogMs >= 1000) {
            s_lastLogMs = now;
            const qint64 avgFrameUs = s_frameSamples > 0 ? s_frameUsSum / s_frameSamples : 0;
            s_frameUsSum = 0;
            s_frameSamples = 0;
            if (lcWeatherPerf().isDebugEnabled()) {
                qCDebug(lcWeatherPerf).nospace()
                    << "weatherFrame avg us=" << avgFrameUs / 1000
                    << " mask=" << out.outdoorMask.width() << "x" << out.outdoorMask.height()
                    << " mapPx=" << out.mapSize.width() << "x" << out.mapSize.height()
                    << " precip=" << out.kind << "/" << out.intensity
                    << " fog=" << out.fog << " clouds=" << out.clouds;
            }
            if (lcWeatherFrame().isDebugEnabled()) {
                int outdoor = 0;
                int impact = 0;
                int maxH = 0;
                qint64 sumH = 0;
                // ThingShape histogram on impact cells (0=none … 6=Ridge).
                int shapeCount[7] = {};
                int shapeImpact = 0;
                const QImage& mask = out.outdoorMask;
                const QImage& ceil = out.ceilingMap;
                const QImage& shapes = out.shapeMap;
                const bool haveShapes = !shapes.isNull() && shapes.size() == mask.size();
                if (!mask.isNull() && !ceil.isNull() && mask.size() == ceil.size()) {
                    for (int y = 0; y < mask.height(); ++y) {
                        const uchar* o = mask.constScanLine(y);
                        const uchar* c = ceil.constScanLine(y);
                        const uchar* s = haveShapes ? shapes.constScanLine(y) : nullptr;
                        for (int x = 0; x < mask.width(); ++x) {
                            if (o[x] < 128)
                                continue;
                            ++outdoor;
                            const int h = int(c[x]);
                            if (h > 3) {
                                ++impact;
                                sumH += h;
                                maxH = std::max(maxH, h);
                                if (s) {
                                    const int sid = int(s[x]);
                                    if (sid >= 0 && sid <= 6) {
                                        ++shapeCount[sid];
                                        ++shapeImpact;
                                    }
                                }
                            }
                        }
                    }
                }
                const float avgH = impact > 0 ? float(sumH) / float(impact) : 0.f;
                qCDebug(lcWeatherFrame).nospace()
                    << "frame precip=" << out.kind << "/" << out.intensity
                    << " fog=" << out.fog << " clouds=" << out.clouds
                    << " wind=(" << out.windX << "," << out.windY << ")"
                    << " fall=(" << out.fallX << "," << out.fallY << ")"
                    << " outdoorFrac=" << out.outdoorFraction
                    << " outdoorCells=" << outdoor
                    << " impactCells=" << impact << " (h>3, splash/collide)"
                    << " maxH=" << maxH << " avgH=" << avgH
                    << " mapPx=" << out.mapSize.width() << "x" << out.mapSize.height()
                    << " origin=(" << out.worldOrigin.x() << "," << out.worldOrigin.y() << ")"
                    << " lightning=" << out.lightning
                    << " t=" << out.time;
                qCDebug(lcWeatherFrame).nospace()
                    << "shapes impact=" << shapeImpact
                    << " map=" << (haveShapes ? "yes" : "NO")
                    << " none=" << shapeCount[0]
                    << " flat=" << shapeCount[1]
                    << " wall=" << shapeCount[2]
                    << " box=" << shapeCount[3]
                    << " round=" << shapeCount[4]
                    << " window=" << shapeCount[5]
                    << " ridge=" << shapeCount[6]
                    << " (splash: wall=drip box/ridge=ring round=ripple+beads flat/none=puddle)";
            }
        }
    }
    return out;
}

void TWorldMapRenderProvider::updateWorldGeometry()
{
    bool needGeo = (m_lighting && m_lighting->enabled())
        || tibia::mods::worldFrameHooks().requestOutdoorGather;
    if (auto* weather = tibia::mods::weatherDriver()) {
        weather->tick();
        const auto geoState = weather->display();
        needGeo = needGeo
            || (weather->enabled() && (geoState.hasAtmosphere || weather->lightningFlash() > 0.001f));
    }
    if (!needGeo) {
        m_frame.geometry = {};
    } else {
        const uint32_t revision = tibia::world::fields().getItemRevision();
        const int sea = g_gameConfig.getMapSeaFloor();
        const int origin = m_camera.z > sea ? m_camera.z : static_cast<int>(m_lastFloor);
        const bool geoFresh = m_frame.geometry.valid() && m_geometryCamera == m_camera
            && m_geometryRevision == revision && m_geometryWidth == drawWidth()
            && m_geometryHeight == drawHeight() && m_geometryFirst == m_firstFloor
            && m_geometryLast == m_lastFloor && m_geometryOrigin == origin;
        if (!geoFresh) {
            QElapsedTimer geoCost;
            geoCost.start();
            m_frame.geometry = buildWorldGeometry({
                .camera = m_camera,
                .drawWidth = drawWidth(),
                .drawHeight = drawHeight(),
                .centerX = centerX(),
                .centerY = centerY(),
                .firstFloor = m_firstFloor,
                .lastFloor = m_lastFloor,
                .heightOriginZ = origin,
            });
            const qint64 geoNs = geoCost.nsecsElapsed();
            m_geometryCamera = m_camera;
            m_geometryRevision = revision;
            m_geometryWidth = drawWidth();
            m_geometryHeight = drawHeight();
            m_geometryFirst = m_firstFloor;
            m_geometryLast = m_lastFloor;
            m_geometryOrigin = origin;
            if (geoNs > 3'000'000) {
                qCWarning(lcWeatherPerf) << "slow worldGeometry us" << geoNs / 1000
                                         << "size" << m_frame.geometry.width << "x" << m_frame.geometry.height
                                         << "floors" << m_firstFloor << ".." << m_lastFloor;
            } else if (lcWeatherPerf().isDebugEnabled()) {
                static QElapsedTimer s_geoLog;
                static qint64 s_geoSum = 0;
                static int s_geoCount = 0;
                s_geoSum += geoNs;
                ++s_geoCount;
                if (!s_geoLog.isValid() || s_geoLog.elapsed() > 1000) {
                    s_geoLog.start();
                    qCDebug(lcWeatherPerf) << "worldGeometry rebuilds" << s_geoCount
                                           << "avg us" << (s_geoCount > 0 ? s_geoSum / s_geoCount / 1000 : 0)
                                           << "last us" << geoNs / 1000
                                           << "size" << m_frame.geometry.width << "x" << m_frame.geometry.height;
                    s_geoSum = 0;
                    s_geoCount = 0;
                }
            }
        }
    }

    if (auto* weather = tibia::mods::weatherDriver()) {
        const auto state = weather->display();
        const bool rainingOnPlayer =
            !m_frame.geometry.valid() || m_frame.geometry.outdoorAt(centerX(), centerY());
        const bool aboveSea = m_camera.z <= g_gameConfig.getMapSeaFloor();
        weather->updateSound(state, weather->lightningFlash(), weather->enabled(), rainingOnPlayer, aboveSea);
    }
}

void TWorldMapRenderProvider::applyWeatherAmbientTint()
{
    if (m_lightUntinted.isNull()) {
        if (m_frame.light.isNull())
            return;
        m_lightUntinted = m_frame.light;
    }
    m_frame.light = m_lightUntinted;
    auto* weather = tibia::mods::weatherDriver();
    if (!weather || !weather->enabled() || !m_frame.geometry.valid())
        return;
    const auto state = weather->display();
    // Clouds are a scrolling overlay only — do not dim ambient (rain/storm/fog handle mood).
    const float mood = std::max(state.intensity * (state.kind == 3 /* Storm */ ? 1.f : 0.7f),
                                state.fog * 0.6f);
    if (mood < 0.01f)
        return;
    // Cool / desaturate outdoor light cells; indoor mask unchanged.
    const float amount = mood * (state.kind == 3 /* Storm */ ? 0.45f : 0.28f);
    const float coolR = 1.f - amount * 0.35f;
    const float coolG = 1.f - amount * 0.15f;
    const float coolB = 1.f + amount * 0.08f;
    for (int y = 0; y < m_frame.light.height() && y < m_frame.geometry.height; ++y) {
        auto* line = reinterpret_cast<QRgb*>(m_frame.light.scanLine(y));
        const uchar* outdoor = m_frame.geometry.outdoorMask.constScanLine(y);
        for (int x = 0; x < m_frame.light.width() && x < m_frame.geometry.width; ++x) {
            if (outdoor[x] == 0)
                continue;
            const QRgb c = line[x];
            line[x] = qPremultiply(qRgba(std::clamp(int(qRed(c) * coolR), 0, 255),
                                         std::clamp(int(qGreen(c) * coolG), 0, 255),
                                         std::clamp(int(qBlue(c) * coolB), 0, 255), qAlpha(c)));
        }
    }
}

/// TWorldMapRenderProvider::v7 (0x101056cd0) filling TLightMap (0x10107a2c0).
/// CIP allocates a fresh TLightMap every frame — no idle light-image cache.
void TWorldMapRenderProvider::buildLight()
{
    updateWorldGeometry();

    const light::BuildInput in{
        .camera = m_camera,
        .drawWidth = drawWidth(),
        .drawHeight = drawHeight(),
        .centerX = centerX(),
        .centerY = centerY(),
        .firstFloor = m_firstFloor,
        .lastFloor = m_lastFloor,
        .options = m_lightOptions,
    };
    // One CIP floor-walk: raycast ambient + classic TLightMap (shared fieldFlags / roofs).
    const light::ClassicLightBuild built = light::buildClassicLightPair(in, m_lights, (m_lighting && m_lighting->enabled()));
    if ((m_lighting && m_lighting->enabled()))
        if (m_lighting) m_lighting->setFieldAmbient(light::toLightImage(built.ambient, in.drawWidth, in.drawHeight));
    m_lightUntinted = light::toLightImage(built.classic, in.drawWidth, in.drawHeight);
    m_frame.light = m_lightUntinted;
    applyWeatherAmbientTint();
}

float TWorldMapRenderProvider::lightnessAt(const QPoint& mapPixel) const
{
    // CIP dims creature name/HUD from the light map (TLightMap::v7). With Light
    // Effects off the LightMap is not drawn — keep HUD at full brightness.
    if (!m_lightOptions.effectsEnabled)
        return 1.f;
    return worldmap::TLightMap::lightnessAt(m_frame.light, mapPixel);
}

int TWorldMapRenderProvider::fieldLightness(const Position& field) const
{
    if (!m_lightOptions.effectsEnabled)
        return 255;
    const Point pixel = toScreen(field);
    if (pixel.x < 0 || pixel.y < 0)
        return 0;
    return worldmap::TLightMap::fieldLightness(m_frame.light, pixel.x / FieldSize, pixel.y / FieldSize);
}

void TWorldMapRenderProvider::addLight(const Point& center, const uint8_t intensity, const uint8_t color)
{
    if (intensity > 0)
        m_lights.push_back({ QPoint(center.x, center.y), intensity, color, static_cast<uint8_t>(m_floor) });
}

/// TRenderVolumeProvider::v10 (FUN_101060450): floors above are hidden from the
/// lowest one that covers the camera's field or a field next to it (the plus
/// shape), unless that field blocks the view itself. A floor covers a field
/// with a bank or bottom object drawn over it, or a bank right above it;
/// dont_hide objects never cover. CIP hides them at once, without a fade.
uint8_t TWorldMapRenderProvider::firstVisibleFloor() const
{
    return renderer::TRenderVolumeProvider::firstVisibleFloor(m_camera);
}

uint8_t TWorldMapRenderProvider::lastVisibleFloor() const
{
    return renderer::TRenderVolumeProvider::lastVisibleFloor(m_camera);
}

Point TWorldMapRenderProvider::toScreen(const Position& pos) const
{
    const int floorShift = m_camera.z - pos.z;
    return { (centerX() + pos.x - m_camera.x - floorShift) * FieldSize, (centerY() + pos.y - m_camera.y - floorShift) * FieldSize };
}

QPoint TWorldMapRenderProvider::mapPixelOf(const Position& field) const
{
    const Point pixel = toScreen(field);
    return { pixel.x, pixel.y };
}

Position TWorldMapRenderProvider::lightField(const int x, const int y, const int z) const
{
    return light::lightField(m_camera, centerX(), centerY(), x, y, z);
}

Position TWorldMapRenderProvider::firstField(const int z) const
{
    Position field = m_camera.translated(-centerX(), -centerY());
    field.coveredUp(m_camera.z - z);
    return field;
}

Position TWorldMapRenderProvider::fieldAt(const QPoint& mapPixel, const int z) const
{
    if (!m_camera.isValid())
        return {};
    // TWorldMapPicker (0x100f77870): the inverse of toScreen, field = pixel >> 5.
    const int floorShift = m_camera.z - z;
    const int x = (mapPixel.x() >> 5) - centerX() + m_camera.x + floorShift;
    const int y = (mapPixel.y() >> 5) - centerY() + m_camera.y + floorShift;
    if (x < 0 || y < 0 || z < 0 || z > g_gameConfig.getMapMaxZ())
        return {};
    return { static_cast<uint16_t>(x), static_cast<uint16_t>(y), static_cast<uint8_t>(z) };
}

SpriteRecord::Highlight TWorldMapRenderProvider::highlightOf(Creature& creature) const
{
    if (!m_targetHighlight)
        return SpriteRecord::NoHighlight;
    if (m_attackTarget && creature.getId() == m_attackTarget)
        return SpriteRecord::AttackTarget;
    if (m_followTarget && creature.getId() == m_followTarget)
        return SpriteRecord::FollowTarget;
    return SpriteRecord::NoHighlight;
}

void TWorldMapRenderProvider::updateHighlightBaseplate()
{
    // Drop any previous baseplate so a second advance does not stack frames.
    std::erase_if(m_frame.sprites, [](const SpriteRecord& sprite) { return sprite.sprite.isHighlightBaseplate(); });
    if (!m_mouseHighlight || !m_mouseHighlightField.isValid())
        return;
    const appearances::TWorldmapHighlightBaseplateSpriteInfo info(
        appearances::TWorldmapHighlightBaseplateSpriteInfo::frameAtMillis(g_clock.millis()));
    const QPoint topLeft = mapPixelOf(m_mouseHighlightField);
    // Sprites anchor at bottom-right (same as creature marks on a field).
    m_frame.sprites.push_back({
        .anchor = topLeft + QPoint(FieldSize, FieldSize),
        .sprite = info.key(),
    });
}

/// A field next to a lying object (a 2x2 corpse) draws its creatures and top
/// objects in the lying object's slot, after it (CIP: FUN_101057d80 maps the
/// fields up, left and up-left of each lying object to it; FUN_101055ae0
/// re-keys their creature and top records).
void TWorldMapRenderProvider::addTile(const TilePtr& tile, const Point& dest)
{
    uint8_t elevation = 0;
    addTileBelow(tile, dest, elevation);

    if (const Tile* lying = lyingObjectField(*tile);
        lying && std::find(m_drawnLying.begin(), m_drawnLying.end(), lying) == m_drawnLying.end()) {
        addTileEffects(tile, dest);
        m_lyingDeferred.push_back({ lying, tile, dest, elevation });
        return;
    }

    const auto deferredHere = [this, &tile](auto draw) {
        for (const Deferred& deferred : m_lyingDeferred) {
            if (deferred.lyingField == tile.get())
                draw(deferred);
        }
    };
    const bool lyingHere = hasLyingObject(*tile);
    if (lyingHere) {
        m_drawnLying.push_back(tile.get());
        deferredHere([this](const Deferred& deferred) { addTileCreatures(deferred.tile, deferred.dest, deferred.elevation); });
    }
    addTileCreatures(tile, dest, elevation);
    addTileEffects(tile, dest);
    if (lyingHere) {
        deferredHere([this](const Deferred& deferred) { addTileTop(deferred.tile, deferred.dest); });
        std::erase_if(m_lyingDeferred, [&tile](const Deferred& deferred) { return deferred.lyingField == tile.get(); });
    }
    addTileTop(tile, dest);
}

void TWorldMapRenderProvider::flushLyingDeferred()
{
    for (const Deferred& deferred : m_lyingDeferred) {
        addTileCreatures(deferred.tile, deferred.dest, deferred.elevation);
        addTileTop(deferred.tile, deferred.dest);
    }
    m_lyingDeferred.clear();
}

const Tile* TWorldMapRenderProvider::lyingObjectField(const Tile& tile) const
{
    auto& field = const_cast<Tile&>(tile);
    if (!field.hasCreatures() && !field.hasTopItem() && field.getWalkingCreatures().empty())
        return nullptr;
    // The last lying object in draw order wins: down-right, right, down.
    static constexpr std::array<std::array<int, 2>, 3> Neighbours{ { { 1, 1 }, { 1, 0 }, { 0, 1 } } };
    const Position& pos = field.getPosition();
    for (const auto& [dx, dy] : Neighbours) {
        const TilePtr& neighbour = tibia::world::fields().getTile(pos.translated(dx, dy));
        if (neighbour && neighbour->isDrawable() && !neighbour->isCompletelyCovered(m_firstFloor, false) && hasLyingObject(*neighbour))
            return neighbour.get();
    }
    return nullptr;
}

void TWorldMapRenderProvider::addTileBelow(const TilePtr& tile, const Point& dest, uint8_t& elevation)
{
    const Position& tilePos = tile->getPosition();
    const auto& things = tile->getThings();

    for (const ThingPtr& thing : things) {
        if (!thing->isGround() && !thing->isGroundBorder())
            break;
        addThing(thing, dest, elevation);
    }

    // Walking diagonally past a blocking thing (a tree), the creature is behind it.
    for (const CreaturePtr& creature : tile->getWalkingCreatures()) {
        if (walksDiagonallyAcross(*creature) && creature->getLastStepToPosition() != tilePos) {
            const Position& pos = creature->getPosition();
            const int lift = creature->getDrawElevation();
            std::optional<tibia::mods::WorldLightingDriver::ThingScope> raycastThing;
    if (m_lighting) raycastThing.emplace(m_lighting->thing(*creature, static_cast<uint8_t>(lift)));
            addCreature(Common, *creature,
                        Point(dest.x + (pos.x - tilePos.x) * FieldSize - lift, dest.y + (pos.y - tilePos.y) * FieldSize - lift));
        }
    }

    for (const ThingPtr& thing : things) {
        if (thing->isGround() || thing->isGroundBorder())
            continue;
        if (!thing->isOnBottom())
            break;
        addThing(thing, dest, elevation);
    }

    if (tile->hasCommonItem()) {
        for (auto it = things.rbegin(); it != things.rend(); ++it) {
            if ((*it)->isCommon())
                addThing(*it, dest, elevation);
        }
    }

    if (const auto* effects = tile->getEffects()) {
        uint8_t effectElevation = 0;
        for (const EffectPtr& effect : *effects) {
            if (effect->isUnderCreatures())
                addThing(effect, dest, effectElevation);
        }
    }
}

void TWorldMapRenderProvider::addTileCreatures(const TilePtr& tile, const Point& dest, uint8_t elevation)
{
    const Position& tilePos = tile->getPosition();
    bool localPlayerDrawn = false;
    if (tile->hasCreatures()) {
        for (const ThingPtr& thing : tile->getThings()) {
            if (!thing->isCreature() || static_cast<Creature*>(thing.get())->isWalking())
                continue;
            if (thing->isLocalPlayer()) {
                if (thing->getPosition() != tilePos)
                    continue;
                localPlayerDrawn = true;
            }
            addThing(thing, dest, elevation);
        }
    }

    for (const CreaturePtr& creature : tile->getWalkingCreatures()) {
        if (walksDiagonallyAcross(*creature) && creature->getLastStepToPosition() != tilePos)
            continue;
        const Position& pos = creature->getPosition();
        const int lift = creature->getDrawElevation();
        std::optional<tibia::mods::WorldLightingDriver::ThingScope> raycastThing;
    if (m_lighting) raycastThing.emplace(m_lighting->thing(*creature, static_cast<uint8_t>(lift)));
        addCreature(Common, *creature,
                    Point(dest.x + (pos.x - tilePos.x) * FieldSize - lift, dest.y + (pos.y - tilePos.y) * FieldSize - lift));
    }

    // The player stands here while the server still has them elsewhere (pre-walk).
    const LocalPlayerPtr player = tibia::world::player();
    if (!localPlayerDrawn && player && !player->isWalking() && player->getPosition() == tilePos)
        addThing(player, dest, elevation);
}

void TWorldMapRenderProvider::addTileEffects(const TilePtr& tile, const Point& dest)
{
    uint8_t elevation = 0;
    if (const auto* effects = tile->getEffects()) {
        for (const EffectPtr& effect : *effects) {
            if (!effect->isUnderCreatures())
                addThing(effect, dest, elevation);
        }
    }
}

void TWorldMapRenderProvider::addTileTop(const TilePtr& tile, const Point& dest)
{
    if (!tile->hasTopItem())
        return;
    uint8_t elevation = 0;
    for (const ThingPtr& thing : tile->getThings()) {
        if (thing->isOnTop())
            addThing(thing, dest, elevation);
    }
}

void TWorldMapRenderProvider::addThing(const ThingPtr& thing, const Point& dest, uint8_t& elevation)
{
    const Point at(dest.x - elevation, dest.y - elevation);
    std::optional<tibia::mods::WorldLightingDriver::ThingScope> raycastThing;
    if (m_lighting) raycastThing.emplace(m_lighting->thing(*thing, elevation));

    Order order = Common;
    if (thing->isSingleGround())
        order = Ground;
    else if (thing->isSingleGroundBorder())
        order = GroundBorder;
    else if (thing->isEffect() && tibia::host::app().isDrawingEffectsOnTop() && !static_cast<Effect*>(thing.get())->isUnderCreatures())
        order = OnTop;

    if (thing->isItem())
        addItem(order, static_cast<Item&>(*thing), at);
    else if (thing->isCreature())
        addCreature(order, static_cast<Creature&>(*thing), at);
    else if (thing->isEffect())
        addEffect(order, static_cast<Effect&>(*thing), at);

    if (thing->hasElevation())
        elevation = static_cast<uint8_t>(std::min<int>(elevation + thing->getElevation(), g_gameConfig.getTileMaxElevation()));
}

void TWorldMapRenderProvider::addItem(const Order order, Item& item, const Point& dest)
{
    if (!item.canDraw() || item.isHided())
        return;
    ThingType* type = typeOf(item);
    const auto highlight = m_mouseHighlight && m_mouseTarget.get() == &item ? SpriteRecord::MouseTarget : SpriteRecord::NoHighlight;
    // Show-off socket: PodiumVisible toggles the pedestal sprites; the creature
    // look is independent (MonsterVisible is encoded by presence of look attrs).
    const bool drawSocket = !item.isPodium() || item.isPodiumVisible();
    if (drawSocket) {
        addSprites(order, type, dest, item.getPatternX(), item.getPatternY(), item.getPatternZ(), item.calculateAnimationPhase(),
                   false, 0, highlight);
        if (type && type->hasLight()) {
            // Classic LightMap only here. Raycast uses addFrame art lights (a centre duplicate
            // was creature-like and leaked through walls).
            addLight(dest + Point(FieldSize / 2), type->getLight().intensity, type->getLight().color);
        }
    }
    if (item.isPodium())
        addPodiumOutfit(order, item, dest, highlight);
}

void TWorldMapRenderProvider::addPodiumOutfit(const Order order, Item& item, const Point& dest,
                                             const SpriteRecord::Highlight highlight)
{
    if (!item.hasPodiumOutfit())
        return;

    const Outfit& outfit = item.getPodiumOutfit();
    const int direction = std::clamp<int>(item.getPodiumDirection(), Otc::North, Otc::West);

    if (outfit.isCreature()) {
        ThingType* type = g_things.getRawThingType(outfit.getId(), ThingCategoryCreature);
        if (!type)
            return;

        Point at = dest;
        const bool mounted = outfit.hasMount();
        if (mounted) {
            if (ThingType* mount = g_things.getRawThingType(outfit.getMount(), ThingCategoryCreature)) {
                at -= mount->getDisplacement();
                int mountPhase = 0;
                if (auto* idle = mount->getIdleAnimator())
                    mountPhase = idle->getPhase();
                addSprites(order, mount, at, direction, 0, 0, mountPhase, false, 0, highlight);
                at += type->getDisplacement();
            }
        }

        const int zPattern = mounted ? std::min(1, std::max(0, type->getNumPatternZ() - 1)) : 0;
        int phase = 0;
        if (auto* idle = type->getIdleAnimator())
            phase = idle->getPhase();
        const bool colorize = type->getLayers() > 1;
        const uint32_t colors = colorize ? outfitColors(outfit) : 0;
        for (int addon = 0; addon < type->getNumPatternY(); ++addon) {
            if (addon > 0 && !(outfit.getAddons() & (1 << (addon - 1))))
                continue;
            addSprites(order, type, at, direction, addon, zPattern, phase, colorize, colors, highlight);
        }
        return;
    }

    if (!outfit.isItem())
        return;

    ThingType* type = g_things.getRawThingType(outfit.getAuxId(), ThingCategoryItem);
    if (!type)
        return;

    int phase = 0;
    if (auto* animator = type->getIdleAnimator())
        phase = animator->getPhase();
    else if (const int phases = type->getAnimationPhases(); phases > 1) {
        const int ticks = g_gameConfig.getItemTicksPerFrame();
        phase = static_cast<int>((g_clock.millis() % (static_cast<long long>(ticks) * phases)) / ticks);
    }
    addSprites(order, type, dest - type->getDisplacement(), 0, 0, 0, phase, false, 0, highlight);
}

void TWorldMapRenderProvider::addCreature(const Order order, Creature& creature, const Point& dest)
{
    if (creature.isLocalPlayer() && static_cast<LocalPlayer&>(creature).isSoftDead())
        return;
    if (!creature.canBeSeen() || !creature.canDraw() || (creature.isDead() && !creature.isHidden()) || creature.isHided())
        return;

    Point at = dest + tibia::world::motion().offset(creature);
    if (const PointF jump = creature.getJumpOffset(); !jump.isNull())
        at -= Point(static_cast<int>(std::round(jump.x)), static_cast<int>(std::round(jump.y)));

    Light light = creature.getLight();
    // The local player always sees a little around them in the dark.
    if (creature.isLocalPlayer() && (tibia::world::fields().getLight().intensity < 64 || m_camera.z > g_gameConfig.getMapSeaFloor())) {
        if (light.intensity == 0)
            light.intensity = 2;
        else if (light.color == 0 || light.color > 215)
            light.color = 215;
    }
    addLight(at + Point(FieldSize / 2), light.intensity, light.color);
    if (m_lighting) m_lighting->addCenterLight(at + Point(FieldSize / 2), light.intensity, light.color);

    if (m_floor == m_camera.z && !creature.isDead() && !creature.isHidden()) {
        const uint32_t id = creature.getId();
        const bool listed = std::any_of(m_frame.creatures.begin(), m_frame.creatures.end(),
                                        [id](const CreatureHud& hud) { return hud.creature->getId() == id; });
        if (!listed) {
            const Point head = g_game.getFeature(Otc::GameNegativeOffset) ? at : at - creature.getDisplacement();
            m_frame.creatures.push_back({ creature.static_self_cast<Creature>(), QPoint(head.x, head.y) });
        }
    }

    // FUN_100a3df30: the marks are the creature's first record, on its field
    // shifted by the outfit's displacement.
    if (const std::array<uint32_t, 3> marks = creatureMarks(creature); marks[0]) {
        // CIP TCreatureMarkAppearanceInstanceSpriteInfo on the creature's field.
        const appearances::TCreatureMarkAppearanceInstanceSpriteInfo markInfo(marks);
        const Point anchor = at - creature.getDisplacement() + Point(FieldSize);
        m_orders[order].push_back({ .anchor = QPoint(anchor.x, anchor.y),
                                    .sprite = markInfo.key() });
    }

    const Outfit& outfit = creature.getOutfit();
    ThingType* type = typeOf(creature);
    if (!type)
        return;
    const SpriteRecord::Highlight highlight = highlightOf(creature);
    const Point walked = dest + tibia::world::motion().offset(creature);
    const Point jumped = at;
    addAttachedEffects(order, creature, walked, jumped, false);
    const bool mounted = outfit.hasMount();
    addPaperdolls(order, creature, at, mounted, false, highlight);

    if (outfit.isCreature()) {
        if (ThingType* mount = creature.getMountThingType()) {
            at -= mount->getDisplacement();
            addSprites(order, mount, at, creature.getPatternX(), 0, 0, creature.getCurrentAnimationPhase(true), false, 0, highlight);
            at += creature.getDisplacement();
        }
        const int phase = creature.getCurrentAnimationPhase();
        const bool colorize = creature.isDrawingOutfitColor() && type->getLayers() > 1;
        const uint32_t colors = colorize ? outfitColors(outfit) : 0;
        for (int addon = 0; addon < type->getNumPatternY(); ++addon) {
            if (addon > 0 && !(outfit.getAddons() & (1 << (addon - 1))))
                continue;
            addSprites(order, type, at, creature.getPatternX(), addon, creature.getPatternZ(), phase, colorize, colors, highlight);
        }
        addPaperdolls(order, creature, at, mounted, true, highlight);
        addAttachedEffects(order, creature, walked, jumped, true);
        return;
    }

    // An item outfit, or the invisible effect: the effect loops the phases
    // between its first and last.
    int phases = type->getAnimationPhases();
    int ticks = g_gameConfig.getItemTicksPerFrame();
    if (outfit.isEffect()) {
        phases = std::max(1, phases - 2);
        ticks = g_gameConfig.getInvisibleTicksPerFrame();
    }
    int phase = 0;
    if (auto* animator = type->getIdleAnimator(); animator && outfit.isItem())
        phase = animator->getPhase();
    else if (phases > 1)
        phase = static_cast<int>((g_clock.millis() % (static_cast<long long>(ticks) * phases)) / ticks);
    if (outfit.isEffect())
        phase = std::min(phase + 1, phases);
    addSprites(order, type, at - creature.getDisplacement(), 0, 0, 0, phase, false, 0, highlight);
    addPaperdolls(order, creature, at, mounted, true, highlight);
    addAttachedEffects(order, creature, walked, jumped, true);
}

void TWorldMapRenderProvider::addPaperdolls(const Order order, Creature& creature, const Point& at, const bool mount,
                                            const bool onTop, const SpriteRecord::Highlight highlight)
{
    for (const PaperdollPtr& paperdoll : creature.getPaperdolls()) {
        if (!paperdoll || !paperdoll->shows(mount, onTop))
            continue;
        ThingType* type = paperdoll->thingType();
        if (!type)
            continue;
        const Point dest = at + paperdoll->layerOffset(mount);
        const int phase = paperdoll->animationPhase();
        const bool colorize = type->getLayers() > 1;
        const uint32_t colors = colorize ? paperdoll->packedColors() : 0;
        const int dir = static_cast<int>(paperdoll->direction()) % std::max(1, type->getNumPatternX());
        const int z = creature.getPatternZ();
        if (paperdoll->onlyAddon()) {
            for (int addon = 0; addon < type->getNumPatternY(); ++addon) {
                if (addon > 0 && !(paperdoll->addons() & (1u << (addon - 1))))
                    continue;
                addSprites(order, type, dest, dir, addon, z, phase, colorize, colors, highlight);
            }
        } else {
            addSprites(order, type, dest, dir, 0, z, phase, colorize, colors, highlight);
        }
    }
}

void TWorldMapRenderProvider::addAttachedEffects(const Order order, Creature& creature, const Point& walked, const Point& jumped,
                                                 const bool onTop)
{
    if (!creature.hasAttachedEffects())
        return;
    for (const AttachedEffectPtr& effect : creature.getAttachedEffects()) {
        int phase = 0;
        Point offset;
        float opacity = 1.f;
        if (!effect->getDrawFrame(onTop, phase, offset, opacity))
            continue;
        ThingType* type = effect->getThingType();
        const Point point = (effect->isFollowingOwner() ? jumped : walked) + offset;
        const int xPattern = static_cast<int>(effect->getDirection()) % std::max<int>(1, type->getNumPatternX());
        uint8_t tint = 0;
        if (const auto hook = tibia::mods::effectTintHook())
            tint = hook(effect->getId());
        addSprites(order, type, point, xPattern, 0, 0, phase, false, 0, SpriteRecord::NoHighlight, opacity, tint);
        if (const Light& light = effect->getLight(); light.intensity > 0) {
            addLight(walked + Point(FieldSize / 2), light.intensity, light.color);
            if (m_lighting) m_lighting->addCenterLight(walked + Point(FieldSize / 2), light.intensity, light.color);
        }
        if (type->hasLight()) {
            addLight(point + Point(FieldSize / 2), type->getLight().intensity, type->getLight().color);
            if (m_lighting) m_lighting->addCenterLight(point + Point(FieldSize / 2), type->getLight().intensity, type->getLight().color);
        }
    }
}

void TWorldMapRenderProvider::addEffect(const Order order, Effect& effect, const Point& dest)
{
    const float opacity = effectOpacity(effect.getSource());
    if (opacity < MinEffectOpacity)
        return;
    int phase = 0, xPattern = 0, yPattern = 0;
    if (!effect.getDrawFrame(phase, xPattern, yPattern))
        return;
    // CIP appearances::TFieldEffectInstance / TDelayedEffectInstance / …
    const appearances::TAppearanceInstancePtr effectAppearance =
        appearances::TAppearanceInstanceFactory::fromEffect(effect);
    ThingType* type = effectAppearance ? effectAppearance->thingType() : typeOf(effect);
    if (type && !type->getSprites().empty())
        (void)sprites::TEffectWrapperSpritesheetIdentifier(type->getSprites().front());
    uint8_t tint = 0;
    if (const auto hook = tibia::mods::effectVariantTintHook())
        tint = hook(effect.getVariantId());
    addSprites(order, type, dest, xPattern, yPattern, 0, phase, false, 0, SpriteRecord::NoHighlight, opacity, tint);
    if (type && type->hasLight()) {
        const Point center = dest + Point(FieldSize / 2);
        addLight(center, type->getLight().intensity, type->getLight().color);
        if (m_lighting) m_lighting->addCenterLight(center, type->getLight().intensity, type->getLight().color);
    }
}

void TWorldMapRenderProvider::addMissile(Missile& missile)
{
    const float opacity = effectOpacity(missile.getSource());
    if (opacity < MinEffectOpacity || !missile.canDraw() || missile.isHided())
        return;
    std::optional<tibia::mods::WorldLightingDriver::ThingScope> raycastThing;
    if (m_lighting) raycastThing.emplace(m_lighting->thing(missile, 0));
    // CIP appearances::TMissileEffectInstance; missiles share effects::TEffectStorage.
    const appearances::TAppearanceInstancePtr missileAppearance =
        appearances::TAppearanceInstanceFactory::fromMissile(missile);
    ThingType* type = missileAppearance ? missileAppearance->thingType() : typeOf(missile);
    const Point dest = toScreen(missile.getPosition()) + missile.getFlightOffset();
    addSprites(OnTop, type, dest, missile.getPatternX(), missile.getPatternY(), 0, 0, false, 0, SpriteRecord::NoHighlight, opacity);
    // TWorldMapRenderProvider::v11 (0x10105a180): missiles share TEffectStorage with field effects
    // and light the map from their current flight position.
    if (type && type->hasLight()) {
        const Point center = dest + Point(FieldSize / 2);
        addLight(center, type->getLight().intensity, type->getLight().color);
        if (m_lighting) m_lighting->addCenterLight(center, type->getLight().intensity, type->getLight().color);
    }
}

float TWorldMapRenderProvider::effectOpacity(const Otc::MagicEffectSources source)
{
    return g_client.getEffectAlpha(source);
}

void TWorldMapRenderProvider::addSprites(const Order order, ThingType* type, const Point& dest, const int xPattern, const int yPattern,
                                         const int zPattern, const int animationPhase, const bool colorize, const uint32_t colors,
                                         const SpriteRecord::Highlight highlight, const float opacity, const uint8_t tint)
{
    if (!type || type->isNull() || type->getAnimationPhases() == 0)
        return;

    const auto& sprites = type->getSprites();
    // A thing's frame ends at the bottom-right of its field, less its displacement.
    const Point anchor = dest - type->getDisplacement() + Point(FieldSize);
    auto& out = m_orders[order];
    const bool protobuf = g_game.isUsingProtobuf();
    const bool raycast = (m_lighting && m_lighting->frameActive());

    // .spr files use sprite 0 for blank fields. SpriteKey matches CIP SpriteInfo::key().
    const auto exists = [&](const uint32_t index) { return index < sprites.size() && (protobuf || sprites[index] != 0); };
    const auto add = [&](const int w, const int h, const Point& at) {
        const uint32_t index = type->getSpriteIndex(w, h, 0, xPattern, yPattern, zPattern, animationPhase);
        if (!exists(index))
            return;
        SpriteKey key{ .spriteId = sprites[index] };
        if (colorize) {
            const uint32_t maskIndex = type->getSpriteIndex(w, h, 1, xPattern, yPattern, zPattern, animationPhase);
            if (exists(maskIndex)) {
                key.maskId = sprites[maskIndex];
                key.colors = colors;
            }
        }
        key.tint = tint;
        out.push_back({ .anchor = QPoint(at.x, at.y),
                        .sprite = key,
                        .opacity = opacity,
                        .highlight = highlight });
    };

    // Protobuf sprites are the whole frame; .spr ones are 32x32 fields from the
    // bottom-right one up.
    if (protobuf) {
        const uint32_t index = type->getSpriteIndex(-1, -1, 0, xPattern, yPattern, zPattern, animationPhase);
        if (exists(index)) {
            SpriteKey key{ .spriteId = sprites[index], .tint = tint };
            if (colorize) {
                const uint32_t maskIndex = type->getSpriteIndex(-1, -1, 1, xPattern, yPattern, zPattern, animationPhase);
                if (exists(maskIndex)) {
                    key.maskId = sprites[maskIndex];
                    key.colors = colors;
                }
            }
            out.push_back({ .anchor = QPoint(anchor.x, anchor.y),
                            .sprite = key,
                            .opacity = opacity,
                            .highlight = highlight });
        }
        if (raycast) {
            const int stampPh = (m_lighting ? m_lighting->stampPhase(animationPhase) : 0);
            const uint32_t stampIndex = stampPh == animationPhase
                ? index
                : type->getSpriteIndex(-1, -1, 0, xPattern, yPattern, zPattern, stampPh);
            if (stampIndex < sprites.size())
                if (m_lighting) m_lighting->addFrame(*type, anchor, sprites[stampIndex], xPattern);
        }
        return;
    }
    const Size size = type->getSize();
    for (int h = 0; h < size.height(); ++h) {
        for (int w = 0; w < size.width(); ++w)
            add(w, h, anchor - Point(w * FieldSize, h * FieldSize));
    }
}

}
