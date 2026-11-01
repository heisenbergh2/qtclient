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

#include "filestream.h"

#include "binarytree.h"
#include "tibia/runtime/host.h"

#include <fstream>
#if ENABLE_ENCRYPTION == 1
#include "resourcemanager.h"
#endif

inline void grow(std::vector<uint8_t>& data, const size_t size) {
    if (size > data.size())
        data.resize(size);
}

FileStream::FileStream(std::string name, const std::string_view buffer) :
    m_name(std::move(name)),
    m_pos(0),
    m_writeable(false),
    m_caching(true)
{
    m_data.resize(buffer.length());
    memcpy(&m_data[0], &buffer[0], buffer.length());
}

FileStream::FileStream(std::string name, std::string diskPath, const bool writeable) :
    m_name(std::move(name)),
    m_diskPath(std::move(diskPath)),
    m_pos(0),
    m_writeable(writeable),
    m_caching(true)
{
}

FileStream::~FileStream()
{
    close();
}

void FileStream::cache(bool
#if ENABLE_ENCRYPTION == 1
                       useEnc
#endif
)
{
    m_caching = true;

#if ENABLE_ENCRYPTION == 1
    if (!m_writeable && useEnc) {
        const std::string encHeader(ENCRYPTION_HEADER);
        if (m_data.size() >= encHeader.size() &&
            std::memcmp(m_data.data(), encHeader.data(), encHeader.size()) == 0) {
            m_data.erase(m_data.begin(), m_data.begin() + encHeader.size());
            ResourceManager::decrypt(m_data.data(), static_cast<int32_t>(m_data.size()));
        }
    }
#else
    (void)0;
#endif
}

void FileStream::close()
{
    if (m_writeable && !m_diskPath.empty()) {
        std::ofstream out(m_diskPath, std::ios::binary);
        if (out)
            out.write(reinterpret_cast<const char*>(m_data.data()), static_cast<std::streamsize>(m_data.size()));
        m_diskPath.clear();
    }

    m_data.clear();
    m_pos = 0;
}

void FileStream::flush()
{
    if (!m_writeable)
        throwError("filestream is not writeable");

    
}

int FileStream::read(void* buffer, const uint32_t size, const uint32_t nmemb)
{
    
    int writePos = 0;
    auto* const outBuffer = static_cast<uint8_t*>(buffer);
    for (uint32_t i = 0; i < nmemb; ++i) {
        if (m_pos + size > m_data.size())
            return i;

        for (uint32_t j = 0; j < size; ++j)
            outBuffer[writePos++] = m_data[m_pos++];
    }
    return nmemb;
}

void FileStream::write(const void* buffer, const uint32_t count)
{
    
        grow(m_data, m_pos + count);
        memcpy(&m_data[m_pos], buffer, count);
        m_pos += count;
    
}

void FileStream::seek(const uint32_t pos)
{
    
        if (pos > m_data.size())
            throwError("seek failed");
        m_pos = pos;
    
}

void FileStream::skip(const uint32_t len)
{
    seek(tell() + len);
}

uint32_t FileStream::size() const
{
    return static_cast<uint32_t>(m_data.size());
}

uint32_t FileStream::tell() const
{
    return m_pos;
}

bool FileStream::eof() const
{
    return m_pos >= m_data.size();
}

uint8_t FileStream::getU8()
{
    if (m_pos + 1 > m_data.size())
        throwError("read failed");
    return m_data[m_pos++];
}
uint16_t FileStream::getU16()
{
    uint16_t v = 0;
    
        if (m_pos + 2 > m_data.size())
            throwError("read failed");

        v = stdext::readULE16(&m_data[m_pos]);
        m_pos += 2;
    
    return v;
}

uint32_t FileStream::getU32()
{
    uint32_t v = 0;
    
        if (m_pos + 4 > m_data.size())
            throwError("read failed");

        v = stdext::readULE32(&m_data[m_pos]);
        m_pos += 4;
    
    return v;
}

uint64_t FileStream::getU64()
{
    uint64_t v = 0;
    
        if (m_pos + 8 > m_data.size())
            throwError("read failed");
        v = stdext::readULE64(&m_data[m_pos]);
        m_pos += 8;
    
    return v;
}

int8_t FileStream::get8()
{
    int8_t v = 0;
    
        if (m_pos + 1 > m_data.size())
            throwError("read failed");

        v = m_data[m_pos];
        m_pos += 1;
    
    return v;
}

int16_t FileStream::get16()
{
    int16_t v = 0;
    
        if (m_pos + 2 > m_data.size())
            throwError("read failed");

        v = stdext::readSLE16(&m_data[m_pos]);
        m_pos += 2;
    
    return v;
}

int32_t FileStream::get32()
{
    int32_t v = 0;
    
        if (m_pos + 4 > m_data.size())
            throwError("read failed");

        v = stdext::readSLE32(&m_data[m_pos]);
        m_pos += 4;
    
    return v;
}

int64_t FileStream::get64()
{
    int64_t v = 0;
    
        if (m_pos + 8 > m_data.size())
            throwError("read failed");
        v = stdext::readSLE64(&m_data[m_pos]);
        m_pos += 8;
    
    return v;
}

std::string FileStream::getString()
{
    std::string str;
    if (const uint16_t len = getU16(); len > 0 && len < 8192) {
        char buffer[8192];
        
            if (m_pos + len > m_data.size()) {
                throwError("[FileStream::getString] - Read failed");
                return {};
            }

            str = { (char*)&m_data[m_pos], len };
            m_pos += len;
        
    } else if (len != 0)
        throwError("[FileStream::getString] - Read failed because string is too big");
    return str;
}

BinaryTreePtr FileStream::getBinaryTree()
{
    if (const uint8_t byte = getU8(); byte != static_cast<uint8_t>(BinaryTree::Node::START))
        throw Exception("failed to read node start (getBinaryTree): {}", byte);

    return  std::make_shared<BinaryTree>(shared_from_this());
}

void FileStream::startNode(const uint8_t n)
{
    addU8(static_cast<uint8_t>(BinaryTree::Node::START));
    addU8(n);
}

void FileStream::endNode()
{
    addU8(static_cast<uint8_t>(BinaryTree::Node::END));
}

void FileStream::addU8(const uint8_t v)
{
    
        m_data.push_back(v);
        m_pos++;
    
}

void FileStream::addU16(const uint16_t v)
{
    
        grow(m_data, m_pos + 2);
        stdext::writeULE16(&m_data[m_pos], v);
        m_pos += 2;
    
}

void FileStream::addU32(const uint32_t v)
{
    
        grow(m_data, m_pos + 4);
        stdext::writeULE32(&m_data[m_pos], v);
        m_pos += 4;
    
}

void FileStream::addU64(const uint64_t v)
{
    
        grow(m_data, m_pos + 8);
        stdext::writeULE64(&m_data[m_pos], v);
        m_pos += 8;
    
}

void FileStream::add8(const int8_t v)
{
    
        m_data.push_back(v);
        m_pos++;
    
}

void FileStream::add16(const int16_t v)
{
    
        grow(m_data, m_pos + 2);
        stdext::writeSLE16(&m_data[m_pos], v);
        m_pos += 2;
    
}

void FileStream::add32(const int32_t v)
{
    
        grow(m_data, m_pos + 4);
        stdext::writeSLE32(&m_data[m_pos], v);
        m_pos += 4;
    
}

void FileStream::add64(const int64_t v)
{
    
        grow(m_data, m_pos + 8);
        stdext::writeSLE64(&m_data[m_pos], v);
        m_pos += 8;
    
}

void FileStream::addString(const std::string_view v)
{
    addU16(v.length());
    write(v.data(), v.length());
}

void FileStream::throwError(const std::string_view message) const
{
    throw Exception(fmt::format("in file '{}': {}", m_name, message));
}