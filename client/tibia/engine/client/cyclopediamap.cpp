/*
 * Copyright (c) 2024-2026 qtclient contributors
 */

#include "cyclopediamap.h"

#include "tibia/world/game.h"

#include <framework/core/logger.h>
#include <framework/core/resourcemanager.h>
#include <framework/graphics/image.h>
#include <framework/stdext/string.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <sstream>

CyclopediaMap g_cyclopediaMap;

namespace {

constexpr int WT_VARINT = 0;
constexpr int WT_LEN = 2;

struct ProtoField
{
    int fn{ 0 };
    int wt{ 0 };
    uint64_t varint{ 0 };
    std::string_view bytes;
};

bool readVarint(std::string_view buf, size_t& i, uint64_t& out)
{
    out = 0;
    int shift = 0;
    while (i < buf.size()) {
        const uint8_t b = static_cast<uint8_t>(buf[i++]);
        out |= static_cast<uint64_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0)
            return true;
        shift += 7;
        if (shift > 70)
            return false;
    }
    return false;
}

bool parseFields(std::string_view buf, std::vector<ProtoField>& out)
{
    size_t i = 0;
    while (i < buf.size()) {
        uint64_t key = 0;
        if (!readVarint(buf, i, key))
            return false;
        ProtoField field;
        field.fn = static_cast<int>(key >> 3);
        field.wt = static_cast<int>(key & 7);
        if (field.wt == WT_VARINT) {
            if (!readVarint(buf, i, field.varint))
                return false;
        } else if (field.wt == WT_LEN) {
            uint64_t len = 0;
            if (!readVarint(buf, i, len) || i + len > buf.size())
                return false;
            field.bytes = buf.substr(i, static_cast<size_t>(len));
            i += static_cast<size_t>(len);
        } else if (field.wt == 1) {
            if (i + 8 > buf.size())
                return false;
            i += 8;
            continue;
        } else if (field.wt == 5) {
            if (i + 4 > buf.size())
                return false;
            i += 4;
            continue;
        } else {
            return false;
        }
        out.push_back(field);
    }
    return true;
}

std::optional<Position> decodePosition(std::string_view raw)
{
    std::vector<ProtoField> fields;
    if (!parseFields(raw, fields))
        return std::nullopt;
    int x = 0, y = 0, z = 0;
    bool hasX = false, hasY = false, hasZ = false;
    for (const auto& f : fields) {
        if (f.wt != WT_VARINT)
            continue;
        if (f.fn == 1) {
            x = static_cast<int>(f.varint);
            hasX = true;
        } else if (f.fn == 2) {
            y = static_cast<int>(f.varint);
            hasY = true;
        } else if (f.fn == 3) {
            z = static_cast<int>(f.varint);
            hasZ = true;
        }
    }
    if (!hasX || !hasY || !hasZ)
        return std::nullopt;
    return Position(x, y, static_cast<uint8_t>(z));
}

std::string jsonEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (const unsigned char c : s) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return out;
}

void appendPosJson(std::ostringstream& oss, const Position& pos)
{
    oss << "{\"x\":" << pos.x << ",\"y\":" << pos.y << ",\"z\":" << static_cast<int>(pos.z) << '}';
}

void appendBoundaryJson(std::ostringstream& oss, const CyclopediaMapBoundary& b)
{
    oss << "{\"x\":" << b.x << ",\"y\":" << b.y << ",\"z\":" << b.z
        << ",\"w\":" << b.w << ",\"h\":" << b.h << '}';
}

} // namespace

void CyclopediaMap::clear()
{
    m_areas.clear();
    m_hitTestOrder.clear();
    m_overviewImages.clear();
    m_loaded = false;
}

std::string CyclopediaMap::findOfficialMapDat(int /*clientVersion*/) const
{
    // CIP short name under Resources/ (assetsRoot registers the search path).
    const std::string dir = "assets/";
    if (!tibia::files::data().directoryExists(dir))
        return {};
    for (const auto& file : tibia::files::data().listDirectoryFiles(dir, false, false, false)) {
        auto lower = file;
        stdext::tolower(lower);
        if (lower.starts_with("map-") && lower.ends_with(".dat"))
            return dir + file;
    }
    return {};
}

bool CyclopediaMap::loadDatFile(const std::string& path, bool isCustom)
{
    if (!tibia::files::data().fileExists(path))
        return false;

    const std::string buf = tibia::files::data().readFileContents(path);
    std::vector<ProtoField> top;
    if (!parseFields(buf, top)) {
        g_logger.error("Cyclopedia map: failed to parse " + path);
        return false;
    }

    struct AssetRow
    {
        int kind{ 0 };
        std::optional<Position> position;
        std::string file;
        int width{ 0 };
        int height{ 0 };
        int areaId{ 0 };
    };
    std::vector<AssetRow> assets;

    for (const auto& field : top) {
        if (field.fn == 1 && field.wt == WT_LEN) {
            std::vector<ProtoField> inner;
            if (!parseFields(field.bytes, inner))
                continue;
            CyclopediaMapArea area;
            for (const auto& f : inner) {
                if (f.fn == 1 && f.wt == WT_VARINT)
                    area.id = static_cast<int>(f.varint);
                else if (f.fn == 2 && f.wt == WT_LEN)
                    area.name.assign(f.bytes.begin(), f.bytes.end());
                else if (f.fn == 3 && f.wt == WT_VARINT)
                    area.kind = static_cast<CyclopediaAreaKind>(f.varint);
                else if (f.fn == 4 && f.wt == WT_VARINT)
                    area.children.push_back(static_cast<int>(f.varint));
                else if (f.fn == 5 && f.wt == WT_LEN)
                    area.position = decodePosition(f.bytes);
                else if (f.fn == 6 && f.wt == WT_VARINT)
                    area.hidden = static_cast<int>(f.varint);
                else if (f.fn == 7 && f.wt == WT_LEN) {
                    std::string label(f.bytes.begin(), f.bytes.end());
                    if (!label.empty())
                        area.labels.push_back(std::move(label));
                }
            }
            if (area.id == 0)
                continue;
            if (isCustom) {
                area.displayKind = "hunt";
            } else if (area.kind == CyclopediaAreaKind::City) {
                area.displayKind = "city";
            } else {
                area.displayKind = "subarea";
            }
            auto& dest = m_areas[area.id];
            if (!isCustom || dest.id == 0) {
                // Official always wins; custom inserts new wiki/product rows.
                dest = std::move(area);
            } else {
                if (!dest.position && area.position)
                    dest.position = area.position;
                if (dest.name.empty())
                    dest.name = std::move(area.name);
            }
        } else if (field.fn == 3 && field.wt == WT_LEN) {
            std::vector<ProtoField> inner;
            if (!parseFields(field.bytes, inner))
                continue;
            AssetRow asset;
            for (const auto& f : inner) {
                if (f.fn == 1 && f.wt == WT_VARINT)
                    asset.kind = static_cast<int>(f.varint);
                else if (f.fn == 2 && f.wt == WT_LEN)
                    asset.position = decodePosition(f.bytes);
                else if (f.fn == 3 && f.wt == WT_LEN)
                    asset.file.assign(f.bytes.begin(), f.bytes.end());
                else if (f.fn == 4 && f.wt == WT_VARINT)
                    asset.width = static_cast<int>(f.varint);
                else if (f.fn == 5 && f.wt == WT_VARINT)
                    asset.height = static_cast<int>(f.varint);
                else if (f.fn == 6 && f.wt == WT_VARINT)
                    asset.areaId = static_cast<int>(f.varint);
            }
            if (asset.kind == 0 && asset.areaId > 0)
                assets.push_back(std::move(asset));
        }
    }

    for (const auto& asset : assets) {
        auto it = m_areas.find(asset.areaId);
        if (it == m_areas.end()) {
            // Custom MapAssets without AreaData still need a stub for occupancy.
            CyclopediaMapArea stub;
            stub.id = asset.areaId;
            stub.kind = CyclopediaAreaKind::Subarea;
            stub.displayKind = isCustom ? "hunt" : "subarea";
            m_areas[asset.areaId] = stub;
            it = m_areas.find(asset.areaId);
        }
        auto& area = it->second;
        if (asset.position && asset.width > 0 && asset.height > 0) {
            // Official assets win; custom only fills missing silhouettes.
            if (!area.boundary) {
                area.boundary = CyclopediaMapBoundary{
                    asset.position->x,
                    asset.position->y,
                    static_cast<int>(asset.position->z),
                    asset.width,
                    asset.height
                };
            }
            if (!area.position)
                area.position = asset.position;
        }
        if (!asset.file.empty() && area.overviewFile.empty())
            area.overviewFile = asset.file;
    }

    return true;
}

void CyclopediaMap::rebuildDerived()
{
    // Parent city/region from AreaData.children.
    for (auto& [id, area] : m_areas) {
        (void)id;
        if (area.kind != CyclopediaAreaKind::City)
            continue;
        for (const int childId : area.children) {
            auto it = m_areas.find(childId);
            if (it == m_areas.end())
                continue;
            if (it->second.parentId == 0)
                it->second.parentId = area.id;
            if (it->second.cityName.empty())
                it->second.cityName = area.name;
        }
    }

    m_hitTestOrder.clear();
    for (const auto& [id, area] : m_areas) {
        if (area.kind != CyclopediaAreaKind::Subarea)
            continue;
        if (!area.boundary || area.boundary->w <= 0 || area.boundary->h <= 0)
            continue;
        m_hitTestOrder.push_back(id);
    }
    std::sort(m_hitTestOrder.begin(), m_hitTestOrder.end(), [this](int a, int b) {
        const auto& aa = m_areas[a].boundary;
        const auto& bb = m_areas[b].boundary;
        const int areaA = aa->w * aa->h;
        const int areaB = bb->w * bb->h;
        if (areaA != areaB)
            return areaA < areaB;
        return a < b;
    });
}

bool CyclopediaMap::load(int clientVersion)
{
    clear();
    if (clientVersion <= 0 && g_game.getClientVersion() > 0)
        clientVersion = g_game.getClientVersion();

    const std::string official = findOfficialMapDat(clientVersion);
    if (official.empty()) {
        g_logger.warning("Cyclopedia map: no map-*.dat found under assets/");
        return false;
    }
    if (!loadDatFile(official, false))
        return false;

    rebuildDerived();
    m_loaded = !m_areas.empty();
    if (m_loaded) {
        g_logger.info(
            "Cyclopedia map: loaded " + std::to_string(m_areas.size())
            + " areas from " + official
        );
    }
    return m_loaded;
}

std::string CyclopediaMap::getIndexJson()
{
    if (!m_loaded)
        return R"({"version":1,"areas":[],"subareas":[]})";

    std::ostringstream oss;
    oss << "{\"version\":1,\"areas\":[";
    bool firstArea = true;
    std::vector<int> ids;
    ids.reserve(m_areas.size());
    for (const auto& [id, _] : m_areas)
        ids.push_back(id);
    std::sort(ids.begin(), ids.end());

    for (const int id : ids) {
        const auto& area = m_areas.at(id);
        if (area.kind != CyclopediaAreaKind::City || area.hidden)
            continue;
        if (!area.position)
            continue;
        if (!firstArea)
            oss << ',';
        firstArea = false;
        oss << "{\"id\":" << area.id
            << ",\"name\":\"" << jsonEscape(area.name) << "\",\"position\":";
        appendPosJson(oss, *area.position);
        oss << '}';
    }

    oss << "],\"subareas\":[";
    bool firstSub = true;
    for (const int id : ids) {
        const auto& area = m_areas.at(id);
        if (area.kind != CyclopediaAreaKind::Subarea)
            continue;
        // Prefer entries that have a pin and/or silhouette.
        if (!area.position && !area.boundary)
            continue;
        if (!firstSub)
            oss << ',';
        firstSub = false;
        oss << "{\"id\":" << area.id
            << ",\"name\":\"" << jsonEscape(area.name) << '"'
            << ",\"kind\":\"" << jsonEscape(area.displayKind) << '"'
            << ",\"city\":\"" << jsonEscape(area.cityName) << '"';
        if (area.parentId > 0)
            oss << ",\"cityId\":" << area.parentId;
        if (!area.overviewFile.empty())
            oss << ",\"overviewFile\":\"" << jsonEscape(area.overviewFile) << '"';
        else
            oss << ",\"overviewFile\":null";
        if (area.boundary) {
            oss << ",\"boundary\":";
            appendBoundaryJson(oss, *area.boundary);
        } else {
            oss << ",\"boundary\":null";
        }
        if (area.position) {
            oss << ",\"position\":";
            appendPosJson(oss, *area.position);
        } else if (area.boundary) {
            const auto& b = *area.boundary;
            oss << ",\"position\":{\"x\":" << (b.x + b.w / 2)
                << ",\"y\":" << (b.y + b.h / 2)
                << ",\"z\":" << b.z << '}';
        } else {
            oss << ",\"position\":null";
        }
        oss << '}';
    }
    oss << "]}";
    return oss.str();
}

std::string CyclopediaMap::getAreaName(int areaId)
{
    const auto* area = getArea(areaId);
    return area ? area->name : std::string{};
}

int CyclopediaMap::getParentAreaId(int areaId)
{
    const auto* area = getArea(areaId);
    return area ? area->parentId : 0;
}

std::string CyclopediaMap::resolveOverviewPath(const std::string& fileName) const
{
    if (fileName.empty())
        return {};

    const std::string path = "assets/" + fileName;
    if (tibia::files::data().fileExists(path))
        return path;
    return {};
}

const ImagePtr& CyclopediaMap::getOverviewImage(const CyclopediaMapArea& area)
{
    const auto it = m_overviewImages.find(area.id);
    if (it != m_overviewImages.end())
        return it->second;

    ImagePtr img;
    const std::string path = resolveOverviewPath(area.overviewFile);
    if (!path.empty() && tibia::files::data().fileExists(path)) {
        try {
            img = Image::load(path);
        } catch (const std::exception&) {
            img = nullptr;
        }
    }
    return m_overviewImages.emplace(area.id, img).first->second;
}

bool CyclopediaMap::overviewMaskContains(const CyclopediaMapArea& area, const Position& tile)
{
    if (!area.boundary || area.overviewFile.empty())
        return false;

    const auto& b = *area.boundary;
    const int px = tile.x - b.x;
    const int py = tile.y - b.y;
    if (px < 0 || py < 0 || px >= b.w || py >= b.h)
        return false;

    const ImagePtr& img = getOverviewImage(area);
    if (!img || !img->hasPixels())
        return false;
    if (px >= img->getWidth() || py >= img->getHeight())
        return false;

    // CIP overview masks are 1px per world tile; alpha marks the silhouette interior.
    return img->getPixel(px, py)[3] > 40;
}

int CyclopediaMap::getSubareaAt(const Position& tile)
{
    static constexpr int SEA_FLOOR = 7;

    int bestMaskId = 0;
    int bestMaskArea = INT_MAX;
    int bestAabbId = 0;
    int bestAabbArea = INT_MAX;

    for (const int id : m_hitTestOrder) {
        const auto& area = m_areas.at(id);
        const auto& b = *area.boundary;
        const int viewZ = static_cast<int>(tile.z);
        // Surface silhouettes (z<=7) stay valid on ground and floors above.
        if (b.z <= SEA_FLOOR) {
            if (viewZ > SEA_FLOOR)
                continue;
        } else if (viewZ != b.z) {
            continue;
        }

        if (tile.x < b.x || tile.x >= b.x + b.w || tile.y < b.y || tile.y >= b.y + b.h)
            continue;

        const int areaSize = b.w * b.h;
        if (areaSize < bestAabbArea) {
            bestAabbArea = areaSize;
            bestAabbId = id;
        }

        // Prefer opaque overview-mask hits: Thais City AABB is larger than
        // Thais Surroundings, so AABB-only picks would select the wrong region.
        if (overviewMaskContains(area, tile) && areaSize < bestMaskArea) {
            bestMaskArea = areaSize;
            bestMaskId = id;
        }
    }

    return bestMaskId != 0 ? bestMaskId : bestAabbId;
}

const CyclopediaMapArea* CyclopediaMap::getArea(int areaId) const
{
    const auto it = m_areas.find(areaId);
    return it == m_areas.end() ? nullptr : &it->second;
}
