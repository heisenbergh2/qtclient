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

// Height stamps: what a thing's art is read as in 3D, for sun shadows and raycast lighting.
// Depends on nothing but the geometry types, so tools/shadows builds this same source.

#include <framework/util/point.h>
#include <framework/util/rect.h>
#include <framework/util/size.h>

#include <cstddef>
#include <cstdint>

// Where a light sits in the height maps, in sprite px: `height` above its ground point, which is
// `ground` down-right of the light's screen position; rays leave it past `clear` (its own lamp,
// bowl or wall). A negative height means a creature-like source. Only the raycast mode reads it.
// A loose light is a movable item's, one anyone can drop or push there (a lit torch on the
// floor): it ranks after the map's and creatures' lights (RayLighting::pack). A transient light
// is an effect's or a missile's: it ranks after the map's lights and groups only with its kind,
// so a crowd's spells neither push out nor move the map's lights.
struct LightShape
{
    float height{ -1.f };
    float ground{ 0.f };
    float clear{ 6.f };
    bool loose{ false };
    bool transient{ false };
};

// RGBA8 pixels, row-major.
struct PixelGrid
{
    uint8_t* data{ nullptr };
    int width{ 0 };
    int height{ 0 };

    uint8_t* at(const int x, const int y) const { return data + (static_cast<size_t>(y) * width + x) * 4; }
};

// What a thing's art is read as (lighting stamps + climate rain splash).
enum class ThingShape : uint8_t
{
    Auto,  // worked out from the art (any of the below)
    Flat,  // lies on the floor: surface only (shade on the art), no height columns
    Wall,  // built into the tile grid: stands on the tile's far edges (walls, counters, tables)
    Box,   // stands on its contacts; its top face shows how deep it is (chests, cabinets, barrels)
    Round, // stands on its contacts, each height a disc (poles, trees, statues)
    Window, // a wall with an opening light passes through (open windows; their art is not see-through)
    Ridge, // solid floor→tip under silhouette (Round SE depth); floor surface blends with Flat
};

// One frame of a thing and where its stamps go.
struct StampFrame
{
    PixelGrid sprite;
    Rect frame;        // in sprite
    PixelGrid columns;  // heights standing on each ground pixel
    PixelGrid floating; // those above a gap over the ground-attached ones; without it, columns
                        // keeps only the topmost span
    PixelGrid surface;  // height of each drawn pixel
    Point origin;      // of the frame's region in columns and surface: its size plus pad
    int pad{ 0 };
    int cellPx{ 32 };  // texels per tile
    int hdScale{ 1 };  // texels per sprite px
    bool creature{ false };
    bool onTop{ false };
    int elevation{ 0 };
    Point displacement;
    ThingShape shape{ ThingShape::Auto }; // ignored on a creature
};

struct StampResult
{
    ThingShape shape{ ThingShape::Auto }; // what the frame was read as (Auto on a creature)
    bool floats{ false };                   // wrote any floating span
};

StampResult writeHeightStampFrame(const StampFrame& stamp);

// Where a thing's frames go in its stamp images. Only the first layer is stamped (the others
// are a creature's outfit masks), and ThingType::getTextureIndex lays the layers out one after
// another, so its frames are the texture's first ones, `columns` to a row as in the texture,
// each `frameSize` plus `pad`.
struct StampLayout
{
    int columns{ 1 };
    Size frameSize;
    int pad{ 0 };

    static int stampedFrames(int indexSize, int textureLayers);
    Size imageSize(int frames) const;
    Rect cell(int frame) const;
};

struct FrameLight
{
    PointF point; // logical px from the frame's top-left
    LightShape shape;
};

// `surface` is the frame's stamp (nullptr without one); `hook` is -1 unless wall-hung.
FrameLight findFrameLight(const PixelGrid& sprite, const Rect& frame, const PixelGrid* surface, const Point& origin,
                          int hdScale, int hook, const Point& displacement);
