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

#include "lightsink.h"
#include "position.h"

#include <framework/stdext/time.h>

// Eases the lights MapView::drawLights gathers in and out over the frames, for either light
// sink: a light that appears (an effect, a creature walking into view) fades in, one that goes
// away fades out where it last stood, so a crowd's spells do not strobe. Lights carry no
// identity: a light continues the last frame's nearest unclaimed one of its colour and kind
// (creature, the map's, effect or missile) on its floor within MATCH_TILES (a missile in
// flight, a creature walking), and a new spell where one just ended stays lit. Each floor's
// lights are held and handed on, with the ones fading out, before the next floor's, so the
// sink's floor and shade order holds. A teleport, floor change or zoom shows the new lights at
// once.
class LightFader final : public LightSink
{
public:
    static constexpr float FADE_IN_MS = 100.f;
    static constexpr float FADE_OUT_MS = 250.f;
    static constexpr int MATCH_TILES = 2;
    // Lights fading out past this many lights in all are dropped (a crowd's spells ending at
    // once); the sink caps what it draws anyway.
    static constexpr size_t MAX_TRACKED = 2048;
    // A gap this long between gathers (lights off, the map thread stalled) shows lights at once.
    static constexpr ticks_t SNAP_AFTER_MS = 500;

    void begin(LightSink* sink, const Position& camera, uint16_t tileSize);
    void nextFloor(uint8_t z, bool cameraFloor);
    void end();

    void addLightSource(const Point& pos, const Light& light, float brightness = 1.f, const LightShape& shape = {}) override;
    void resetShade(const Point& pos) override { m_sink->resetShade(pos); }
    bool placesLights() const override { return m_sink->placesLights(); }

private:
    struct Tracked
    {
        Point world;
        Light light;
        float brightness{ 1.f };
        LightShape shape;
        uint8_t z{ 0 };
        float level{ 0.f };
    };

    void flush();
    void emit(const Tracked& light);
    uint64_t keyOf(const Point& cell, const Tracked& light) const;
    Point cellOf(const Point& world) const;

    LightSink* m_sink{ nullptr };
    Position m_camera;
    Point m_origin;
    uint16_t m_tileSize{ 0 };
    uint8_t m_z{ 0 };
    bool m_active{ false };
    bool m_floorOpen{ false };
    bool m_snap{ true };
    float m_elapsed{ 0.f };
    ticks_t m_lastMillis{ 0 };

    std::vector<Tracked> m_previous;
    std::vector<Tracked> m_current;
    std::vector<Tracked> m_floor;
    std::vector<std::pair<uint64_t, uint32_t>> m_byCell;
    std::vector<uint8_t> m_claimed;
};
