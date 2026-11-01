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

#include "global.h"

#include <algorithm>
#include <cmath>

// Short-axis visible SQM → landscape/portrait visible dim + stream/aware radius.
// radius = ceil((visible - 1) / 2) + 1  (ceil covers a half-edge tile; +1 walk preload)
namespace MapViewport
{
    constexpr int MAX_ASPECT_NUM = 21; // 21:9 / 9:21 ceiling
    constexpr int MAX_ASPECT_DEN = 9;

    inline int streamRadius(const double visible)
    {
        if (visible < 1.0)
            return 1;
        const double half = (visible - 1.0) / 2.0;
        return static_cast<int>(std::ceil(half)) + 1;
    }

    // Integer form of streamRadius(visibleNum / visibleDen) for constexpr use.
    constexpr int streamRadiusRational(const int visibleNum, const int visibleDen = 1)
    {
        if (visibleNum < visibleDen)
            return 1;
        // ceil((num/den - 1) / 2) + 1 = (num + den - 1) / (2*den) + 1
        return (visibleNum + visibleDen - 1) / (2 * visibleDen) + 1;
    }

    inline int maxAwareRadius(const int visibleSqm)
    {
        return streamRadiusRational(visibleSqm * MAX_ASPECT_NUM, MAX_ASPECT_DEN);
    }

    inline Size maxAwareViewPort(const int visibleSqm)
    {
        const int r = maxAwareRadius(visibleSqm);
        return { r, r };
    }

    // MapView centers the player on a tile — visible W/H must be odd or camera/cursor skew by 1 SQM.
    inline int ensureOdd(int n)
    {
        n = std::max(3, n);
        if (n % 2 == 0)
            ++n;
        return n;
    }

    // shortAxis locks the smaller panel edge; the other follows panel aspect.
    // Display dims are forced odd for MapView centering; stream radius still uses the
    // configured short axis (ceil half + 1) via maxAwareRadius / Crystal MAP_VISIBLE_SQM.
    inline Size visibleDimension(const int shortAxis, const Size& panelSize)
    {
        const int axis = ensureOdd(shortAxis);
        if (panelSize.width() <= 0 || panelSize.height() <= 0)
            return { axis, axis };

        if (panelSize.width() >= panelSize.height()) {
            const double ratio = static_cast<double>(panelSize.width()) / panelSize.height();
            const int w = ensureOdd(static_cast<int>(std::lround(shortAxis * ratio)));
            return { w, axis };
        }

        const double ratio = static_cast<double>(panelSize.height()) / panelSize.width();
        const int h = ensureOdd(static_cast<int>(std::lround(shortAxis * ratio)));
        return { axis, h };
    }
}
