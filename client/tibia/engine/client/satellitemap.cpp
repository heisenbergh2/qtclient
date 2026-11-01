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

#include "satellitemap.h"

#include <framework/core/filestream.h>
#include <framework/core/resourcemanager.h>
#include <framework/core/logger.h>
#include <framework/graphics/image.h>
#include <framework/graphics/texture.h>
#include <framework/const.h>
#include <framework/core/clock.h>
#include <algorithm>
#include <cmath>

SatelliteMap g_satelliteMap;

void SatelliteMap::clear()
{
    for (auto& v : m_placements)
        v.clear();
    m_textures.clear();
    m_maskTextures.clear();
    m_maskImages.clear();
    m_indexLoaded = false;
    m_dir.clear();
}

const ImagePtr& SatelliteMap::getMaskImage(const int maskId)
{
    const auto it = m_maskImages.find(maskId);
    if (it != m_maskImages.end())
        return it->second;

    ImagePtr img;
    const std::string file = m_dir + "/satellite/zones/" + std::to_string(maskId);
    // Zone masks are optional (not all areas ship a sid PNG); skip Image::load so missing
    // files do not spam "Unable to load image" errors on area select.
    if (tibia::files::data().fileExists(tibia::files::data().guessFilePath(file, "png"))) {
        img = Image::load(file);
    }
    return m_maskImages.emplace(maskId, img).first->second;
}

bool SatelliteMap::maskContains(const int sid, const int rx, const int ry, const int wx, const int wy)
{
    const ImagePtr& img = getMaskImage(sid);
    if (!img)
        return false;

    const int px = wx - rx;
    const int py = wy - ry;
    if (px < 0 || py < 0 || px >= img->getWidth() || py >= img->getHeight())
        return false;

    // Mask is 1px per world tile, RGBA; alpha marks the shape's interior.
    return img->getPixel(px, py)[3] > 40;
}

bool SatelliteMap::ensureIndex(const std::string& assetsDir)
{
    if (m_indexLoaded && m_dir == assetsDir)
        return true;

    // Different data set: drop everything and reload.
    for (auto& v : m_placements)
        v.clear();
    m_textures.clear();
    m_indexLoaded = false;
    m_dir = assetsDir;

    const std::string path = assetsDir + "/satellite/index.dat";

    FileStreamPtr fin;
    try {
        fin = tibia::files::data().openFile(path);
    } catch (const std::exception&) {
        return false; // satellite data not shipped for this data set
    }
    if (!fin)
        return false;

    try {
        fin->cache();
        if (fin->getU8() != 'S' || fin->getU8() != 'A' || fin->getU8() != 'T' || fin->getU8() != 'L') {
            fin->close();
            return false;
        }
        fin->getU8(); // version
        const uint8_t r = fin->getU8();
        const uint8_t g = fin->getU8();
        const uint8_t b = fin->getU8();
        m_ocean = Color(static_cast<int>(r), static_cast<int>(g), static_cast<int>(b), 255);
        fin->getU32(); fin->getU32(); fin->getU32(); fin->getU32(); // world bounds (reserved)

        const uint32_t count = fin->getU32();
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t floor = fin->getU8();
            SatellitePlacement p;
            // NOTE: use getU32 (readULE32), not get32 — stdext::readSLE32 sign-extends the low
            // 16 bits and corrupts any coord >= 0x8000 (e.g. Venore/Edron at x >= 32768).
            p.x = static_cast<int32_t>(fin->getU32());
            p.y = static_cast<int32_t>(fin->getU32());
            p.w = fin->getU16();
            p.h = fin->getU16();
            p.layer = fin->getU8();
            p.pngId = fin->getU16();
            if (floor < m_placements.size())
                m_placements[floor].push_back(p);
        }
        fin->close();
    } catch (const std::exception& e) {
        g_logger.error("SatelliteMap: failed to parse {}: {}", path, e.what());
        for (auto& v : m_placements)
            v.clear();
        return false;
    }

    m_indexLoaded = true;
    return true;
}

bool SatelliteMap::loadFloors(const std::string& assetsDir, int /*minFloor*/, int /*maxFloor*/)
{
    // The whole index is small; textures load lazily (only visible tiles) in draw().
    return ensureIndex(assetsDir);
}

bool SatelliteMap::hasChunksForView(const int floor)
{
    if (!m_indexLoaded || floor < 0 || floor > 7 || floor >= static_cast<int>(m_placements.size()))
        return false;
    // Surface view stacks the current floor over everything down to ground (7),
    // so it is available if any floor in [floor..7] carries chunks.
    for (int f = floor; f <= 7 && f < static_cast<int>(m_placements.size()); ++f)
        if (!m_placements[f].empty())
            return true;
    return false;
}

const TexturePtr& SatelliteMap::getTile(const uint16_t pngId)
{
    const auto it = m_textures.find(pngId);
    if (it != m_textures.end())
        return it->second;

    TexturePtr tex;
    const std::string file = m_dir + "/satellite/" + std::to_string(pngId); // guessFilePath appends .png
    ImagePtr img;
    try {
        img = Image::load(file);
    } catch (const std::exception&) {
        img = nullptr;
    }
    if (img) {
        tex = std::make_shared<Texture>(img, false, false);
        tex->enableMipmaps(); // clean minification when zoomed out (no downscale aliasing/blur)
        tex->setSmooth(false); // keep nearest for crisp magnification -> GL_NEAREST_MIPMAP_NEAREST
    } else {
        g_logger.warning("SatelliteMap: failed to load tile {} ({})", pngId, file);
    }
    return m_textures.emplace(pngId, tex).first->second;
}

const TexturePtr& SatelliteMap::getMask(const int maskId)
{
    const auto it = m_maskTextures.find(maskId);
    if (it != m_maskTextures.end())
        return it->second;

    TexturePtr tex;
    const std::string file = m_dir + "/satellite/zones/" + std::to_string(maskId);
    ImagePtr img;
    // Same as getMaskImage: optional asset; avoid error spam when the PNG is absent.
    if (tibia::files::data().fileExists(tibia::files::data().guessFilePath(file, "png"))) {
        img = Image::load(file);
    }
    if (img) {
        tex = std::make_shared<Texture>(img, false, false);
        tex->setSmooth(false);
    }
    return m_maskTextures.emplace(maskId, tex).first->second;
}

void SatelliteMap::draw(const Rect& screenRect, const Position& mapCenter, const float scale, const float opacity)
{
    (void)screenRect; (void)mapCenter; (void)scale; (void)opacity;
}

