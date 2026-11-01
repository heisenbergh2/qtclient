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

#include "lightfader.h"

#include <algorithm>
#include <cmath>

void LightFader::begin(LightSink* sink, const Position& camera, const uint16_t tileSize)
{
    const ticks_t now = stdext::millis();
    m_active = sink->gathersLights();
    m_snap = !m_active || sink != m_sink || tileSize != m_tileSize || camera.z != m_camera.z
        || std::abs(camera.x - m_camera.x) > 1 || std::abs(camera.y - m_camera.y) > 1
        || now - m_lastMillis > SNAP_AFTER_MS;
    m_elapsed = static_cast<float>(now - m_lastMillis);
    m_lastMillis = now;
    m_sink = sink;
    m_camera = camera;
    m_tileSize = tileSize;
    // Lights are gathered in map px around the camera's tile; they are matched in px of the
    // world, which stay put as the camera steps.
    m_origin = Point(camera.x * tileSize, camera.y * tileSize);
    m_floorOpen = false;
    m_floor.clear();

    m_previous.swap(m_current);
    m_current.clear();
    if (m_snap)
        m_previous.clear();
    m_byCell.clear();
    for (uint32_t i = 0; i < m_previous.size(); ++i) {
        const auto& light = m_previous[i];
        m_byCell.emplace_back(keyOf(cellOf(light.world), light), i);
    }
    std::ranges::sort(m_byCell);
    m_claimed.assign(m_previous.size(), 0);
}

void LightFader::nextFloor(const uint8_t z, const bool cameraFloor)
{
    if (m_floorOpen)
        flush();
    m_floorOpen = true;
    m_z = z;
    m_sink->beginFloor(cameraFloor);
}

void LightFader::end()
{
    if (m_floorOpen)
        flush();
    m_floorOpen = false;
}

void LightFader::addLightSource(const Point& pos, const Light& light, const float brightness, const LightShape& shape)
{
    if (!m_active || light.intensity == 0)
        return;
    m_floor.push_back({ .world = pos + m_origin, .light = light, .brightness = brightness, .shape = shape, .z = m_z });
}

Point LightFader::cellOf(const Point& world) const
{
    // Cells as wide as the match reach: a match is always in the light's cell or one around it.
    const int size = MATCH_TILES * m_tileSize;
    const auto cell = [size](const int v) { return (v < 0 ? v - size + 1 : v) / size; };
    return { cell(world.x), cell(world.y) };
}

uint64_t LightFader::keyOf(const Point& cell, const Tracked& light) const
{
    // Only a light of its kind continues one: a spell landing by a torch must not take the
    // torch's place and leave the torch to fade in again.
    const uint64_t kind = light.shape.height < 0.f ? 0 : light.shape.transient ? 2 : 1;
    return static_cast<uint64_t>(static_cast<uint16_t>(cell.x)) | static_cast<uint64_t>(static_cast<uint16_t>(cell.y)) << 16
        | static_cast<uint64_t>(light.light.color) << 32 | static_cast<uint64_t>(light.z) << 40 | kind << 48;
}

void LightFader::flush()
{
    const int reach = MATCH_TILES * m_tileSize;
    const int64_t reachSq = static_cast<int64_t>(reach) * reach;
    for (auto& light : m_floor) {
        int best = -1;
        if (!m_snap) {
            int64_t bestSq = reachSq;
            const Point cell = cellOf(light.world);
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const uint64_t key = keyOf(cell + Point(dx, dy), light);
                    for (auto it = std::ranges::lower_bound(m_byCell, std::pair<uint64_t, uint32_t>(key, 0));
                         it != m_byCell.end() && it->first == key; ++it) {
                        if (m_claimed[it->second])
                            continue;
                        const Point d = m_previous[it->second].world - light.world;
                        const int64_t distSq = static_cast<int64_t>(d.x) * d.x + static_cast<int64_t>(d.y) * d.y;
                        if (distSq <= bestSq) {
                            bestSq = distSq;
                            best = static_cast<int>(it->second);
                        }
                    }
                }
            }
        }
        const float from = best >= 0 ? m_previous[best].level : 0.f;
        if (best >= 0)
            m_claimed[best] = 1;
        light.level = m_snap ? 1.f : std::min(1.f, from + m_elapsed / FADE_IN_MS);
        emit(light);
        m_current.push_back(light);
    }
    m_floor.clear();

    if (m_snap)
        return;
    for (uint32_t i = 0; i < m_previous.size() && m_current.size() < MAX_TRACKED; ++i) {
        if (m_claimed[i] || m_previous[i].z != m_z)
            continue;
        Tracked light = m_previous[i];
        light.level -= m_elapsed / FADE_OUT_MS;
        if (light.level <= 0.f)
            continue;
        emit(light);
        m_current.push_back(light);
    }
}

void LightFader::emit(const Tracked& light)
{
    // Eased, in 64 steps: a fade changes what the sink packs or paints at most that often.
    const float eased = light.level * light.level * (3.f - 2.f * light.level);
    const float brightness = std::round(light.brightness * eased * 64.f) / 64.f;
    if (brightness > 0.f)
        m_sink->addLightSource(light.world - m_origin, light.light, brightness, light.shape);
}
