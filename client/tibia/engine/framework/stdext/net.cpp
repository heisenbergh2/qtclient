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

#include "net.h"

#include <QHostAddress>
#include <QtEndian>

namespace stdext
{
    std::string ip_to_string(uint32_t ip)
    {
        return QHostAddress(qFromBigEndian(ip)).toString().toStdString();
    }

    uint32_t string_to_ip(const std::string_view string)
    {
        const QHostAddress address(QString::fromUtf8(string.data(), static_cast<int>(string.size())));
        return qToBigEndian(address.toIPv4Address());
    }

    std::vector<uint32_t> listSubnetAddresses(const uint32_t address, const uint8_t mask)
    {
        std::vector<uint32_t> list;
        if (mask < 32) {
            const uint32_t bitmask = (0xFFFFFFFF >> mask);
            const uint32_t host = qFromBigEndian(address);
            for (uint32_t i = 0; i <= bitmask; ++i) {
                const uint32_t ip = qToBigEndian((host & ~bitmask) | i);
                const uint32_t first = qFromBigEndian(ip) >> 24;
                if (first != 0 && first != 0xFF)
                    list.push_back(ip);
            }
        } else
            list.push_back(address);

        return list;
    }
}
