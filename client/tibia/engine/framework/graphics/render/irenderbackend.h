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

#include "renderframe.h"

#include <functional>

class Texture;

/*
 * IRenderBackend - the consumer side of the renderer boundary.
 *
 * A backend receives a RenderFrame and produces pixels. It is told WHAT to draw and decides
 * HOW; it knows nothing about pools, widgets, map floors or Lua.
 *
 * Resource plane. On OpenGL, `Texture` and `FrameBuffer` own their GL names and the GL backend
 * only resolves handles to them, so every resource hook below defaults to "nothing to do". A
 * backend that owns its native objects (WebGPU) receives pixels through `uploadTexture`,
 * creates size-only textures and render targets lazily from the handles a frame names, and
 * releases them through `destroyTexture`.
 */
class IRenderBackend
{
public:
    // RGBA8, top-left origin, whatever the backend stores natively.
    using ReadCallback = std::function<void(const ImagePtr&)>;

    virtual ~IRenderBackend() = default;

    // False means this backend cannot run here and the caller must choose another; it must
    // leave nothing half-initialised behind.
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;

    // Drawable size in PIXELS, not points. The platform layer already reports backing pixels.
    virtual void resize(const Size& drawableSize) = 0;

    // Encode and submit one frame. Returning false leaves the frame's side effects (atlas
    // composites) pending for the next one.
    virtual bool render(const RenderFrame& frame) = 0;

    // False while a device is still being acquired (the browser's WebGPU adapter and device
    // arrive asynchronously). Frames are not assembled until it turns true.
    [[nodiscard]] virtual bool isReady() const { return true; }

    // True once acquisition has definitively failed; the caller falls back to another engine.
    [[nodiscard]] virtual bool hasFailed() const { return false; }

    // Whether render() presents the frame itself, so the window must not swap GL buffers.
    [[nodiscard]] virtual bool presentsFrames() const { return false; }

    // --- resource plane (backends that own native objects) --------------------------------
    virtual void uploadTexture(const Texture& /*texture*/, const ImagePtr& /*image*/) {}
    virtual void destroyTexture(uint32_t /*uniqueId*/) {}

    // Reads back the contents of a texture or render-target texture (by Texture unique id), or
    // the presented frame when `uniqueId` is 0. The callback runs on the render thread, possibly
    // a frame later; it receives null when the backend cannot read back.
    virtual void readTexture(uint32_t /*uniqueId*/, const ReadCallback& done) { done(nullptr); }

    [[nodiscard]] virtual const char* name() const = 0;
};
