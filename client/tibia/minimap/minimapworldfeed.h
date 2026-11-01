#pragma once

#include "minimap/minimaptilestorage.h"

#include <tibia/world/map.h>

namespace tibia::worldmap {
class TWorldmapProtocolMessageHandler;
}

namespace tibia::minimap {

/// The engine's map as the minimap's world: field data from qtclient tiles,
/// and the engine's map notifications driving the storage the way CIP's map
/// messages do (TMinimapTileStorage onFieldChanged / setPlayerZ / markUnseen).
class TMinimapWorldFeed final : public MapObserver, public IMinimapWorld
{
public:
    /// Observes g_map until destroyed.
    explicit TMinimapWorldFeed(TMinimapTileStorage& storage);
    ~TMinimapWorldFeed() override;

    /// Optional CIP map-protocol bridge (forwards tile/window changes).
    void setProtocolHandler(worldmap::TWorldmapProtocolMessageHandler* handler) { m_protocol = handler; }

    void onTileChanged(const Position& pos) override;
    void onWindowChanged(bool fullDescription) override;

    bool isKnown(const Position& coord) const override;
    TMinimapFieldData fieldData(const Position& coord) const override;
    void forEachKnownField(int z, const std::function<void(const Position&)>& visit) const override;
    QRect areaOn(int z) const override;

private:
    TMinimapTileStorage& m_storage;
    worldmap::TWorldmapProtocolMessageHandler* m_protocol = nullptr;
};

}
