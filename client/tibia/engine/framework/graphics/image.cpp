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

#include "image.h"

#include "apngloader.h"
#include "framework/core/filestream.h"
#include "framework/core/logger.h"
#include "framework/core/resourcemanager.h"

#include <array>
#include <lzma.h>

using namespace qrcodegen;

Image::Image(const Size& size, const int bpp, const uint8_t* pixels) : m_size(size), m_bpp(bpp)
{
    const int w = size.width();
    const int h = size.height();
    if (w <= 0 || h <= 0 || bpp <= 0) {
        m_size = Size(0, 0);
        m_bpp = bpp > 0 ? bpp : 4;
        return;
    }

    const size_t bytes = static_cast<size_t>(w) * static_cast<size_t>(h) * static_cast<size_t>(bpp);
    m_pixels.resize(bytes, 0);
    if (pixels)
        memcpy(m_pixels.data(), pixels, m_pixels.size());
}

ImagePtr Image::load(const std::string& file)
{
    if (file.ends_with(".bmp.lzma") || file.ends_with(".BMP.LZMA")) {
        return loadCipBmp(file);
    }
    const auto& path = tibia::files::data().guessFilePath(file, "png");
    try {
        return loadPNG(path);
    } catch (const stdext::exception& e) {
        g_logger.error("Unable to load image '{}': {}", path, e.what());
    }
    return nullptr;
}

ImagePtr Image::loadPNG(const char* data, const size_t size)
{
    std::stringstream fin(std::string{ data, size });
    ImagePtr image;
    if (apng_data apng; load_apng(fin, &apng) == 0) {
        const size_t frameSize = static_cast<size_t>(apng.width) * apng.height * apng.bpp;
        const uint32_t availableFrames = apng.last_frame > apng.first_frame
                                             ? apng.last_frame - apng.first_frame
                                             : 0;
        const uint32_t frameCount = std::min(apng.num_frames, availableFrames);
        const uint32_t firstFrame = availableFrames > 0 ? apng.first_frame : 0;
        const auto* firstFrameData = apng.pdata + (static_cast<size_t>(firstFrame) * frameSize);
        image = std::make_shared<Image>(Size(apng.width, apng.height), apng.bpp, firstFrameData);
        if (frameCount > 1 && apng.frames_delay) {
            for (uint32_t i = 0; i < frameCount; ++i) {
                // Create a new Image for every frame to avoid circular reference (image -> m_animation -> image)
                const size_t frameOffset = static_cast<size_t>(apng.first_frame + i) * frameSize;
                ImagePtr frameImage = std::make_shared<Image>(Size(apng.width, apng.height), apng.bpp,
                                                              apng.pdata + frameOffset);
                image->addAnimationFrame(frameImage, apng.frames_delay[i]);
            }
        }
        free_apng(&apng);
    }

    if (!image)
        return nullptr;

    int cntTransparentPixel = 0;
    for (const auto& pixel : image->getPixels()) {
        if (pixel == 0 && ++cntTransparentPixel == 4) {
            image->setTransparentPixel(true);
            break;
        }
    }

    return image;
}

ImagePtr Image::loadPNG(const std::string& file)
{
    std::stringstream fin;
    tibia::files::data().readFileStream(file, fin);

    const std::string buffer{ fin.str() };

    return loadPNG(buffer.data(), buffer.size());
}

void Image::savePNG(const std::string& fileName)
{
    const auto& fin = tibia::files::data().createFile(fileName);
    if (!fin)
        throw Exception("failed to open file '{}' for write", fileName);

    fin->cache();
    std::stringstream data;
    save_png(data, m_size.width(), m_size.height(), 4, getPixelData());
    fin->write(data.str().c_str(), data.str().length());
    fin->flush();
    fin->close();
}

void Image::overwriteMask(const Color& maskedColor, const Color& insideColor, const Color& outsideColor)
{
    assert(m_bpp == 4);

    for (int p = 0; p < getPixelCount(); ++p) {
        uint8_t& r = m_pixels[p * 4 + 0];
        uint8_t& g = m_pixels[p * 4 + 1];
        uint8_t& b = m_pixels[p * 4 + 2];
        uint8_t& a = m_pixels[p * 4 + 3];

        Color pixelColor(r, g, b, a);
        Color writeColor = (pixelColor == maskedColor) ? insideColor : outsideColor;

        r = writeColor.r();
        g = writeColor.g();
        b = writeColor.b();
        a = writeColor.a();
    }
}

void Image::overwrite(const Color& color)
{
    assert(m_bpp == 4);

    for (int p = 0; p < getPixelCount(); ++p) {
        uint8_t& r = m_pixels[p * 4 + 0];
        uint8_t& g = m_pixels[p * 4 + 1];
        uint8_t& b = m_pixels[p * 4 + 2];
        uint8_t& a = m_pixels[p * 4 + 3];

        Color pixelColor(r, g, b, a);
        Color writeColor = (pixelColor == Color::alpha) ? Color::alpha : color;

        r = writeColor.r();
        g = writeColor.g();
        b = writeColor.b();
        a = writeColor.a();
    }
}

void Image::blit(const Point& dest, const ImagePtr& other)
{
    assert(m_bpp == 4);

    if (!other)
        return;

    const uint8_t* otherPixels = other->getPixelData();
    for (int p = 0; p < other->getPixelCount(); ++p) {
        const int x = p % other->getWidth();
        const int y = p / other->getWidth();
        const int pos = ((dest.y + y) * m_size.width() + (dest.x + x)) * 4;

        if (otherPixels[p * 4 + 3] != 0) {
            m_pixels[pos + 0] = otherPixels[p * 4 + 0];
            m_pixels[pos + 1] = otherPixels[p * 4 + 1];
            m_pixels[pos + 2] = otherPixels[p * 4 + 2];
            m_pixels[pos + 3] = otherPixels[p * 4 + 3];
        }
    }
}

void Image::paste(const ImagePtr& other)
{
    assert(m_bpp == 4);

    if (!other)
        return;

    const uint8_t* otherPixels = other->getPixelData();
    for (int p = 0; p < other->getPixelCount(); ++p) {
        const int x = p % other->getWidth();
        const int y = p / other->getWidth();
        const int pos = (y * m_size.width() + x) * 4;

        m_pixels[pos + 0] = otherPixels[p * 4 + 0];
        m_pixels[pos + 1] = otherPixels[p * 4 + 1];
        m_pixels[pos + 2] = otherPixels[p * 4 + 2];
        m_pixels[pos + 3] = otherPixels[p * 4 + 3];
    }
}

bool Image::nextMipmap()
{
    assert(m_bpp == 4);

    const int iw = m_size.width();
    const int ih = m_size.height();
    if ((iw == 1 && ih == 1) || m_pixels.empty())
        return false;

    const int ow = iw > 1 ? iw / 2 : 1;
    const int oh = ih > 1 ? ih / 2 : 1;

    std::vector<uint8_t > pixels(ow * oh * 4, 0xFF);

    //FIXME: calculate mipmaps for 8x1, 4x1, 2x1 ...
    if (iw != 1 && ih != 1) {
        for (int x = 0; x < ow; ++x) {
            for (int y = 0; y < oh; ++y) {
                uint8_t* inPixel[4];
                inPixel[0] = &m_pixels[((y * 2) * iw + (x * 2)) * 4];
                inPixel[1] = &m_pixels[((y * 2) * iw + (x * 2) + 1) * 4];
                inPixel[2] = &m_pixels[((y * 2 + 1) * iw + (x * 2)) * 4];
                inPixel[3] = &m_pixels[((y * 2 + 1) * iw + (x * 2) + 1) * 4];
                uint8_t* outPixel = &pixels[(y * ow + x) * 4];

                int pixelsSum[4];
                for (int& i : pixelsSum)
                    i = 0;

                int usedPixels = 0;
                for (auto& j : inPixel) {
                    // ignore colors of complete alpha pixels
                    if (j[3] < 16)
                        continue;

                    for (int i = 0; i < 4; ++i)
                        pixelsSum[i] += j[i];

                    ++usedPixels;
                }

                // try to guess the alpha pixel more accurately
                for (int i = 0; i < 4; ++i) {
                    if (usedPixels > 0)
                        outPixel[i] = pixelsSum[i] / usedPixels;
                    else
                        outPixel[i] = 0;
                }
                outPixel[3] = pixelsSum[3] / 4;
            }
        }
    }

    m_pixels = pixels;
    m_size = { ow, oh };
    return true;
}

void Image::flipVertically()
{
    for (int line = 0, h = m_size.height(), w = m_size.width(); line != h / 2; ++line) {
        std::swap_ranges(
            m_pixels.begin() + 4 * w * line,
            m_pixels.begin() + 4 * w * (line + 1),
            m_pixels.begin() + 4 * w * (h - line - 1));
    }
}

void Image::setOpacity(const uint8_t v) {
    for (size_t i = 3, s = m_pixels.size(); i < s; i += 4)
        m_pixels[i] = v;
}

void Image::reverseChannels()
{
    uint8_t* pixelData = m_pixels.data();
    for (uint8_t* itr = pixelData; itr < pixelData + m_pixels.size(); itr += m_bpp) {
        std::swap(*(itr + 0), *(itr + 2));
    }
}

ImagePtr Image::fromQRCode(const std::string& code, const int border)
{
    try {
        const QrCode qrCode = QrCode::encodeText(code.c_str(), QrCode::Ecc::MEDIUM);

        const auto size = qrCode.getSize();
        ImagePtr image(new Image(Size(size + border * 2, size + border * 2)));

        for (int x = 0; x < size + border * 2; ++x) {
            for (int y = 0; y < size + border * 2; ++y) {
                image->setPixel(x, y, qrCode.getModule(x - border, y - border) ? Color::black : Color::white);
            }
        }

        return image;
    } catch (const std::exception& e) {
        g_logger.error("Failed to encode qr-code: '{}': {}", code, e.what());
    }

    return {};
}

namespace {
std::vector<uint8_t> decompressCipLzma(const std::string& fileData)
{
    // CIP format: 32-byte proprietary header + LZMA-alone stream whose
    // uncompressed-size field (bytes 5–12) is often invalid — patch to unknown.
    constexpr size_t CIP_HEADER_SIZE = 32;
    constexpr size_t LZMA_PROPS_SIZE = 5;
    constexpr size_t LZMA_SIZE_FIELD = 8;

    if (fileData.size() <= CIP_HEADER_SIZE + LZMA_PROPS_SIZE + LZMA_SIZE_FIELD)
        return {};

    std::vector<uint8_t> lzmaData(
        reinterpret_cast<const uint8_t*>(fileData.data()) + CIP_HEADER_SIZE,
        reinterpret_cast<const uint8_t*>(fileData.data()) + fileData.size());

    std::fill(lzmaData.begin() + LZMA_PROPS_SIZE,
              lzmaData.begin() + LZMA_PROPS_SIZE + LZMA_SIZE_FIELD,
              0xFFu);

    lzma_stream strm = LZMA_STREAM_INIT;
    constexpr size_t LZMA_MEMORY_LIMIT_BYTES = 64ull * 1024 * 1024;
    if (lzma_alone_decoder(&strm, LZMA_MEMORY_LIMIT_BYTES) != LZMA_OK) {
        lzma_end(&strm);
        return {};
    }

    strm.next_in = lzmaData.data();
    strm.avail_in = lzmaData.size();

    std::vector<uint8_t> output;
    output.reserve(64 * 1024);
    constexpr size_t MAX_DECOMPRESSED = 4096ULL * 4096 * 4 + 2048;

    std::array<uint8_t, 65536> buf{};
    lzma_ret ret = LZMA_OK;
    while (ret == LZMA_OK || ret == LZMA_STREAM_END) {
        strm.next_out = buf.data();
        strm.avail_out = buf.size();
        const lzma_action action = (strm.avail_in == 0) ? LZMA_FINISH : LZMA_RUN;
        ret = lzma_code(&strm, action);
        const size_t got = buf.size() - strm.avail_out;
        if (output.size() + got > MAX_DECOMPRESSED) {
            lzma_end(&strm);
            return {};
        }
        output.insert(output.end(), buf.begin(), buf.begin() + got);
        if (ret == LZMA_STREAM_END)
            break;
    }
    lzma_end(&strm);
    if (ret != LZMA_STREAM_END)
        return {};
    return output;
}
} // namespace

ImagePtr Image::fromBmpBytes(const std::vector<uint8_t>& data)
{
    constexpr size_t BMP_MIN_HEADER = 54;
    if (data.size() < BMP_MIN_HEADER || data[0] != 'B' || data[1] != 'M')
        return nullptr;

    auto readU16 = [&](size_t off) -> uint16_t {
        return static_cast<uint16_t>(data[off]) |
               (static_cast<uint16_t>(data[off + 1]) << 8);
    };
    auto readI32 = [&](size_t off) -> int32_t {
        return static_cast<int32_t>(
            static_cast<uint32_t>(data[off]) |
            (static_cast<uint32_t>(data[off + 1]) << 8) |
            (static_cast<uint32_t>(data[off + 2]) << 16) |
            (static_cast<uint32_t>(data[off + 3]) << 24));
    };
    auto readU32 = [&](size_t off) -> uint32_t {
        return static_cast<uint32_t>(readI32(off));
    };

    const uint32_t pixelOffset = readU32(10);
    const int32_t width = readI32(18);
    int32_t height = readI32(22);
    const uint16_t bpp = readU16(28);
    const bool topDown = (height < 0);
    const int64_t mag = topDown ? -(int64_t)height : (int64_t)height;
    if (width <= 0 || width > 4096 || mag == 0 || mag > 4096)
        return nullptr;
    height = static_cast<int32_t>(mag);

    const uint32_t compression = readU32(30);
    const bool isBitfields = (compression == 3);
    if (compression != 0 && !isBitfields)
        return nullptr;
    if (bpp != 24 && bpp != 32)
        return nullptr;
    if (isBitfields && bpp != 32)
        return nullptr;

    uint32_t rMask = 0x00FF0000u, gMask = 0x0000FF00u,
             bMask = 0x000000FFu, aMask = 0xFF000000u;
    if (isBitfields) {
        if (data.size() < 66)
            return nullptr;
        rMask = readU32(54);
        gMask = readU32(58);
        bMask = readU32(62);
        aMask = ~(rMask | gMask | bMask);
    }

    auto maskShift = [](uint32_t m) -> int {
        if (!m) return 0;
        int s = 0;
        while (!(m & 1u)) {
            m >>= 1;
            ++s;
        }
        return s;
    };
    const int rShift = maskShift(rMask);
    const int gShift = maskShift(gMask);
    const int bShift = maskShift(bMask);
    const int aShift = maskShift(aMask);

    const int bytesPerPixel = bpp / 8;
    const int rowStride = (width * bytesPerPixel + 3) & ~3;
    if (pixelOffset >= data.size())
        return nullptr;
    if (data.size() < pixelOffset + static_cast<size_t>(rowStride) * height)
        return nullptr;

    auto image = std::make_shared<Image>(Size(width, height), /*bpp=*/4);
    if (image->getWidth() != width || image->getHeight() != height)
        return nullptr;

    const uint8_t* src = data.data() + pixelOffset;
    bool hasTransparent = false;
    for (int y = 0; y < height; ++y) {
        const int srcY = topDown ? y : (height - 1 - y);
        const uint8_t* row = src + static_cast<ptrdiff_t>(srcY) * rowStride;
        for (int x = 0; x < width; ++x) {
            const uint8_t* p = row + x * bytesPerPixel;
            uint8_t r, g, b, a;
            if (isBitfields) {
                const uint32_t px = static_cast<uint32_t>(p[0]) |
                                    (static_cast<uint32_t>(p[1]) << 8) |
                                    (static_cast<uint32_t>(p[2]) << 16) |
                                    (static_cast<uint32_t>(p[3]) << 24);
                r = static_cast<uint8_t>((px & rMask) >> rShift);
                g = static_cast<uint8_t>((px & gMask) >> gShift);
                b = static_cast<uint8_t>((px & bMask) >> bShift);
                a = aMask ? static_cast<uint8_t>((px & aMask) >> aShift) : uint8_t{ 255 };
            } else {
                r = p[2];
                g = p[1];
                b = p[0];
                a = (bpp == 32) ? p[3] : uint8_t{ 255 };
            }
            if (a == 0)
                hasTransparent = true;
            const uint8_t rgba[4] = { r, g, b, a };
            image->setPixel(x, y, rgba);
        }
    }
    image->setTransparentPixel(hasTransparent);
    return image;
}

ImagePtr Image::fromCipBmpData(const std::string& fileData)
{
    auto bmp = decompressCipLzma(fileData);
    if (bmp.empty())
        return nullptr;
    auto image = fromBmpBytes(bmp);
    bmp.clear();
    bmp.shrink_to_fit();
    return image;
}

ImagePtr Image::loadCipBmp(const std::string& file)
{
    try {
        const auto& path = tibia::files::data().resolvePath(file);
        const std::string fileData = tibia::files::data().readFileContents(path);
        return fromCipBmpData(fileData);
    } catch (const stdext::exception& e) {
        g_logger.error("Unable to load CIP image '{}': {}", file, e.what());
    }
    return nullptr;
}
