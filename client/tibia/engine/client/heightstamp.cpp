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

#include "heightstamp.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace {

// How the art is read, in sprite px.
constexpr int MAX_DEPTH = 16; // deepest a round thing gets
// Round chords are halved: full ones smear every slice along the view, so a low sun loses a
// cypress's thin stem.
constexpr double CHORD_SCALE = 0.5;
constexpr int SPAN_GAP = 4; // between heights that still belong to one span
// A window's opening (ThingShape::Window), sprite px: between the sill and the lintel, past
// the jambs at either end of its tile (where open shutters are drawn).
constexpr int WINDOW_SILL = 8;
constexpr int WINDOW_LINTEL = 24;
constexpr int WINDOW_JAMB = 4;
// Columns B on a creature: under the roof marks (DrawPool::addShadowRoof, B > 0.5) and over 0.1.
constexpr uint8_t CREATURE_MARK = 0x40;

// The encoded heights (0-255) standing on one column block.
struct Heights
{
    std::array<uint64_t, 4> words{};

    void set(const int v) { words[v >> 6] |= uint64_t{ 1 } << (v & 63); }
    // Every height from the floor up to v.
    void setUpTo(const int v)
    {
        for (int w = 0; w < 4; ++w) {
            const int last = v - w * 64;
            words[w] |= last >= 63 ? ~uint64_t{ 0 } : last >= 0 ? (uint64_t{ 2 } << last) - 1 : 0;
        }
    }
    Heights& operator|=(const Heights& other)
    {
        for (int w = 0; w < 4; ++w)
            words[w] |= other.words[w];
        return *this;
    }
    bool test(const int v) const { return (words[v >> 6] >> (v & 63)) & 1; }
    bool none() const { return (words[0] | words[1] | words[2] | words[3]) == 0; }
    int highest() const
    {
        for (int w = 3; w >= 0; --w) {
            if (words[w])
                return w * 64 + 63 - std::countl_zero(words[w]);
        }
        return -1;
    }
    int lowest() const
    {
        for (int w = 0; w < 4; ++w) {
            if (words[w])
                return w * 64 + std::countr_zero(words[w]);
        }
        return -1;
    }
};

// Per-thread working buffers: textures load on the async dispatcher's threads, and stamping
// every frame of a thing anew would allocate them all again each time.
struct Scratch
{
    std::vector<uint8_t> opaque, centred, solid, grouped, foot;
    std::vector<uint16_t> run, ahead;
    std::vector<int> z, columnTop;
    std::vector<Heights> standing;
};

template<typename T>
std::vector<T>& reset(std::vector<T>& buffer, const size_t size, const T& value = T{})
{
    buffer.assign(size, value);
    return buffer;
}

} // namespace

// Writes one frame's sun-shadow stamps at `origin` in `columns`, `floating` and `surface`
// (regions the frame's size plus `pad` below and right). The client draws height h above a
// ground pixel g at g - (h, h): every drawn pixel gets a height z, stands on the ground pixel
// p + (z, z) and casts its own shadow from there.
//
// Columns texel, at a ground pixel (logical px * 2, merged with max across things):
//   R  the highest drawn pixel of the span standing there from the floor up
//   G  255 - the lowest, so the merge keeps the lowest
//   B  CREATURE_MARK on a creature, whose own rays pass through it (0 on anything else)
// Floating texel: the same for the topmost span when a gap parts it from the floor one (or
// nothing stands near the floor there): a table top or a lamp head floats, and a ray passing
// under it stays lit.
// Surface texel, at a drawn pixel (painted in draw order):
//   R  255, for DrawPool::addShadowCover (a caster's own stamp masks it out)
//   G  the top of the column the pixel stands in, whole logical px * 2, + 1 on a creature
//      (its rays start past its own feet and pass through its own columns). Round uses G from
//      the pixel's own height (no wall rim) so FACE_SHADE does not light tips vs dark faces.
//   B  the pixel's own height, logical px * 2 (B < G on a Wall/Box/Window side face)
//   A  255 where the thing is drawn
//
// z = min(diagonal distance to the footprint, H). Items sit on their own tile: the frame's
// bottom-right cell, moved by the displacement the sprite is drawn up-left with. The shape
// (StampFrame::shape, Auto unless overridden) picks the reading:
// - Walls, things built into the tile grid, stand on the tile's far edges (the nearer one
//   along the diagonal), so the pieces of one table, counter or wall line up across seams: low
//   furniture drawn as a raised tile (H = elevation), top overlays such as arches, and walls
//   (contacts along the tile's far edge; H = how far the art rises above the tile, one tile
//   when it stays inside it). Furniture drawn well short of the tile's far edge (a bookcase)
//   stands on its drawn edge instead.
// - Creatures stand on the anti-diagonal through their tile's centre, round about it (below).
// - Boxes and round things (anything else) stand on their contacts (the bottom and right
//   outline nearest the floor), round ones with extra depth (below): a pixel whose diagonal
//   run ends on one stands there, any other on the middle of their hull.
//   H = the larger of the box minus the contacts' extent and the shortest front run at least
//   half the longest (knots on a pole or a branch end early); a block that holds things up
//   (a crate) is as tall as its elevation.
// - Flat things lie on the floor and stamp no columns.
// Without a floating target, columns keeps the topmost span in its place.
StampResult writeHeightStampFrame(const StampFrame& stamp)
{
    const auto& [sprite, frame, columns, floating, surface, origin, pad, cellPx, hdScale, creature, onTop, elevation,
                 displacement, requested] = stamp;
    ThingShape shape = creature ? ThingShape::Auto : requested;
    const int width = frame.width();
    const int height = frame.height();
    const int cellX = width - cellPx + displacement.x * hdScale;
    const int cellY = height - cellPx + displacement.y * hdScale;
    const auto at = [width](const int x, const int y) { return static_cast<size_t>(y) * width + x; };
    const auto encode = [hdScale](const int px) {
        const float logical = static_cast<float>(px) / static_cast<float>(hdScale);
        return static_cast<uint8_t>(std::clamp<int>(static_cast<int>(std::lround(logical * 2.f)), 0, 255));
    };
    const auto putSurface = [&](const int x, const int y, const int top, const int up, const bool figure) {
        uint8_t* texel = surface.at(origin.x + x, origin.y + y);
        const int wholeTop = std::min<int>(static_cast<int>(std::lround(static_cast<float>(top) / hdScale)), 127);
        texel[0] = 0xFF;
        texel[1] = static_cast<uint8_t>(wholeTop * 2 + (figure ? 1 : 0));
        texel[2] = encode(std::min(up, top));
        texel[3] = 0xFF;
    };

    thread_local Scratch scratch;
    const size_t area = static_cast<size_t>(width) * height;
    auto& opaque = reset(scratch.opaque, area);
    auto& run = reset(scratch.run, area);
    int minX = width, minY = height, maxX = -1, maxY = -1;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (sprite.at(frame.x() + x, frame.y() + y)[3] <= 0x20)
                continue;
            opaque[at(x, y)] = 1;
            run[at(x, y)] = (x > 0 && y > 0 ? run[at(x - 1, y - 1)] : 0) + 1;
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    }
    if (maxX < 0)
        return { ThingShape::Flat };

    // Distance from each drawn pixel down-right to the end of its run.
    auto& ahead = reset(scratch.ahead, area);
    for (int y = height - 1; y >= 0; --y) {
        for (int x = width - 1; x >= 0; --x) {
            if (opaque[at(x, y)] && x + 1 < width && y + 1 < height && opaque[at(x + 1, y + 1)])
                ahead[at(x, y)] = ahead[at(x + 1, y + 1)] + 1;
        }
    }

    auto& z = reset(scratch.z, area);
    auto& centred = reset(scratch.centred, area); // stands halfway across its contacts
    auto& solid = reset(scratch.solid, area);     // its run reaches a contact: solid down to the floor
    int wallX = -1; // the far edges a wall stands on
    int wallY = -1;
    const auto forEachDrawn = [&](const auto& fn) {
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                if (opaque[at(x, y)])
                    fn(x, y);
            }
        }
    };

    if (shape == ThingShape::Flat) {
        forEachDrawn([&](const int x, const int y) { putSurface(x, y, 0, 0, false); });
        return { shape };
    }

    if (creature) {
        // The tile's centre, not the lowest drawn pixel: outfits reach past it toward the
        // tile's far corner.
        const int feet = cellX + cellY + cellPx;
        forEachDrawn([&](const int x, const int y) {
            z[at(x, y)] = std::max((feet - x - y) / 2, 0);
            centred[at(x, y)] = 1;
        });
    } else {
        // Contacts: drawn pixels with nothing drawn below or down-right, in 8-connected
        // groups. A group stands on the floor when its lowest point is near the lowest one,
        // along the diagonal or in screen rows (a chest behind the coins spilling out of it).
        // The straight right outline above a standing group's rightmost pixel stands too.
        const int diagonalTolerance = 14 * hdScale;
        const int rowTolerance = 4 * hdScale;
        const auto isContact = [&](const int x, const int y) {
            if (!opaque[at(x, y)] || y + 1 >= height)
                return opaque[at(x, y)] != 0;
            return !opaque[at(x, y + 1)] && (x + 1 >= width || !opaque[at(x + 1, y + 1)]);
        };

        auto& grouped = reset(scratch.grouped, area);
        std::vector<std::vector<Point>> groups;
        int lowestDiagonal = 0;
        int lowestRow = 0;
        forEachDrawn([&](const int x, const int y) {
            if (grouped[at(x, y)] || !isContact(x, y))
                return;
            auto& group = groups.emplace_back();
            std::vector<Point> pending{ Point(x, y) };
            grouped[at(x, y)] = 1;
            while (!pending.empty()) {
                const Point p = pending.back();
                pending.pop_back();
                group.push_back(p);
                lowestDiagonal = std::max(lowestDiagonal, p.x + p.y);
                lowestRow = std::max(lowestRow, p.y);
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = p.x + dx;
                        const int ny = p.y + dy;
                        if (nx < 0 || ny < 0 || nx >= width || ny >= height || grouped[at(nx, ny)] || !isContact(nx, ny))
                            continue;
                        grouped[at(nx, ny)] = 1;
                        pending.emplace_back(nx, ny);
                    }
                }
            }
        });

        auto& foot = reset(scratch.foot, area);
        std::vector<std::pair<double, double>> edges; // (x - y, x + y) of each contact's floor edge
        for (const auto& group : groups) {
            int diagonal = 0;
            int row = 0;
            Point right(-1, 0);
            for (const auto& p : group) {
                diagonal = std::max(diagonal, p.x + p.y);
                row = std::max(row, p.y);
                if (p.x > right.x || (p.x == right.x && p.y < right.y))
                    right = p;
            }
            if (diagonal < lowestDiagonal - diagonalTolerance && row < lowestRow - rowTolerance)
                continue;
            for (const auto& p : group) {
                foot[at(p.x, p.y)] = 1;
                edges.emplace_back(p.x - p.y - 0.5, p.x + p.y + 1.5);
            }
            for (int y = right.y; y > 0 && opaque[at(right.x, y - 1)]
                 && (right.x + 1 >= width || !opaque[at(right.x + 1, y - 1)]);) {
                --y;
                foot[at(right.x, y)] = 1;
                edges.emplace_back(right.x - y + 0.5, right.x + y + 1.5);
            }
        }

        // Contacts along the far right column or bottom row of a tile at (tileX, tileY): a wall
        // standing on the tile's edge.
        const auto edgeRunAt = [&](const int tileX, const int tileY) {
            int best = 0;
            const auto measure = [&](int x, int y, const int dx, const int dy) {
                for (int length = 0; x < width && y < height; x += dx, y += dy) {
                    length = foot[at(x, y)] ? length + 1 : 0;
                    best = std::max(best, length);
                }
            };
            if (tileX + cellPx == width)
                measure(width - 1, std::max(tileY, 0), 0, 1);
            if (tileY + cellPx == height)
                measure(std::max(tileX, 0), height - 1, 1, 0);
            return best;
        };

        // How far the art reaches above its tile: a railing is as tall as it is drawn, not
        // a whole tile.
        const int rise = std::max(cellX - minX, cellY - minY);
        int standX = cellX;
        int standY = cellY;
        int tileHeight = 0;
        if (elevation > 0) {
            const int block = elevation * hdScale;
            const int gapX = std::max(cellX + cellPx - 1 - maxX, 0);
            const int gapY = std::max(cellY + cellPx - 1 - maxY, 0);
            if (rise >= block - hdScale && rise <= block + 4 * hdScale) {
                tileHeight = block;
            } else if (std::max(gapX, gapY) >= cellPx / 4 && edgeRunAt(cellX - gapX, cellY - gapY) >= cellPx / 2) {
                // Furniture drawn well short of its tile's far edge (a bookcase against a
                // wall) stands on the edge it is drawn on.
                standX = cellX - gapX;
                standY = cellY - gapY;
                tileHeight = std::clamp(std::max(rise, block), 1, cellPx);
            }
        } else if (onTop || edgeRunAt(cellX, cellY) >= cellPx / 2) {
            tileHeight = rise >= cellPx / 4 ? std::min(rise, cellPx) : cellPx;
        }
        if ((shape == ThingShape::Wall || shape == ThingShape::Window) && tileHeight == 0)
            tileHeight = rise >= cellPx / 4 ? std::min(rise, cellPx) : cellPx;
        else if (shape == ThingShape::Box || shape == ThingShape::Round || shape == ThingShape::Ridge)
            tileHeight = 0;

        if (tileHeight > 0) {
            if (shape != ThingShape::Window)
                shape = ThingShape::Wall;
            wallX = standX + cellPx - 1;
            wallY = standY + cellPx - 1;
            forEachDrawn([&](const int x, const int y) {
                z[at(x, y)] = std::clamp(std::min(standX + cellPx - 1 - x, standY + cellPx - 1 - y), 0, tileHeight);
            });
        } else {
            // Where each diagonal s = x - y crosses the contacts' hull (in s, u = x + y): a
            // pixel whose run does not end on a contact stands halfway across.
            std::ranges::sort(edges);
            edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
            // The art alone does not tell a box from a round thing (a fence panel and a lamp
            // both stand on contacts across their width): round unless an override says box.
            if (shape == ThingShape::Auto)
                shape = ThingShape::Round;
            const auto cross = [](const auto& o, const auto& a, const auto& b) {
                return (a.first - o.first) * (b.second - o.second) - (a.second - o.second) * (b.first - o.first);
            };
            std::vector<std::pair<double, double>> hull;
            if (edges.size() < 3) {
                hull = edges;
            } else {
                hull.resize(edges.size() * 2);
                size_t k = 0;
                for (const auto& p : edges) {
                    while (k >= 2 && cross(hull[k - 2], hull[k - 1], p) <= 0)
                        --k;
                    hull[k++] = p;
                }
                for (size_t i = edges.size() - 1, lower = k + 1; i-- > 0;) {
                    while (k >= lower && cross(hull[k - 2], hull[k - 1], edges[i]) <= 0)
                        --k;
                    hull[k++] = edges[i];
                }
                hull.resize(k - 1);
            }

            const double firstS = std::floor(edges.front().first);
            const int steps = static_cast<int>((std::ceil(edges.back().first) - firstS) * 2) + 1;
            std::vector<double> low(steps, std::numeric_limits<double>::infinity());
            std::vector<double> high(steps, -std::numeric_limits<double>::infinity());
            for (size_t i = 0; i < hull.size(); ++i) {
                const auto& a = hull[i];
                const auto& b = hull[(i + 1) % hull.size()];
                for (int k = 0; k < steps; ++k) {
                    const double s = firstS + k * 0.5;
                    if (s < std::min(a.first, b.first) || s > std::max(a.first, b.first))
                        continue;
                    double uLow = std::min(a.second, b.second);
                    double uHigh = std::max(a.second, b.second);
                    if (a.first != b.first)
                        uLow = uHigh = a.second + (s - a.first) * (b.second - a.second) / (b.first - a.first);
                    low[k] = std::min(low[k], uLow);
                    high[k] = std::max(high[k], uHigh);
                }
            }
            for (const auto& [s, u] : edges) {
                const int k = std::clamp<int>(static_cast<int>(std::lround((s - firstS) * 2)), 0, steps - 1);
                low[k] = std::min(low[k], u);
                high[k] = std::max(high[k], u);
            }
            std::vector<double> chainS;
            std::vector<double> chainU;
            for (int k = 0; k < steps; ++k) {
                if (std::isfinite(low[k])) {
                    chainS.push_back(firstS + k * 0.5);
                    chainU.push_back((low[k] + high[k]) / 2);
                }
            }
            const auto middle = [&](const double s) {
                if (s <= chainS.front())
                    return chainU.front();
                if (s >= chainS.back())
                    return chainU.back();
                const size_t i = std::upper_bound(chainS.begin(), chainS.end(), s) - chainS.begin();
                return chainU[i - 1] + (s - chainS[i - 1]) * (chainU[i] - chainU[i - 1]) / (chainS[i] - chainS[i - 1]);
            };

            // Capped at the larger of the box minus the contacts' extent (a box is its
            // footprint swept up-left by its height) and the shortest front run at least half
            // the longest.
            int footMinX = width, footMinY = height, footMaxX = -1, footMaxY = -1;
            int longest = 0;
            forEachDrawn([&](const int x, const int y) {
                if (foot[at(x, y)]) {
                    footMinX = std::min(footMinX, x);
                    footMinY = std::min(footMinY, y);
                    footMaxX = std::max(footMaxX, x);
                    footMaxY = std::max(footMaxY, y);
                }
                if (ahead[at(x, y)] == 0)
                    longest = std::max<int>(longest, run[at(x, y)]);
            });
            int frontRun = longest;
            forEachDrawn([&](const int x, const int y) {
                if (ahead[at(x, y)] == 0 && run[at(x, y)] * 2 >= longest)
                    frontRun = std::min<int>(frontRun, run[at(x, y)]);
            });
            // A block that holds things up (a crate) is as tall as they stand: 8 px, two stacked
            // 16. Its top face, about as deep as it is tall, reads as up to twice that; a thing
            // drawn taller (a chair's back, a statue on a plinth) keeps its art's height.
            const double footprintHeight = ((maxX - minX) - (footMaxX - footMinX) + (maxY - minY) - (footMaxY - footMinY)) / 2.0;
            double cap = std::max({ footprintHeight, static_cast<double>(frontRun), 1.0 });
            const double block = elevation * hdScale;
            if (block > 0 && cap <= block * 2)
                cap = std::min(cap, block);

            forEachDrawn([&](const int x, const int y) {
                const int length = ahead[at(x, y)];
                double h = length;
                if (!foot[at(std::min(x + length, width - 1), std::min(y + length, height - 1))]) {
                    h = std::max(h, (middle(x - y) - (x + y)) / 2.0);
                    centred[at(x, y)] = 1;
                } else {
                    solid[at(x, y)] = 1;
                }
                z[at(x, y)] = static_cast<int>(std::lround(std::min(h, cap)));
            });
        }
    }

    // Every drawn pixel stands on p + (z, z). The art only shows a thing's front: a round thing
    // is read as a disc at each height, as wide as its run of pixels across the view (along
    // x - y), so a pole stands on a disc rather than a line a low sun sees edge-on. A pixel
    // adds its chord of that disc along the view: behind it when it stands on a contact (the
    // disc's front), around it when it stands halfway across. A creature is round too, every
    // pixel halfway across, so a low sun sees a body rather than its edge.
    // A column keeps the spans of heights standing there that hang together: a lamp head
    // floats over its base and a ray passes under it, while the pole below stays solid. A
    // pixel whose run reaches a contact stands on a solid body (a crate's top face, cut off at
    // its height, has no art below it): its column fills from the floor up.
    // Columns are whole sprite pixels (hdScale texels square): a ray climbs one sprite pixel
    // per step, so a narrower column falls between two steps.
    const bool ridge = shape == ThingShape::Ridge;
    const bool round = creature || shape == ThingShape::Round || ridge;
    // A wall stands on a line one pixel thick; it also stands one pixel behind it (under its
    // own art), so the ray lights' occlusion march (two pixel steps) cannot step over it.
    const bool window = !creature && shape == ThingShape::Window;
    const int wallDepth = !creature && (shape == ThingShape::Wall || window) ? hdScale : 0;
    // A window's art paints its opening (sky, an open shutter's shadow) rather than leaving it
    // see-through: the middle of its tile stands no higher than the sill and floats from the
    // lintel up, so light passes between them.
    const auto inOpening = [&](const int x, const int y, const int h) {
        if (!window || h <= WINDOW_SILL * hdScale || h >= WINDOW_LINTEL * hdScale)
            return false;
        // Along the edge it stands on: the nearer one along the diagonal, as z picked it.
        const int along = wallX - x <= wallY - y ? y + h - (wallY + 1 - cellPx) : x + h - (wallX + 1 - cellPx);
        return along >= WINDOW_JAMB * hdScale && along < cellPx - WINDOW_JAMB * hdScale;
    };
    const int stampWidth = width + pad;
    const int stampHeight = height + pad;
    const int blocksWide = (stampWidth + hdScale - 1) / hdScale;
    const int blocksHigh = (stampHeight + hdScale - 1) / hdScale;
    auto& standing = reset(scratch.standing, static_cast<size_t>(blocksWide) * blocksHigh);
    auto& columnTop = reset(scratch.columnTop, static_cast<size_t>(stampWidth) * stampHeight, -1);
    const auto markColumnTop = [&](const int gx, const int gy, const int h) {
        const int blockX = gx - gx % hdScale;
        const int blockY = gy - gy % hdScale;
        for (int by = blockY; by < std::min(blockY + hdScale, stampHeight); ++by) {
            for (int bx = blockX; bx < std::min(blockX + hdScale, stampWidth); ++bx) {
                auto& top = columnTop[static_cast<size_t>(by) * stampWidth + bx];
                top = std::max(top, h);
            }
        }
    };
    const auto roundChord = [&](const int x, const int y, const int h) {
        int reach[2] = { 0, 0 };
        for (int side = 0; side < 2; ++side) {
            const int sign = side ? -1 : 1;
            for (int k = 1;; ++k) {
                const int nx = x + sign * k;
                const int ny = y - sign * k;
                if (nx < 0 || ny < 0 || nx >= width || ny >= height || !opaque[at(nx, ny)]
                    || std::abs(z[at(nx, ny)] - h) > 1)
                    break;
                reach[side] = k;
            }
        }
        const double radius = (reach[0] + reach[1] + 1) / 2.0;
        const double offset = (reach[0] - reach[1]) / 2.0;
        // Half chords keep thin stems sharp under a low SE sun. Ridge shares this scale so
        // outdoor umbras follow the rock silhouette (full plan disc cast square blocks).
        return std::min(static_cast<int>(std::lround(CHORD_SCALE * std::sqrt(std::max(radius * radius - offset * offset, 0.0)))),
                        MAX_DEPTH * hdScale / 2);
    };
    forEachDrawn([&](const int x, const int y) {
        const int h = z[at(x, y)];
        Heights heights;
        // Ridge: solid floor→tip under the silhouette (cave lamps cannot clear the rock).
        // Footprint is Round-style SE depth — not an omnidirectional plan disc (that squared sun).
        if (ridge || solid[at(x, y)])
            heights.setUpTo(encode(h));
        else
            heights.set(encode(h));

        int chord = round ? roundChord(x, y, h) : 0;
        const int front = centred[at(x, y)] ? chord : 0;
        const bool open = inOpening(x, y, h);
        for (int d = -front; d <= 2 * chord - front + wallDepth; ++d) {
            const int gx = x + h - d;
            const int gy = y + h - d;
            if (gx < 0 || gy < 0)
                break;
            if (gx >= stampWidth || gy >= stampHeight)
                continue;
            if (!open)
                standing[static_cast<size_t>(gy / hdScale) * blocksWide + gx / hdScale] |= heights;
            if (d != 0)
                continue;
            markColumnTop(gx, gy, h);
        }
    });
    const auto writeSpan = [&](const PixelGrid& target, const int blockX, const int blockY, const int top, const int low) {
        for (int by = blockY * hdScale; by < std::min((blockY + 1) * hdScale, stampHeight); ++by) {
            for (int bx = blockX * hdScale; bx < std::min((blockX + 1) * hdScale, stampWidth); ++bx) {
                uint8_t* texel = target.at(origin.x + bx, origin.y + by);
                texel[0] = static_cast<uint8_t>(std::max(top, 1));
                texel[1] = static_cast<uint8_t>(255 - low);
                texel[2] = creature ? CREATURE_MARK : 0;
                texel[3] = 0xFF;
            }
        }
    };
    // With a floating target, the span hanging together from the floor up goes in columns and
    // the topmost one, when a gap parts them, in floating: things merge with max in each, so a
    // lamp head over a barrel stays apart from it. Spans between the two are dropped.
    bool floats = false;
    for (int blockY = 0; blockY < blocksHigh; ++blockY) {
        for (int blockX = 0; blockX < blocksWide; ++blockX) {
            const auto& heights = standing[static_cast<size_t>(blockY) * blocksWide + blockX];
            if (heights.none())
                continue;
            const int top = heights.highest();
            int low = top;
            for (int v = top - 1; v >= 0 && low - v <= SPAN_GAP * 2; --v) {
                if (heights.test(v))
                    low = v;
            }
            if (!floating.data) {
                writeSpan(columns, blockX, blockY, top, low);
                continue;
            }
            const int groundLow = heights.lowest();
            int groundTop = groundLow;
            for (int v = groundLow + 1; v <= 255 && v - groundTop <= SPAN_GAP * 2; ++v) {
                if (heights.test(v))
                    groundTop = v;
            }
            if (groundLow <= SPAN_GAP * 2)
                writeSpan(columns, blockX, blockY, groundTop, groundLow);
            if (groundLow > SPAN_GAP * 2 || low > groundTop) {
                writeSpan(floating, blockX, blockY, top, low);
                floats = true;
            }
        }
    }

    // Ridge surface stays floor-level so the ridge top blends with neighbouring Flat mountain;
    // solid columns under the silhouette (above) still block cave lamps.
    if (ridge) {
        forEachDrawn([&](const int x, const int y) { putSurface(x, y, 0, 0, false); });
        return { shape, floats };
    }

    // Round volumes (trees, statues) keep a high tip in `up` for occlusion, but do not mark a
    // wall-style side face under a rim: that FACE_SHADE split leaves tips fully lit while faces
    // go dark. Wall/Box/Window still use columnTop so fronts floor at FACE_SHADE. Creatures
    // already use top = h (and the figure mark skips face shade).
    forEachDrawn([&](const int x, const int y) {
        const int h = z[at(x, y)];
        const int gx = x + h;
        const int gy = y + h;
        const bool inside = gx < stampWidth && gy < stampHeight;
        const bool roundSurface = creature || shape == ThingShape::Round;
        const int top = creature || roundSurface || !inside ? h : columnTop[static_cast<size_t>(gy) * stampWidth + gx];
        putSurface(x, y, top, h, creature);
    });
    return { shape, floats };
}

int StampLayout::stampedFrames(const int indexSize, const int textureLayers)
{
    return textureLayers > 0 ? indexSize / textureLayers : 0;
}

Size StampLayout::imageSize(const int frames) const
{
    if (columns <= 0 || frames <= 0)
        return {};
    const int rows = (frames + columns - 1) / columns;
    return { columns * (frameSize.width() + pad), rows * (frameSize.height() + pad) };
}

Rect StampLayout::cell(const int frame) const
{
    const int cols = std::max(columns, 1);
    return { Point(frame % cols * (frameSize.width() + pad), frame / cols * (frameSize.height() + pad)),
             frameSize + Size(pad) };
}

// Where a frame's light comes from: its brightest pixels (a flame, glowing coals), weighted by
// how far their luminance rises over three quarters of the brightest one's. With the frame's
// stamp (`surface` at `origin`, things standing on the floor) the light floats a little above
// those pixels and its rays clear the thing's own footprint. A wall-hung light (`hook` >= 0: 1
// south wall, 2 east wall, 0 the nearer tile edge) sits just in front of the wall it hangs on,
// so the room sees it and the wall does not hide it. Anything else keeps height < 0.
FrameLight findFrameLight(const PixelGrid& sprite, const Rect& frame, const PixelGrid* surface, const Point& origin,
                          const int hdScale, const int hook, const Point& displacement)
{
    constexpr float LIFT = 4.f;
    constexpr float FRONT = 2.f;
    constexpr float OWN_LIFT = 1.f;
    constexpr float CLEAR_MIN = 6.f;
    constexpr float CLEAR_MARGIN = 2.f;
    constexpr float CLEAR_MAX = 24.f;
    // Under FRONT: hung things cast nothing, and its rays must not start past the wall it hangs on.
    constexpr float HUNG_CLEAR = 1.f;

    FrameLight light;
    const int width = frame.width();
    const int height = frame.height();
    thread_local std::vector<float> scratchLuminance;
    auto& luminance = reset(scratchLuminance, static_cast<size_t>(width) * height, -1.f);
    float brightest = 0.f;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const uint8_t* p = sprite.at(frame.x() + x, frame.y() + y);
            if (p[3] <= 0x20)
                continue;
            const float l = (0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2]) / 255.f;
            luminance[static_cast<size_t>(y) * width + x] = l;
            brightest = std::max(brightest, l);
        }
    }
    if (brightest <= 0.f)
        return light;

    const float scale = static_cast<float>(hdScale);
    const auto upAt = [&](const int x, const int y) {
        const uint8_t* texel = surface->at(origin.x + x, origin.y + y);
        return texel[3] ? texel[2] / 2.f : -1.f;
    };
    double total = 0, sumX = 0, sumY = 0, upTotal = 0, sumUp = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float rise = luminance[static_cast<size_t>(y) * width + x] - 0.75f * brightest;
            if (rise <= 0.f)
                continue;
            const double w = rise * rise;
            total += w;
            sumX += w * (x + 0.5);
            sumY += w * (y + 0.5);
            if (surface) {
                if (const float up = upAt(x, y); up >= 0.f) {
                    upTotal += w;
                    sumUp += w * up;
                }
            }
        }
    }
    if (total <= 0)
        return light;
    light.point = PointF(static_cast<float>(sumX / total), static_cast<float>(sumY / total)) / scale;

    if (upTotal > 0) {
        const auto up = static_cast<float>(sumUp / upTotal);
        light.shape.ground = up;
        const PointF ground = light.point + PointF(up, up);
        float reach = 0.f;
        float ownTop = 0.f;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const float pixelUp = luminance[static_cast<size_t>(y) * width + x] >= 0.f ? upAt(x, y) : -1.f;
                if (pixelUp < 0.f)
                    continue;
                const float dx = (x + 0.5f) / scale + pixelUp - ground.x;
                const float dy = (y + 0.5f) / scale + pixelUp - ground.y;
                reach = std::max(reach, dx * dx + dy * dy);
                ownTop = std::max(ownTop, std::floor(surface->at(origin.x + x, origin.y + y)[1] / 2.f));
            }
        }
        // Above its own rim (a basin's bowl), whose columns would otherwise cut a wedge
        // through the light right past the clear distance.
        light.shape.height = std::max(up + LIFT, ownTop + OWN_LIFT);
        light.shape.clear = std::clamp(std::sqrt(reach) + CLEAR_MARGIN, CLEAR_MIN, CLEAR_MAX);
    } else if (hook >= 0) {
        const float right = (width + displacement.x * hdScale) / scale;
        const float bottom = (height + displacement.y * hdScale) / scale;
        const float toSouth = bottom - light.point.y;
        const float toEast = right - light.point.x;
        const float wall = hook == 1 ? toSouth : hook == 2 ? toEast : std::min(toSouth, toEast);
        light.shape.height = light.shape.ground = std::max(wall, 0.f) + FRONT;
        light.shape.clear = HUNG_CLEAR;
    }
    return light;
}
