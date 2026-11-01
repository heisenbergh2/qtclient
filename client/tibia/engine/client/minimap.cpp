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

#include "minimap.h"

#include "gameconfig.h"
#include "tibia/world/tile.h"
#include "framework/core/clock.h"
#include "framework/core/filestream.h"
#include "framework/core/resourcemanager.h"
#include "framework/graphics/image.h"
#include "framework/graphics/texture.h"
#include <algorithm>
#include <cmath>

Minimap g_minimap;
static MinimapTile nulltile;

// Blocks off screen this long drop their textures (one per 64x64 block ever drawn otherwise).
static constexpr ticks_t MINIMAP_TEXTURE_IDLE_TIME = 60 * 1000;
static constexpr ticks_t MINIMAP_EVICTION_INTERVAL = 10 * 1000;

void MinimapBlock::clean()
{
    m_tiles.fill({});
    m_texture.reset();
    m_mustUpdate = false;
}

void MinimapBlock::update()
{
    if (!m_mustUpdate)
        return;

    // The texture keeps the pixels only until it is uploaded.
    const auto image = std::make_shared<Image>(m_size);

    bool shouldDraw = false;
    for (uint_fast8_t x = 0; x < MMBLOCK_SIZE; ++x) {
        for (uint_fast8_t y = 0; y < MMBLOCK_SIZE; ++y) {
            const uint8_t c = getTile(x, y).color;

            Color col = Color::black;
            if (c != UINT8_MAX) {
                col = Color::from8bit(c);
                shouldDraw = true;
            }

            image->setPixel(x, y, col);
        }
    }

    if (shouldDraw)
        m_texture = std::make_shared<Texture>(image, true, false);
    else
        m_texture.reset();

    m_mustUpdate = false;
}

void MinimapBlock::evictTexture(const ticks_t now, const ticks_t idle)
{
    if (!m_texture || now - m_lastDrawn < idle)
        return;
    m_texture.reset();
    m_mustUpdate = true;
}

void MinimapBlock::updateTile(const int x, const int y, const MinimapTile& tile)
{
    if (m_tiles[getTileIndex(x, y)].color != tile.color)
        m_mustUpdate = true;

    m_tiles[getTileIndex(x, y)] = tile;
}

void Minimap::init() {
    m_tileBlocks.resize(g_gameConfig.getMapMaxZ() + 1);
    m_markers.resize(g_gameConfig.getMapMaxZ() + 1);
}

void Minimap::clearMarkers()
{
    for (auto& floor : m_markers)
        floor.clear();
}

bool Minimap::addMarker(const Position& pos, const uint8_t icon, const std::string& description)
{
    if (!pos.isValid() || pos.x < 0 || pos.x > UINT16_MAX || pos.y < 0 || pos.y > UINT16_MAX
        || pos.z >= m_markers.size() || icon >= MINIMAP_MARKER_ICONS)
        return false;

    m_markers[pos.z].push_back({ static_cast<uint16_t>(pos.x), static_cast<uint16_t>(pos.y), icon, description });
    return true;
}

bool Minimap::removeMarker(const Position& pos)
{
    if (!pos.isValid() || pos.z >= m_markers.size())
        return false;
    auto& floor = m_markers[pos.z];
    const auto it = std::find_if(floor.begin(), floor.end(), [&](const MinimapMarker& marker) {
        return marker.x == pos.x && marker.y == pos.y;
    });
    if (it == floor.end())
        return false;
    floor.erase(it);
    return true;
}

bool Minimap::updateMarker(const Position& pos, const uint8_t icon, const std::string& description)
{
    if (!pos.isValid() || pos.z >= m_markers.size() || icon >= MINIMAP_MARKER_ICONS)
        return false;
    auto& floor = m_markers[pos.z];
    for (MinimapMarker& marker : floor) {
        if (marker.x == pos.x && marker.y == pos.y) {
            marker.icon = icon;
            marker.description = description;
            return true;
        }
    }
    return addMarker(pos, icon, description);
}

bool Minimap::markerAt(const Position& pos, MinimapMarker& out) const
{
    if (!pos.isValid() || pos.z >= m_markers.size())
        return false;
    for (const MinimapMarker& marker : m_markers[pos.z]) {
        if (marker.x == pos.x && marker.y == pos.y) {
            out = marker;
            return true;
        }
    }
    return false;
}

size_t Minimap::getMarkerCount() const
{
    size_t count = 0;
    for (const auto& floor : m_markers)
        count += floor.size();
    return count;
}

const std::vector<MinimapMarker>& Minimap::getFloorMarkers(const uint8_t z) const
{
    static const std::vector<MinimapMarker> empty;
    return z < m_markers.size() ? m_markers[z] : empty;
}

void Minimap::terminate() { clean(); }

void Minimap::clean()
{
    SpinLock::Guard lock(m_lock);
    for (uint_fast8_t i = 0; i <= g_gameConfig.getMapMaxZ(); ++i)
        m_tileBlocks[i].clear();
}

void Minimap::draw(const Rect& screenRect, const Position& mapCenter, const float scale, const Color& color)
{
    (void)screenRect; (void)mapCenter; (void)scale; (void)color;
}


Point Minimap::getTilePoint(const Position& pos, const Rect& screenRect, const Position& mapCenter, const float scale)
{
    if (screenRect.isEmpty() || pos.z != mapCenter.z)
        return { -1 };

    const auto& mapRect = calcMapRect(screenRect, mapCenter, scale);
    const auto& off = Point((mapRect.size() * scale).toPoint() - screenRect.size().toPoint()) / 2;
    const auto& posoff = (Point(pos.x, pos.y) - mapRect.topLeft()) * scale;
    return posoff + screenRect.topLeft() - off + (Point(1) * scale) / 2;
}

Position Minimap::getTilePosition(const Point& point, const Rect& screenRect, const Position& mapCenter, const float scale)
{
    if (screenRect.isEmpty())
        return {};

    const auto& mapRect = calcMapRect(screenRect, mapCenter, scale);
    const auto& off = Point((mapRect.size() * scale).toPoint() - screenRect.size().toPoint()) / 2;
    const auto& pos2d = (point - screenRect.topLeft() + off) / scale + mapRect.topLeft();
    return { pos2d.x, pos2d.y, mapCenter.z };
}

Rect Minimap::getTileRect(const Position& pos, const Rect& screenRect, const Position& mapCenter, const float scale)
{
    if (screenRect.isEmpty() || pos.z != mapCenter.z)
        return {};

    // Minimap draws 1 map tile as `scale` screen pixels (see Minimap::draw).
    // Do not multiply by spriteSize — that is game-view tile size, not minimap.
    const int tileSize = std::max<int>(1, static_cast<int>(std::lround(scale)));

    Rect tileRect(0, 0, tileSize, tileSize);
    tileRect.moveCenter(getTilePoint(pos, screenRect, mapCenter, scale));
    return tileRect;
}

Rect Minimap::calcMapRect(const Rect& screenRect, const Position& mapCenter, const float scale) const
{
    const int w = screenRect.width() / scale;
    const int h = std::ceil(screenRect.height() / scale);

    Rect mapRect(0, 0, w, h);
    mapRect.moveCenter(Point(mapCenter.x, mapCenter.y));
    return mapRect;
}

void Minimap::updateTile(const Position& pos, const TilePtr& tile)
{
    MinimapTile minimapTile;
    if (tile) {
        minimapTile.color = tile->getMinimapColorByte();
        minimapTile.flags |= MinimapTileWasSeen;
        if (!tile->isWalkable(true))
            minimapTile.flags |= MinimapTileNotWalkable;
        if (!tile->isPathable())
            minimapTile.flags |= MinimapTileNotPathable;
        minimapTile.speed = std::min<int>(static_cast<int>(std::ceil(tile->getGroundSpeed() / 10.f)), UINT8_MAX);
    } else {
        minimapTile.flags |= MinimapTileNotWalkable | MinimapTileNotPathable;
    }

    if (minimapTile != nulltile) {
        MinimapBlock& block = getBlock(pos);
        const auto& offsetPos = getBlockOffset(Point(pos.x, pos.y));
        block.updateTile(pos.x - offsetPos.x, pos.y - offsetPos.y, minimapTile);
        block.justSaw();
    }
}

const MinimapTile& Minimap::getTile(const Position& pos)
{
    if (pos.z <= g_gameConfig.getMapMaxZ() && hasBlock(pos)) {
        MinimapBlock& block = getBlock(pos);
        const auto& offsetPos = getBlockOffset(Point(pos.x, pos.y));
        return block.getTile(pos.x - offsetPos.x, pos.y - offsetPos.y);
    }
    return nulltile;
}

std::pair<MinimapBlock_ptr, MinimapTile> Minimap::threadGetTile(const Position& pos)
{
    SpinLock::Guard lock(m_lock);

    if (pos.z <= g_gameConfig.getMapMaxZ() && hasBlock(pos)) {
        const auto& block = m_tileBlocks[pos.z][getBlockIndex(pos)];
        if (block) {
            const auto& offsetPos = getBlockOffset(Point(pos.x, pos.y));
            return std::make_pair(block, block->getTile(pos.x - offsetPos.x, pos.y - offsetPos.y));
        }
    }
    return std::make_pair(nullptr, nulltile);
}

bool Minimap::loadImage(const std::string& fileName, const Position& topLeft, float colorFactor)
{
    // non pathable colors
    static Color nonPathableColors[] = {
       "#ffff00"sv, // yellow
    };

    // non walkable colors
    static Color nonWalkableColors[] = {
       "#000000"sv, // oil, black
       "#006600"sv, // trees, dark green
       "#ff3300"sv, // walls, red
       "#666666"sv, // mountain, grey
       "#ff6600"sv, // lava, orange
       "#00ff00"sv, // positon
       "#ccffff"sv, // ice, very light blue
    };

    if (colorFactor <= .01f)
        colorFactor = 1.f;

    try {
        const ImagePtr image = Image::load(fileName);

        const uint8_t waterc = Color::to8bit("#3300cc"sv);

        for (int_fast32_t y = -1; ++y < image->getHeight();) {
            for (int_fast32_t x = -1; ++x < image->getWidth();) {
                Color color = *(uint32_t*)image->getPixel(x, y);
                uint8_t c = Color::to8bit(color * colorFactor);
                int flags = 0;

                if (c == waterc || color.a() == 0) {
                    flags |= MinimapTileNotWalkable;
                    c = UINT8_MAX; // alpha
                }

                if (flags != 0) {
                    for (const Color& col : nonWalkableColors) {
                        if (col == color) {
                            flags |= MinimapTileNotWalkable;
                            break;
                        }
                    }
                }

                if (flags != 0) {
                    for (const Color& col : nonPathableColors) {
                        if (col == color) {
                            flags |= MinimapTileNotPathable;
                            break;
                        }
                    }
                }

                if (c == UINT8_MAX)
                    continue;

                Position pos(topLeft.x + x, topLeft.y + y, topLeft.z);
                MinimapBlock& block = getBlock(pos);
                const auto& offsetPos = getBlockOffset(Point(pos.x, pos.y));
                MinimapTile& tile = block.getTile(pos.x - offsetPos.x, pos.y - offsetPos.y);
                if (!(tile.flags & MinimapTileWasSeen)) {
                    tile.color = c;
                    tile.flags = flags;
                    block.mustUpdate();
                }
            }
        }
        return true;
    } catch (const stdext::exception& e) {
        g_logger.error("Failed to load OTMM minimap: {}", e.what());
        return false;
    }
}

void Minimap::saveImage(const std::string&, const Rect&)
{
    //TODO
}

bool Minimap::loadOtmm(const std::string& fileName)
{
    try {
        const FileStreamPtr fin = tibia::files::data().openFile(fileName);
        if (!fin)
            throw Exception("unable to open file");

        fin->cache();

        const uint32_t signature = fin->getU32();
        if (signature != OTMM_SIGNATURE)
            throw Exception("invalid OTMM file");

        const uint16_t start = fin->getU16();
        const uint16_t version = fin->getU16();
        fin->getU32(); // flags

        switch (version) {
            case 1:
            {
                fin->getString(); // description
                break;
            }
            default:
                throw Exception("OTMM version not supported");
        }

        fin->seek(start);

        constexpr uint32_t blockSize = MMBLOCK_SIZE * MMBLOCK_SIZE * sizeof(MinimapTile);
        std::vector<uint8_t> compressBuffer(compressBound(blockSize));
        std::vector<uint8_t> decompressBuffer(blockSize);

        while (true) {
            Position pos;
            pos.x = fin->getU16();
            pos.y = fin->getU16();
            pos.z = fin->getU8();

            // end of file or file is corrupted
            if (!pos.isValid() || pos.z >= g_gameConfig.getMapMaxZ() + 1)
                break;

            MinimapBlock& block = getBlock(pos);
            const uint16_t len = fin->getU16();
            fin->read(compressBuffer.data(), len);

            unsigned long destLen = blockSize;
            const int ret = uncompress(decompressBuffer.data(), &destLen, compressBuffer.data(), len);

            if (ret != Z_OK || destLen != blockSize)
                break;

            memcpy(&block.getTiles(), decompressBuffer.data(), blockSize);
            block.mustUpdate();
            block.justSaw();
        }

        fin->close();
        return true;
    } catch (const stdext::exception& e) {
        g_logger.error("Failed to load OTMM minimap: {}", e.what());
        return false;
    }
}

void Minimap::saveOtmm(const std::string& fileName)
{
    try {
        const FileStreamPtr fin = tibia::files::data().createFile(fileName);
        fin->cache();

        //TODO: compression flag with zlib
        constexpr uint32_t flags = 0;

        // header
        fin->addU32(OTMM_SIGNATURE);
        fin->addU16(0); // data start, will be overwritten later
        fin->addU16(OTMM_VERSION);
        fin->addU32(flags);

        // version 1 header
        fin->addString("OTMM 1.0"); // description

        // go back and rewrite where the map data starts
        const uint32_t start = fin->tell();
        fin->seek(4);
        fin->addU16(start);
        fin->seek(start);

        constexpr uint32_t blockSize = MMBLOCK_SIZE * MMBLOCK_SIZE * sizeof(MinimapTile);
        constexpr uint32_t COMPRESS_LEVEL = 3;
        std::vector<uint8_t> compressBuffer(compressBound(blockSize));

        for (uint_fast8_t z = 0; z <= g_gameConfig.getMapMaxZ(); ++z) {
            for (const auto& [index, block] : m_tileBlocks[z]) {
                if (!block->wasSeen())
                    continue;

                const auto& pos = getIndexPosition(index, z);
                fin->addU16(pos.x);
                fin->addU16(pos.y);
                fin->addU8(pos.z);

                unsigned long len = blockSize;
                compress2(compressBuffer.data(), &len, (uint8_t*)&block->getTiles(), blockSize, COMPRESS_LEVEL);
                fin->addU16(len);
                fin->write(compressBuffer.data(), len);
            }
        }

        // end of file
        constexpr Position invalidPos;
        fin->addU16(invalidPos.x);
        fin->addU16(invalidPos.y);
        fin->addU8(invalidPos.z);

        fin->flush();

        fin->close();
    } catch (const stdext::exception& e) {
        g_logger.error("Failed to save OTMM minimap: {}", e.what());
    }
}