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
#include <framework/luaengine/luaobject.h>

#include <string>
#include <system_error>

/// Clean socket shutdown. Replaces asio::error::eof.
const std::error_code& gameConnectionEof();

 // @bindclass
class Protocol : public LuaObject
{
public:
    Protocol();
    ~Protocol() override;

#ifndef __EMSCRIPTEN__
    void connect(std::string_view host, uint16_t port);
#else
    void connect(const std::string_view host, uint16_t port, bool gameWorld = false);
#endif
    void disconnect();

    /// Decoded server→client body (opcode first). Replay sets `replay` so the
    /// first-message padding byte is not stripped again.
    void deliverParserBytes(const uint8_t* data, uint16_t size);
    void setReplay(bool replay) { m_replay = replay; }
    bool isReplay() const { return m_replay; }

    /// `clean` matches a peer close (no connection-error dialog).
    void fail(const std::string& message, bool clean);
    void notifyConnected();

    bool isConnected();
    bool isConnecting();
    ticks_t getElapsedTicksSinceLastRead() const;

    void generateXteaKey();
    void setXteaKey(uint32_t a, uint32_t b, uint32_t c, uint32_t d);
    std::vector<uint32_t> getXteaKey() { return { m_xteaKey.begin(), m_xteaKey.end() }; }
    void enableXteaEncryption();

    void enableChecksum();
    void enabledSequencedPackets();
    void setRecordFile(std::string_view file);

    virtual void send(const OutputMessagePtr& outputMessage, bool raw = false);
    virtual void recv();

    ProtocolPtr asProtocol() { return static_self_cast<Protocol>(); }

protected:
    virtual void onConnect();
    virtual void onRecv(const InputMessagePtr& inputMessage);
    virtual void onError(const std::error_code& err);
    virtual void onSend() {};

    bool m_disconnected = false;
    bool m_replay = false;

    std::array<uint32_t, 4> m_xteaKey{};

private:
    InputMessagePtr m_inputMessage;
};
