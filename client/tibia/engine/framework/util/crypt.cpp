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

#include "crypt.h"

#ifndef USE_PRECOMPILED_HEADERS
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iomanip>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <zlib.h>
#endif

#include "framework/core/resourcemanager.h"
#include "tibia/runtime/host.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QString>
#include <QSysInfo>
#include "framework/stdext/math.h"

#include <vector>

constexpr std::size_t CHECKSUM_BYTES = sizeof(uint32_t);

Crypt g_crypt;

namespace {

// Little-endian limbs. Login RSA is raw modular exponentiation (no padding).
class BigInt
{
public:
    std::vector<uint32_t> d;

    static BigInt fromDec(std::string_view text)
    {
        BigInt out;
        for (const char ch : text) {
            if (ch < '0' || ch > '9')
                continue;
            out = add(mulSmall(out, 10), BigInt::fromU32(static_cast<uint32_t>(ch - '0')));
        }
        return out;
    }

    static BigInt fromBytes(const uint8_t* data, int size)
    {
        BigInt out;
        for (int i = 0; i < size; ++i) {
            out = mulSmall(out, 256);
            out = add(out, fromU32(data[i]));
        }
        return out;
    }

    void toBytes(uint8_t* data, int size) const
    {
        std::fill(data, data + size, 0);
        BigInt rest = *this;
        for (int i = size - 1; i >= 0; --i) {
            data[i] = static_cast<uint8_t>(rest.d.empty() ? 0 : (rest.d[0] & 0xff));
            rest = divSmall(rest, 256);
        }
    }

    int bitSize() const
    {
        if (d.empty())
            return 0;
        int bits = static_cast<int>(d.size() - 1) * 32;
        uint32_t top = d.back();
        while (top) {
            ++bits;
            top >>= 1;
        }
        return bits;
    }

    static BigInt product(const BigInt& a, const BigInt& b) { return mul(a, b); }

    static BigInt modPow(BigInt base, BigInt exp, const BigInt& mod)
    {
        BigInt result = fromU32(1);
        base = modReduce(base, mod);
        for (int bit = exp.bitSize() - 1; bit >= 0; --bit) {
            result = modReduce(mul(result, result), mod);
            if (exp.bit(bit))
                result = modReduce(mul(result, base), mod);
        }
        return result;
    }

private:
    static BigInt fromU32(uint32_t v)
    {
        BigInt out;
        if (v)
            out.d.push_back(v);
        return out;
    }

    bool bit(int index) const
    {
        const size_t limb = static_cast<size_t>(index) / 32;
        if (limb >= d.size())
            return false;
        return (d[limb] >> (index % 32)) & 1u;
    }

    static int cmp(const BigInt& a, const BigInt& b)
    {
        if (a.d.size() != b.d.size())
            return a.d.size() < b.d.size() ? -1 : 1;
        for (size_t i = a.d.size(); i-- > 0;) {
            if (a.d[i] != b.d[i])
                return a.d[i] < b.d[i] ? -1 : 1;
        }
        return 0;
    }

    static BigInt add(const BigInt& a, const BigInt& b)
    {
        BigInt out;
        const size_t n = std::max(a.d.size(), b.d.size());
        out.d.resize(n);
        uint64_t carry = 0;
        for (size_t i = 0; i < n; ++i) {
            carry += i < a.d.size() ? a.d[i] : 0;
            carry += i < b.d.size() ? b.d[i] : 0;
            out.d[i] = static_cast<uint32_t>(carry);
            carry >>= 32;
        }
        if (carry)
            out.d.push_back(static_cast<uint32_t>(carry));
        return out;
    }

    static BigInt sub(const BigInt& a, const BigInt& b)
    {
        BigInt out = a;
        uint32_t borrow = 0;
        for (size_t i = 0; i < b.d.size() || borrow; ++i) {
            uint64_t cur = out.d[i];
            const uint64_t take = (i < b.d.size() ? b.d[i] : 0) + borrow;
            if (cur < take) {
                cur += 1ull << 32;
                borrow = 1;
            } else {
                borrow = 0;
            }
            out.d[i] = static_cast<uint32_t>(cur - take);
        }
        while (!out.d.empty() && out.d.back() == 0)
            out.d.pop_back();
        return out;
    }

    static BigInt mulSmall(const BigInt& a, uint32_t m)
    {
        if (m == 0 || a.d.empty())
            return {};
        BigInt out;
        out.d.resize(a.d.size());
        uint64_t carry = 0;
        for (size_t i = 0; i < a.d.size(); ++i) {
            carry += static_cast<uint64_t>(a.d[i]) * m;
            out.d[i] = static_cast<uint32_t>(carry);
            carry >>= 32;
        }
        if (carry)
            out.d.push_back(static_cast<uint32_t>(carry));
        return out;
    }

    static BigInt divSmall(const BigInt& a, uint32_t m)
    {
        BigInt out;
        out.d.resize(a.d.size());
        uint64_t rem = 0;
        for (size_t i = a.d.size(); i-- > 0;) {
            rem = (rem << 32) | a.d[i];
            out.d[i] = static_cast<uint32_t>(rem / m);
            rem %= m;
        }
        while (!out.d.empty() && out.d.back() == 0)
            out.d.pop_back();
        return out;
    }

    static BigInt mul(const BigInt& a, const BigInt& b)
    {
        if (a.d.empty() || b.d.empty())
            return {};
        BigInt out;
        out.d.assign(a.d.size() + b.d.size(), 0);
        for (size_t i = 0; i < a.d.size(); ++i) {
            uint64_t carry = 0;
            for (size_t j = 0; j < b.d.size() || carry; ++j) {
                const uint64_t cur = out.d[i + j] + carry
                    + static_cast<uint64_t>(a.d[i]) * (j < b.d.size() ? b.d[j] : 0);
                out.d[i + j] = static_cast<uint32_t>(cur);
                carry = cur >> 32;
            }
        }
        while (!out.d.empty() && out.d.back() == 0)
            out.d.pop_back();
        return out;
    }

    static BigInt modReduce(BigInt value, const BigInt& mod)
    {
        if (cmp(value, mod) < 0)
            return value;
        const int shift = value.bitSize() - mod.bitSize();
        for (int bit = shift; bit >= 0; --bit) {
            BigInt shifted = mod << bit;
            if (cmp(value, shifted) >= 0)
                value = sub(value, shifted);
        }
        return value;
    }

    BigInt operator<<(int bits) const
    {
        if (bits == 0 || d.empty())
            return *this;
        BigInt out;
        const int limbShift = bits / 32;
        const int bitShift = bits % 32;
        out.d.assign(d.size() + static_cast<size_t>(limbShift) + 1, 0);
        for (size_t i = 0; i < d.size(); ++i) {
            const uint64_t cur = static_cast<uint64_t>(d[i]) << bitShift;
            out.d[i + limbShift] |= static_cast<uint32_t>(cur);
            if (bitShift)
                out.d[i + limbShift + 1] |= static_cast<uint32_t>(cur >> 32);
        }
        while (!out.d.empty() && out.d.back() == 0)
            out.d.pop_back();
        return out;
    }
};

BigInt g_rsaN;
BigInt g_rsaE;
BigInt g_rsaD;

}

Crypt::Crypt() = default;
Crypt::~Crypt() = default;

std::string Crypt::base64Encode(const std::string& decoded_string) {
    return QByteArray(decoded_string.data(), static_cast<int>(decoded_string.size()))
        .toBase64()
        .toStdString();
}

std::string Crypt::base64Decode(const std::string_view& encoded_string) {
    const QByteArray decoded = QByteArray::fromBase64(
        QByteArray(encoded_string.data(), static_cast<int>(encoded_string.size())));
    return std::string(decoded.constData(), static_cast<size_t>(decoded.size()));
}

void Crypt::xorCrypt(std::string& buffer, const std::string& key)
{
    if (key.empty())
        return;

    const size_t keySize = key.size();
    for (size_t i = 0; i < buffer.size(); ++i)
        buffer[i] ^= key[i % keySize];
}

std::string Crypt::genUUID() {
    return uuids::to_string(uuids::uuid_random_generator{ stdext::random_gen() }());
}

bool Crypt::setMachineUUID(std::string uuidstr)
{
    if (uuidstr.empty())
        return false;

    uuidstr = _decrypt(uuidstr, false);

    if (uuidstr.length() != 36)
        return false;

    m_machineUUID = uuids::uuid::from_string(uuidstr).value();

    return true;
}

std::string Crypt::getMachineUUID()
{
    if (m_machineUUID.is_nil()) {
        m_machineUUID = uuids::uuid_random_generator{ stdext::random_gen() }();
    }
    return _encrypt(uuids::to_string(m_machineUUID), false);
}

std::string Crypt::getCryptKey(const bool useMachineUUID) const
{
    static const uuids::uuid default_uuid{};
    const uuids::uuid& uuid = useMachineUUID ? m_machineUUID : default_uuid;

    const uuids::uuid u = uuids::uuid_name_generator(uuid)(
        tibia::host::app().getCompactName() + QSysInfo::currentCpuArchitecture().toStdString()
            + QSysInfo::productType().toStdString() + tibia::files::data().getUserDir()
    );

    const std::size_t hash = std::hash<uuids::uuid>{}(u);

    return std::string(reinterpret_cast<const char*>(&hash), sizeof(hash));
}

std::string Crypt::_encrypt(const std::string& decrypted_string, const bool useMachineUUID)
{
    const uint32_t sum = stdext::computeChecksum(
        { reinterpret_cast<const uint8_t*>(decrypted_string.data()),
          decrypted_string.size() });

    std::string tmp(CHECKSUM_BYTES, '\0');
    tmp.append(decrypted_string);

    stdext::writeULE32(reinterpret_cast<uint8_t*>(tmp.data()), sum);

    const auto key = getCryptKey(useMachineUUID);
    xorCrypt(tmp, key);
    return base64Encode(tmp);
}

std::string Crypt::_decrypt(const std::string& encrypted_string, const bool useMachineUUID)
{
    std::string decoded = base64Decode(encrypted_string);
    if (decoded.size() < CHECKSUM_BYTES)
        return {};

    const auto key = getCryptKey(useMachineUUID);
    xorCrypt(decoded, key);

    const uint32_t readsum =
        stdext::readULE32(reinterpret_cast<const uint8_t*>(decoded.data()));
    decoded.erase(0, CHECKSUM_BYTES);

    const uint32_t sum = stdext::computeChecksum(
        { reinterpret_cast<const uint8_t*>(decoded.data()), decoded.size() });

    return (readsum == sum) ? std::move(decoded) : std::string();
}

void Crypt::rsaSetPublicKey(const std::string& n, const std::string& e)
{
    g_rsaN = BigInt::fromDec(n);
    g_rsaE = BigInt::fromDec(e);
    m_n = n;
    m_e = e;
}

void Crypt::rsaSetPrivateKey(const std::string& p, const std::string& q, const std::string& d)
{
    g_rsaD = BigInt::fromDec(d);
    g_rsaN = BigInt::product(BigInt::fromDec(p), BigInt::fromDec(q));
    m_d = d;
}

bool Crypt::rsaEncrypt(uint8_t* msg, int size)
{
    if (size != rsaGetSize())
        return false;
    const BigInt encrypted = BigInt::modPow(BigInt::fromBytes(msg, size), g_rsaE, g_rsaN);
    encrypted.toBytes(msg, size);
    return true;
}

bool Crypt::rsaDecrypt(uint8_t* msg, int size)
{
    if (size != rsaGetSize())
        return false;
    const BigInt decrypted = BigInt::modPow(BigInt::fromBytes(msg, size), g_rsaD, g_rsaN);
    decrypted.toBytes(msg, size);
    return true;
}

int Crypt::rsaGetSize()
{
    const int bytes = (g_rsaN.bitSize() + 7) / 8;
    return (bytes / 128) * 128;
}

std::string Crypt::crc32(const std::string& decoded_string, const bool upperCase)
{
    uint32_t crc = ::crc32(0, nullptr, 0);
    crc = ::crc32(crc, reinterpret_cast<const Bytef*>(decoded_string.data()), decoded_string.size());
    std::string result = stdext::dec_to_hex(crc);
    if (upperCase)
        std::ranges::transform(result, result.begin(), toupper);
    else
        std::ranges::transform(result, result.begin(), tolower);
    return result;
}

std::string Crypt::sha256(const std::string& decoded_string)
{
    const QByteArray digest = QCryptographicHash::hash(
        QByteArray(decoded_string.data(), static_cast<int>(decoded_string.size())),
        QCryptographicHash::Sha256);
    return digest.toHex().toStdString();
}
