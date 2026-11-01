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

#include "declarations.h"
#include "glutil.h"

// Maps product graphics enums to the active OpenGL/GLES enumerants. Include
// only from translation units that issue native GL calls.
namespace glenums {

inline GLenum drawMode(const DrawMode mode)
{
    switch (mode) {
        case DrawMode::TRIANGLES: return GL_TRIANGLES;
        case DrawMode::TRIANGLE_STRIP: return GL_TRIANGLE_STRIP;
        case DrawMode::NONE:
        default: return GL_NONE;
    }
}

inline GLenum blendEquation(const BlendEquation equation)
{
    switch (equation) {
        case BlendEquation::MAX: return GL_MAX;
        case BlendEquation::MIN: return GL_MIN;
        case BlendEquation::SUBTRACT: return GL_FUNC_SUBTRACT;
        case BlendEquation::REVER_SUBTRACT: return GL_FUNC_REVERSE_SUBTRACT;
        case BlendEquation::ADD:
        default: return GL_FUNC_ADD;
    }
}

inline GLenum shaderType(const ShaderType type)
{
    switch (type) {
        case ShaderType::FRAGMENT: return GL_FRAGMENT_SHADER;
        case ShaderType::VERTEX:
        default: return GL_VERTEX_SHADER;
    }
}

// 1/2-channel images use RED/RG (core + GLES3). Legacy LUMINANCE formats are
// not part of either API surface we compile against.
inline bool channelsToFormat(const int channels, GLenum& format, GLenum& internalFormat)
{
    switch (channels) {
        case 4:
            format = GL_RGBA;
            internalFormat = GL_RGBA8;
            return true;
        case 3:
            format = GL_RGB;
            internalFormat = GL_RGB8;
            return true;
        case 2:
            format = GL_RG;
            internalFormat = GL_RG8;
            return true;
        case 1:
            format = GL_RED;
            internalFormat = GL_R8;
            return true;
        default:
            return false;
    }
}

} // namespace glenums
