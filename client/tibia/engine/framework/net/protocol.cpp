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

#include "protocol.h"
#include "tibia/runtime/host.h"

#include "tibia/world/game.h"
#include "gamelink.h"
#include "inputmessage.h"
#include "outputmessage.h"

#include <algorithm>
#include <ctime>
#include <string>
#include <string_view>

class SocketErrorCategory final : public std::error_category
{
public:
    const char* name() const noexcept override { return "gamesocket"; }
    std::string message(int) const override { return m_message; }
    void set(std::string message) { m_message = std::move(message); }

private:
    std::string m_message;
};

SocketErrorCategory& socketErrors()
{
    static SocketErrorCategory category;
    return category;
}

const std::error_code& gameConnectionEof()
{
    static const std::error_code eof(0, socketErrors());
    return eof;
}

Protocol::Protocol()
    : m_inputMessage(std::make_shared<InputMessage>())
{
}

Protocol::~Protocol()
{
#ifndef NDEBUG
    assert(!tibia::host::app().isTerminated());
#endif
    disconnect();
}

#ifndef __EMSCRIPTEN__
void Protocol::connect(const std::string_view host, const uint16_t port)
{
    m_disconnected = false;
    if (!gamelink::installed()) {
        g_logger.error("game connection is not installed");
        return;
    }
    gamelink::connect(this, host, port, true);
}
#else
void Protocol::connect(const std::string_view host, const uint16_t port, const bool gameWorld)
{
    m_disconnected = false;
    if (!gamelink::installed()) {
        g_logger.error("game connection is not installed");
        return;
    }
    gamelink::connect(this, host, port, gameWorld);
}
#endif

void Protocol::disconnect()
{
    m_disconnected = true;
    gamelink::disconnect(this);
}

void Protocol::deliverParserBytes(const uint8_t* data, const uint16_t size)
{
    if (m_disconnected || !data || size == 0)
        return;
    m_inputMessage->reset();
    m_inputMessage->setHeaderSize(0);
    m_inputMessage->fillBuffer(data, size);
    m_inputMessage->setMessageSize(size);
    onRecv(m_inputMessage);
}

void Protocol::fail(const std::string& message, const bool clean)
{
    if (m_disconnected)
        return;
    m_disconnected = true;
    gamelink::disconnect(this);
    if (clean)
        onError(gameConnectionEof());
    else {
        socketErrors().set(message.empty() ? "connection lost" : message);
        onError(std::error_code(1, socketErrors()));
    }
}

void Protocol::notifyConnected()
{
    if (!m_disconnected)
        onConnect();
}

bool Protocol::isConnected()
{
    if (m_replay)
        return !m_disconnected;
    return gamelink::isConnected(this);
}

bool Protocol::isConnecting()
{
    if (m_replay)
        return false;
    return gamelink::isConnecting(this);
}

namespace {

// Login used to stay under the server's 50 packet/s cap. Log the opcodes when a
// second crosses that, so a reconnect loop is visible in the client log.
void noteClientSendRate(const std::string_view payload)
{
    struct Bucket {
        std::time_t second = 0;
        int count = 0;
        int byOpcode[256] = {};
    };
    static Bucket bucket;
    const std::time_t now = std::time(nullptr);
    if (now != bucket.second) {
        bucket = {};
        bucket.second = now;
    }
    const auto opcode = payload.empty() ? 0 : static_cast<uint8_t>(payload[0]);
    ++bucket.count;
    ++bucket.byOpcode[opcode];
    if (bucket.count != 40 && bucket.count != 80 && bucket.count != 160)
        return;

    int ranked[256];
    for (int i = 0; i < 256; ++i)
        ranked[i] = i;
    std::sort(ranked, ranked + 256, [&](const int a, const int b) { return bucket.byOpcode[a] > bucket.byOpcode[b]; });
    std::string top;
    for (int n = 0; n < 6 && bucket.byOpcode[ranked[n]] > 0; ++n) {
        if (!top.empty())
            top += ' ';
        top += std::to_string(ranked[n]) + "x" + std::to_string(bucket.byOpcode[ranked[n]]);
    }
    g_logger.warning("client send rate reached {} packets this second ({})", bucket.count, top);
}

}

void Protocol::send(const OutputMessagePtr& outputMessage, const bool raw)
{
    if (m_replay) {
        const auto payload = outputMessage->getBuffer();
        if (!payload.empty() && static_cast<uint8_t>(payload[0]) == 0x14)
            fail({}, true);
        outputMessage->reset();
        return;
    }

    const auto payload = outputMessage->getBuffer();
    noteClientSendRate(payload);
    if (payload.size() > 0xffff) {
        g_logger.error("game packet exceeds 65535 bytes");
        outputMessage->reset();
        return;
    }
    gamelink::write(this, reinterpret_cast<const uint8_t*>(payload.data()), static_cast<uint16_t>(payload.size()), raw);
    onSend();
    outputMessage->reset();
}

void Protocol::recv()
{
    // The tibia socket pushes complete messages; nothing to re-arm.
}

void Protocol::generateXteaKey()
{
    gamelink::generateXtea(this, m_xteaKey.data());
}

void Protocol::setXteaKey(const uint32_t a, const uint32_t b, const uint32_t c, const uint32_t d)
{
    m_xteaKey = { a, b, c, d };
    gamelink::setXtea(this, m_xteaKey.data());
}

void Protocol::enableXteaEncryption() { gamelink::enableXtea(this); }
void Protocol::enableChecksum() { gamelink::enableChecksum(this); }
void Protocol::enabledSequencedPackets() { gamelink::enableSequenced(this); }
void Protocol::setRecordFile(const std::string_view file) { gamelink::setRecordFile(this, file); }

void Protocol::onConnect()
{
    if (g_game.getClientVersion() >= 1200) {
        std::string sendWorldName(g_game.getWorldName());
        sendWorldName += '\n';
        const auto& msg = std::make_shared<OutputMessage>();
        msg->addBytes(std::string_view(sendWorldName));
        send(msg, true);
        enabledSequencedPackets();
    }
    callLuaField("onConnect");
}

void Protocol::onRecv(const InputMessagePtr& inputMessage)
{
    callLuaField("onRecv", inputMessage);
}

void Protocol::onError(const std::error_code& err)
{
    callLuaField("onError", err.message(), err.value());
    disconnect();
}

ticks_t Protocol::getElapsedTicksSinceLastRead() const
{
    return gamelink::millisSinceRead(const_cast<Protocol*>(this));
}
