/*
 * Copyright (c) 2024-2026 qtclient contributors
 *
 * Official CIP map-*.dat index for cyclopedia labels, silhouettes, and
 * subarea occupancy lookup (things/<ver>/subarea-*.bmp.lzma).
 */

#pragma once

#include "position.h"

#include <framework/graphics/declarations.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

enum class CyclopediaAreaKind : uint8_t
{
    City = 1,
    Subarea = 2,
};

struct CyclopediaMapBoundary
{
    int x{ 0 };
    int y{ 0 };
    int z{ 0 };
    int w{ 0 };
    int h{ 0 };
};

struct CyclopediaMapArea
{
    int id{ 0 };
    std::string name;
    CyclopediaAreaKind kind{ CyclopediaAreaKind::Subarea };
    std::vector<int> children;
    std::optional<Position> position;
    int hidden{ 0 };
    std::vector<std::string> labels;
    int parentId{ 0 };
    std::string cityName;
    std::string overviewFile;
    std::optional<CyclopediaMapBoundary> boundary;
    // "subarea" | "city" | "settlement" | "surroundings" | "hunt"
    std::string displayKind{ "subarea" };
};

class CyclopediaMap
{
public:
    void clear();

    // Load official things/<ver>/map-*.dat only (CIP areas / subareas).
    bool load(int clientVersion = 0);
    bool isLoaded() { return m_loaded; }

    // Compact JSON index for Lua (same shape as the old subareas.json).
    std::string getIndexJson();

    std::string getAreaName(int areaId);
    int getParentAreaId(int areaId);
    // Prefer opaque CIP overview-mask hits (smallest AABB), else smallest AABB.
    int getSubareaAt(const Position& tile);

    const CyclopediaMapArea* getArea(int areaId) const;

private:
    bool loadDatFile(const std::string& path, bool isCustom);
    void rebuildDerived();
    std::string findOfficialMapDat(int clientVersion) const;
    std::string resolveOverviewPath(const std::string& fileName) const;
    const ImagePtr& getOverviewImage(const CyclopediaMapArea& area);
    bool overviewMaskContains(const CyclopediaMapArea& area, const Position& tile);

    bool m_loaded{ false };
    std::unordered_map<int, CyclopediaMapArea> m_areas;
    std::vector<int> m_hitTestOrder; // subarea ids with boundary, smallest first
    std::unordered_map<int, ImagePtr> m_overviewImages;
};

extern CyclopediaMap g_cyclopediaMap;
