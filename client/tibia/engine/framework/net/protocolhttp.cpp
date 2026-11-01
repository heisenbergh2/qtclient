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

#include "protocolhttp.h"

#include <framework/core/gamehooks.h>

#include "framework/core/asyncdispatcher.h"
#include "framework/core/eventdispatcher.h"
#include "framework/util/crypt.h"

#include <algorithm>
#ifdef __EMSCRIPTEN__
#include <cstring>
#include <emscripten.h>
#include <limits>
#else
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#endif
#include <utility>
#include <vector>

Http g_http;

#ifdef __EMSCRIPTEN__
namespace {
    constexpr int httpOkMin = 200;
    constexpr int httpOkMax = 299;

    std::string normalizeDownloadPath(const std::string& path)
    {
        if (!path.empty() && path[0] == '/')
            return path.substr(1);
        return path;
    }

    std::string describeFetchError(const emscripten_fetch_t* fetch, const bool canceled)
    {
        if (canceled)
            return "canceled";
        if (!fetch)
            return "http_error::no_response";
        if (fetch->status < httpOkMin || fetch->status > httpOkMax)
            return "http_status::" + std::to_string(fetch->status);
        return {};
    }

    // Async emscripten_fetch callbacks never reach Lua under PROXY_TO_PTHREAD +
    // the map/Lua tight loop. LoginHttp already uses SYNCHRONOUS fetch on a
    // worker; keep HTTP get/post/download on that path.
    struct SyncFetchResult
    {
        int status = 0;
        std::string body;
        std::string error;
        uint64_t totalBytes = 0;
    };

    // This emscripten build's emscripten_fetch_t has no responseHeaders field.
    // Probe Content-Length / Content-Range via sync XHR (safe on the download worker).
    EM_JS(double, qtclientProbeHttpSize, (const char* url), {
        try {
            var u = UTF8ToString(url);
            var xhr = new XMLHttpRequest();
            xhr.open('HEAD', u, false);
            xhr.send(null);
            if (xhr.status >= 200 && xhr.status < 300) {
                var cl = xhr.getResponseHeader('Content-Length');
                if (cl) {
                    var n = parseInt(cl, 10);
                    if (n > 0) return n;
                }
            }
            xhr = new XMLHttpRequest();
            xhr.open('GET', u, false);
            xhr.setRequestHeader('Range', 'bytes=0-0');
            xhr.send(null);
            // No regex here: EM_JS stringifies with #__VA_ARGS__, so \\ escapes break acorn.
            var cr = xhr.getResponseHeader('Content-Range');
            if (cr) {
                var slash = cr.lastIndexOf('/');
                if (slash >= 0) {
                    var n = parseInt(cr.substring(slash + 1), 10);
                    if (n > 0) return n;
                }
            }
        } catch (e) {}
        return 0;
    });

    SyncFetchResult syncFetch(const char* method,
                              const std::string& url,
                              int timeoutMs,
                              const std::unordered_map<std::string, std::string>& customHeaders,
                              const std::string* body,
                              bool isJson,
                              bool canceled)
    {
        std::vector<std::string> headerStorage;
        headerStorage.reserve((customHeaders.size() * 2) + 4);
        headerStorage.emplace_back("Accept");
        headerStorage.emplace_back("*/*");
        if (body) {
            headerStorage.emplace_back("Content-Type");
            headerStorage.emplace_back(isJson ? "application/json" : "application/x-www-form-urlencoded");
        }
        for (const auto& header : customHeaders) {
            headerStorage.emplace_back(header.first);
            headerStorage.emplace_back(header.second);
        }
        std::vector<const char*> headerPointers;
        headerPointers.reserve(headerStorage.size() + 1);
        for (const auto& header : headerStorage)
            headerPointers.push_back(header.c_str());
        headerPointers.push_back(nullptr);

        emscripten_fetch_attr_t attr;
        emscripten_fetch_attr_init(&attr);
        std::strcpy(attr.requestMethod, method);
        attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY | EMSCRIPTEN_FETCH_SYNCHRONOUS;
        attr.timeoutMSecs = timeoutMs;
        attr.requestHeaders = headerPointers.data();
        if (body) {
            attr.requestData = body->data();
            attr.requestDataSize = body->size();
        }

        SyncFetchResult out;
        emscripten_fetch_t* fetch = emscripten_fetch(&attr, url.c_str());
        if (!fetch) {
            out.error = "http_error::queue";
            return out;
        }
        out.status = fetch->status;
        out.totalBytes = fetch->totalBytes;
        if (fetch->data && fetch->numBytes > 0)
            out.body.assign(fetch->data, fetch->data + fetch->numBytes);
        out.error = describeFetchError(fetch, canceled);
        emscripten_fetch_close(fetch);
        return out;
    }

    void finishHttpResult(const HttpResult_ptr& result, SyncFetchResult&& fetched)
    {
        result->status = fetched.status;
        result->finished = true;
        result->progress = 100;
        result->response = std::move(fetched.body);
        result->error = std::move(fetched.error);
        result->size = static_cast<int>(std::min<size_t>(result->response.size(), static_cast<size_t>(std::numeric_limits<int>::max())));
    }
}

struct Http::WebSocketContext
{
    Http* http = nullptr;
    HttpResult_ptr result;
    EMSCRIPTEN_WEBSOCKET_T socket = 0;
    bool errorSent = false;
};

void Http::init()
{
    if (m_working.load())
        return;
    m_working.store(true);
}

void Http::terminate()
{
    if (!m_working.load())
        return;
    m_working.store(false);

    std::vector<EMSCRIPTEN_WEBSOCKET_T> websockets;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& entry : m_operations) {
            if (entry.second)
                entry.second->canceled = true;
        }
        for (auto& entry : m_websockets) {
            if (entry.second && entry.second->socket > 0)
                websockets.push_back(entry.second->socket);
        }
        m_operations.clear();
        m_downloads.clear();
    }

    for (const auto socket : websockets) {
        emscripten_websocket_close(socket, 1000, "terminate");
    }
}

HttpResult_ptr Http::registerOperation(const std::string& url, int& operationId)
{
    operationId = m_operationId++;
    auto result = std::make_shared<HttpResult>();
    result->url = url;
    result->operationId = operationId;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_operations[operationId] = result;
    return result;
}

void Http::unregisterOperation(int operationId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_operations.erase(operationId);
}

int Http::computeProgress(const int current, const int total)
{
    if (total <= 0)
        return 0;
    const double value = (static_cast<double>(current) / static_cast<double>(total)) * 100.0;
    return std::clamp(static_cast<int>(value), 0, 100);
}

bool Http::shouldEmitProgress(ticks_t& lastEmit, int progress)
{
    const ticks_t now = stdext::millis();
    if (progress >= 100 || lastEmit == 0 || now - lastEmit >= 100) {
        lastEmit = now;
        return true;
    }
    return false;
}

int Http::get(const std::string& url, int timeout)
{
    if (!timeout)
        timeout = 2;

    if (!m_working.load()) {
        g_logger.error("Http::get called while the client is not running ({})", url);
        return -1;
    }

    int operationId = 0;
    auto result = registerOperation(url, operationId);
    auto headers = m_custom_header;
    g_asyncDispatcher->detach_task([this, url, timeout, result, headers, operationId] {
        auto fetched = syncFetch("GET", url, timeout * 1000, headers, nullptr, false, result->canceled.load());
        finishHttpResult(result, std::move(fetched));
        const std::string error = result->error;
        const std::string body = result->response;
        unregisterOperation(operationId);
        tibia::schedule::post([operationId, url, error, body] {
            g_gameEvents.callGlobalField("g_http", "onGet", operationId, url, error, body);
        });
    });

    return operationId;
}

int Http::post(const std::string& url, const std::string& data, int timeout, bool isJson, bool /*checkContentLength*/)
{
    if (!timeout)
        timeout = 2;
    if (data.empty()) {
        g_logger.error("Invalid post request for {}, empty data, use get instead", url);
        return -1;
    }

    if (!m_working.load()) {
        g_logger.error("Http::post called while the client is not running ({})", url);
        return -1;
    }

    int operationId = 0;
    auto result = registerOperation(url, operationId);
    result->postData = data;

    auto headers = m_custom_header;
    g_asyncDispatcher->detach_task([this, url, data, timeout, isJson, result, headers, operationId] {
        auto fetched = syncFetch("POST", url, timeout * 1000, headers, &data, isJson, result->canceled.load());
        finishHttpResult(result, std::move(fetched));
        const std::string error = result->error;
        const std::string body = result->response;
        unregisterOperation(operationId);
        tibia::schedule::post([operationId, url, error, body] {
            g_gameEvents.callGlobalField("g_http", "onPost", operationId, url, error, body);
        });
    });

    return operationId;
}

int Http::download(const std::string& url, const std::string& path, int timeout)
{
    if (!timeout)
        timeout = 2;

    if (!m_working.load()) {
        g_logger.error("Http::download called while the client is not running ({})", url);
        return -1;
    }

    int operationId = 0;
    auto result = registerOperation(url, operationId);
    auto headers = m_custom_header;

    // Async emscripten_fetch callbacks never reach Lua under PROXY_TO_PTHREAD.
    // Sync fetch on a worker (same as get/post). When HEAD+Range work (CDN CORS
    // OPTIONS), download in chunks and emit onDownloadProgress between chunks so
    // the game thread can update the asset UI bar.
    g_asyncDispatcher->detach_task([this, url, path, timeout, result, headers, operationId] {
        constexpr uint64_t kChunkSize = 1024ull * 1024ull; // 1 MiB
        const int timeoutMs = timeout * 1000;

        auto finishDownload = [this, result, operationId, url, path](SyncFetchResult&& fetched) {
            finishHttpResult(result, std::move(fetched));
            const std::string error = result->error;
            const std::string body = result->response;
            const auto checksum = g_crypt.crc32(body, false);
            const std::string normalizedPath = normalizeDownloadPath(path);
            unregisterOperation(operationId);
            tibia::schedule::post([this, result, operationId, url, error, path, normalizedPath, checksum] {
                if (error.empty())
                    m_downloads[normalizedPath] = result;
                g_gameEvents.callGlobalField("g_http", "onDownload", operationId, url, error, path, checksum);
            });
        };

        if (result->canceled.load()) {
            SyncFetchResult canceled;
            canceled.error = "canceled";
            finishDownload(std::move(canceled));
            return;
        }

        uint64_t totalSize = 0;
        {
            const auto head = syncFetch("HEAD", url, timeoutMs, headers, nullptr, false, result->canceled.load());
            if (head.error.empty() && head.totalBytes > 0)
                totalSize = head.totalBytes;
        }
        if (totalSize == 0) {
            const double probed = qtclientProbeHttpSize(url.c_str());
            if (probed > 0)
                totalSize = static_cast<uint64_t>(probed);
        }

        if (totalSize > 0)
            g_logger.info("Http::download chunked {} bytes from {}", totalSize, url);
        else
            g_logger.warning("Http::download falling back to single GET (no Content-Length) for {}", url);

        ticks_t lastProgressEmit = 0;
        ticks_t lastSpeedSample = stdext::millis();
        int lastBytes = 0;

        auto reportProgress = [this, result, &lastProgressEmit, &lastSpeedSample, &lastBytes](uint64_t current, uint64_t total) {
            if (total == 0)
                return;

            const int progress = computeProgress(
                static_cast<int>(std::min(current, static_cast<uint64_t>(std::numeric_limits<int>::max()))),
                static_cast<int>(std::min(total, static_cast<uint64_t>(std::numeric_limits<int>::max())))
            );

            const ticks_t now = stdext::millis();
            int speed = result->speed.load();
            const ticks_t elapsed = now - lastSpeedSample;
            if (elapsed > 0) {
                const int curBytes = static_cast<int>(std::min(current, static_cast<uint64_t>(std::numeric_limits<int>::max())));
                speed = ((curBytes - lastBytes) * 1000) / static_cast<int>(elapsed);
                lastSpeedSample = now;
                lastBytes = curBytes;
            }
            result->progress = progress;
            result->speed = speed;

            if (!shouldEmitProgress(lastProgressEmit, progress))
                return;

            tibia::schedule::post([result, progress, speed] {
                // Do not drop late progress if finish raced ahead of the queue —
                // the download UI still wants the last percent before onDownload.
                g_gameEvents.callGlobalField("g_http", "onDownloadProgress", result->operationId, result->url, progress, speed);
            });
        };

        if (totalSize > 0) {
            SyncFetchResult aggregated;
            aggregated.status = 200;
            aggregated.body.reserve(static_cast<size_t>(std::min(totalSize, static_cast<uint64_t>(std::numeric_limits<size_t>::max()))));

            uint64_t offset = 0;
            while (offset < totalSize) {
                if (result->canceled.load()) {
                    aggregated.error = "canceled";
                    finishDownload(std::move(aggregated));
                    return;
                }

                const uint64_t end = std::min(offset + kChunkSize, totalSize) - 1;
                auto rangeHeaders = headers;
                rangeHeaders["Range"] = "bytes=" + std::to_string(offset) + "-" + std::to_string(end);

                auto chunk = syncFetch("GET", url, timeoutMs, rangeHeaders, nullptr, false, result->canceled.load());
                if (!chunk.error.empty()) {
                    finishDownload(std::move(chunk));
                    return;
                }

                // Server ignored Range and returned the full body.
                if (chunk.status == 200 && offset == 0 && chunk.body.size() >= totalSize) {
                    aggregated = std::move(chunk);
                    reportProgress(aggregated.body.size(), totalSize);
                    finishDownload(std::move(aggregated));
                    return;
                }

                if (chunk.status != 206 && chunk.status != 200) {
                    chunk.error = "http_status::" + std::to_string(chunk.status);
                    finishDownload(std::move(chunk));
                    return;
                }
                if (chunk.body.empty()) {
                    chunk.error = "http_error::empty_range";
                    finishDownload(std::move(chunk));
                    return;
                }

                aggregated.body.append(chunk.body);
                offset += chunk.body.size();
                reportProgress(offset, totalSize);
            }

            finishDownload(std::move(aggregated));
            return;
        }

        auto fetched = syncFetch("GET", url, timeoutMs, headers, nullptr, false, result->canceled.load());
        finishDownload(std::move(fetched));
    });

    return operationId;
}

EM_BOOL Http::onWebSocketOpen(int, const EmscriptenWebSocketOpenEvent*, void* userData)
{
    auto* context = static_cast<WebSocketContext*>(userData);
    if (!context || !context->result)
        return EM_TRUE;

    auto result = context->result;
    result->connected = true;
    const int operationId = result->operationId;
    tibia::schedule::post([operationId] {
        g_gameEvents.callGlobalField("g_http", "onWsOpen", operationId, "code::websocket_open");
    });
    return EM_TRUE;
}

EM_BOOL Http::onWebSocketMessage(int, const EmscriptenWebSocketMessageEvent* event, void* userData)
{
    auto* context = static_cast<WebSocketContext*>(userData);
    if (!context || !context->result || !event || !event->data || event->numBytes == 0)
        return EM_TRUE;

    const int operationId = context->result->operationId;
    const std::string payload(reinterpret_cast<const char*>(event->data), event->numBytes);
    tibia::schedule::post([operationId, payload] {
        g_gameEvents.callGlobalField("g_http", "onWsMessage", operationId, payload);
    });
    return EM_TRUE;
}

EM_BOOL Http::onWebSocketError(int, const EmscriptenWebSocketErrorEvent*, void* userData)
{
    auto* context = static_cast<WebSocketContext*>(userData);
    if (!context || !context->result)
        return EM_TRUE;

    if (!context->errorSent) {
        context->errorSent = true;
        const int operationId = context->result->operationId;
        const std::string errorReason = "close_code::error websocket";
        context->result->error = errorReason;
        tibia::schedule::post([operationId, errorReason] {
            g_gameEvents.callGlobalField("g_http", "onWsError", operationId, errorReason);
        });
    }

    if (context->socket > 0)
        emscripten_websocket_close(context->socket, 1000, "error");
    return EM_TRUE;
}

void Http::cleanupWebSocket(WebSocketContext* context, const std::string& closeMessage, bool dispatchClose)
{
    if (!context || !context->result)
        return;

    const int operationId = context->result->operationId;
    std::shared_ptr<WebSocketContext> owner;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_websockets.find(operationId);
        if (it != m_websockets.end() && it->second.get() == context) {
            owner = it->second;
            m_websockets.erase(it);
        }
        m_operations.erase(operationId);
    }

    if (dispatchClose && m_working.load()) {
        tibia::schedule::post([operationId, closeMessage] {
            g_gameEvents.callGlobalField("g_http", "onWsClose", operationId, closeMessage);
        });
    }
}

EM_BOOL Http::onWebSocketClose(int, const EmscriptenWebSocketCloseEvent*, void* userData)
{
    auto* context = static_cast<WebSocketContext*>(userData);
    if (!context || !context->http)
        return EM_TRUE;

    context->http->cleanupWebSocket(context, "close_code::normal", !context->errorSent);
    return EM_TRUE;
}

int Http::ws(const std::string& url, int timeout)
{
    if (!timeout)
        timeout = 2;

    if (!m_working.load()) {
        g_logger.error("Http::ws called while the client is not running ({})", url);
        return -1;
    }

    int operationId = 0;
    auto result = registerOperation(url, operationId);
    auto context = std::make_shared<WebSocketContext>();
    context->http = this;
    context->result = result;

    EmscriptenWebSocketCreateAttributes attributes = {
        url.c_str(),
        nullptr,
        EM_FALSE,
    };

    const EMSCRIPTEN_WEBSOCKET_T socket = emscripten_websocket_new(&attributes);
    if (socket <= 0) {
        const std::string errorReason = "close_code::error websocket_create";
        unregisterOperation(operationId);
        tibia::schedule::post([operationId, errorReason] {
            g_gameEvents.callGlobalField("g_http", "onWsError", operationId, errorReason);
        });
        return operationId;
    }

    context->socket = socket;
    auto* rawContext = context.get();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_websockets[operationId] = context;
    }

    emscripten_websocket_set_onopen_callback(socket, rawContext, &Http::onWebSocketOpen);
    emscripten_websocket_set_onmessage_callback(socket, rawContext, &Http::onWebSocketMessage);
    emscripten_websocket_set_onerror_callback(socket, rawContext, &Http::onWebSocketError);
    emscripten_websocket_set_onclose_callback(socket, rawContext, &Http::onWebSocketClose);

    return operationId;
}

bool Http::wsSend(int operationId, const std::string& message)
{
    EMSCRIPTEN_WEBSOCKET_T socket = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_websockets.find(operationId);
        if (it != m_websockets.end() && it->second)
            socket = it->second->socket;
    }

    if (socket <= 0)
        return false;

    return emscripten_websocket_send_utf8_text(socket, message.c_str()) == EMSCRIPTEN_RESULT_SUCCESS;
}

bool Http::wsClose(int operationId)
{
    cancel(operationId);
    return true;
}

bool Http::cancel(int id)
{
    EMSCRIPTEN_WEBSOCKET_T socket = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_operations.find(id);
        if (it != m_operations.end() && it->second)
            it->second->canceled = true;
        const auto wit = m_websockets.find(id);
        if (wit != m_websockets.end() && wit->second)
            socket = wit->second->socket;
    }

    if (socket > 0)
        emscripten_websocket_close(socket, 1000, "cancel");

    return true;
}

#else

namespace {
QNetworkAccessManager& networkAccess()
{
    static QNetworkAccessManager manager;
    return manager;
}

std::unordered_map<int, QNetworkReply*>& liveReplies()
{
    static std::unordered_map<int, QNetworkReply*> replies;
    return replies;
}

QNetworkRequest makeRequest(const std::string& url, const std::string& userAgent,
    const std::unordered_map<std::string, std::string>& headers)
{
    QNetworkRequest request{ QUrl(QString::fromStdString(url)) };
    request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromStdString(userAgent));
    for (const auto& [name, value] : headers)
        request.setRawHeader(QByteArray::fromStdString(name), QByteArray::fromStdString(value));
    return request;
}

void armTimeout(QNetworkReply* reply, int timeoutSeconds)
{
    auto* timer = new QTimer(reply);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, reply, [reply] { reply->abort(); });
    timer->start(std::max(1, timeoutSeconds) * 1000);
}
}

void Http::init()
{
    if (m_working.load())
        return;
    networkAccess();
    m_working.store(true);
}

void Http::terminate()
{
    if (!m_working.load())
        return;
    m_working.store(false);
    std::vector<QNetworkReply*> replies;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& entry : m_operations) {
            if (entry.second)
                entry.second->canceled = true;
        }
        for (auto& entry : liveReplies())
            replies.push_back(entry.second);
        liveReplies().clear();
        m_operations.clear();
        m_downloads.clear();
    }
    for (QNetworkReply* reply : replies) {
        if (reply)
            reply->abort();
    }
}

int Http::get(const std::string& url, int timeout)
{
    if (!timeout)
        timeout = 2;
    if (!m_working.load()) {
        g_logger.error("Http::get called while the client is not running ({})", url);
        return -1;
    }
    int operationId = 0;
    auto result = registerOperation(url, operationId);
    QNetworkReply* reply = networkAccess().get(makeRequest(url, m_userAgent, m_custom_header));
    liveReplies()[operationId] = reply;
    armTimeout(reply, timeout);
    const auto lastProgress = std::make_shared<ticks_t>(0);
    QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [this, result, lastProgress](qint64 current, qint64 total) {
        const int progress = computeProgress(static_cast<int>(current), static_cast<int>(total));
        if (result->finished.load() || result->canceled.load())
            return;
        result->progress = progress;
        if (!shouldEmitProgress(*lastProgress, progress))
            return;
        tibia::schedule::post([result, progress] {
            if (result->finished.load() || result->canceled.load())
                return;
            g_gameEvents.callGlobalField("g_http", "onGetProgress", result->operationId, result->url, progress);
        });
    });
    QObject::connect(reply, &QNetworkReply::finished, reply, [this, operationId, result, reply] {
        result->finished = true;
        const QByteArray body = reply->readAll();
        result->status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        result->response.assign(body.constData(), static_cast<size_t>(body.size()));
        result->size = static_cast<int>(body.size());
        result->progress = 100;
        std::string error;
        if (result->canceled.load() || reply->error() == QNetworkReply::OperationCanceledError)
            error = "canceled";
        else if (reply->error() != QNetworkReply::NoError)
            error = reply->errorString().toStdString();
        result->error = error;
        liveReplies().erase(operationId);
        unregisterOperation(operationId);
        const std::string payload = result->response;
        tibia::schedule::post([result, error, payload] {
            g_gameEvents.callGlobalField("g_http", "onGet", result->operationId, result->url, error, payload);
        });
        reply->deleteLater();
    });
    return operationId;
}

int Http::post(const std::string& url, const std::string& data, int timeout, bool isJson, bool)
{
    if (!timeout)
        timeout = 2;
    if (data.empty()) {
        g_logger.error("Invalid post request for {}, empty data, use get instead", url);
        return -1;
    }
    if (!m_working.load()) {
        g_logger.error("Http::post called while the client is not running ({})", url);
        return -1;
    }
    int operationId = 0;
    auto result = registerOperation(url, operationId);
    result->postData = data;
    auto headers = m_custom_header;
    headers["Accept"] = "*/*";
    headers["Content-Type"] = isJson ? "application/json" : "application/x-www-form-urlencoded";
    QNetworkReply* reply = networkAccess().post(
        makeRequest(url, m_userAgent, headers),
        QByteArray(data.data(), static_cast<int>(data.size())));
    liveReplies()[operationId] = reply;
    armTimeout(reply, timeout);
    QObject::connect(reply, &QNetworkReply::finished, reply, [this, operationId, result, reply] {
        result->finished = true;
        const QByteArray body = reply->readAll();
        result->status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        result->response.assign(body.constData(), static_cast<size_t>(body.size()));
        result->size = static_cast<int>(body.size());
        std::string error;
        if (result->canceled.load() || reply->error() == QNetworkReply::OperationCanceledError)
            error = "canceled";
        else if (reply->error() != QNetworkReply::NoError)
            error = reply->errorString().toStdString();
        result->error = error;
        liveReplies().erase(operationId);
        unregisterOperation(operationId);
        const std::string payload = result->response;
        tibia::schedule::post([result, error, payload] {
            g_gameEvents.callGlobalField("g_http", "onPost", result->operationId, result->url, error, payload);
        });
        reply->deleteLater();
    });
    return operationId;
}

int Http::download(const std::string& url, const std::string& path, int timeout)
{
    if (!timeout)
        timeout = 5;
    if (!m_working.load()) {
        g_logger.error("Http::download called while the client is not running ({})", url);
        return -1;
    }
    int operationId = 0;
    auto result = registerOperation(url, operationId);
    m_downloads[path] = result;
    QNetworkReply* reply = networkAccess().get(makeRequest(url, m_userAgent, m_custom_header));
    liveReplies()[operationId] = reply;
    armTimeout(reply, timeout);
    const auto lastProgress = std::make_shared<ticks_t>(0);
    QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [this, result, lastProgress](qint64 current, qint64 total) {
        const int progress = computeProgress(static_cast<int>(current), static_cast<int>(total));
        if (result->finished.load() || result->canceled.load())
            return;
        result->progress = progress;
        if (!shouldEmitProgress(*lastProgress, progress))
            return;
        tibia::schedule::post([result, progress] {
            if (result->finished.load() || result->canceled.load())
                return;
            g_gameEvents.callGlobalField("g_http", "onDownloadProgress", result->operationId, result->url, progress, 0);
        });
    });
    QObject::connect(reply, &QNetworkReply::finished, reply, [this, operationId, result, reply, path] {
        result->finished = true;
        const QByteArray body = reply->readAll();
        result->response.assign(body.constData(), static_cast<size_t>(body.size()));
        result->size = static_cast<int>(body.size());
        std::string error;
        if (result->canceled.load() || reply->error() == QNetworkReply::OperationCanceledError)
            error = "canceled";
        else if (reply->error() != QNetworkReply::NoError)
            error = reply->errorString().toStdString();
        else {
            QFile file(QString::fromStdString(path));
            if (!file.open(QIODevice::WriteOnly) || file.write(body) != body.size())
                error = "failed to write " + path;
        }
        result->error = error;
        liveReplies().erase(operationId);
        unregisterOperation(operationId);
        const std::string checksum = error.empty() ? g_crypt.crc32(result->response, false) : std::string{};
        tibia::schedule::post([result, error, path, checksum] {
            g_gameEvents.callGlobalField("g_http", "onDownload", result->operationId, result->url, error, path, checksum);
        });
        reply->deleteLater();
    });
    return operationId;
}

int Http::ws(const std::string& url, int)
{
    g_logger.error("Http::ws is not available ({}); the game socket is QTcpSocket", url);
    return -1;
}

bool Http::wsSend(int, const std::string&) { return false; }

bool Http::wsClose(int operationId)
{
    return cancel(operationId);
}

bool Http::cancel(int id)
{
    QNetworkReply* reply = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_operations.find(id);
        if (it != m_operations.end() && it->second)
            it->second->canceled = true;
        const auto replyIt = liveReplies().find(id);
        if (replyIt != liveReplies().end())
            reply = replyIt->second;
    }
    if (reply)
        reply->abort();
    return reply != nullptr;
}

int Http::computeProgress(const int current, const int total)
{
    if (total <= 0)
        return 0;
    return std::clamp(current * 100 / total, 0, 100);
}

HttpResult_ptr Http::registerOperation(const std::string& url, int& operationId)
{
    auto result = std::make_shared<HttpResult>();
    std::lock_guard<std::mutex> lock(m_mutex);
    operationId = m_operationId.fetch_add(1);
    result->operationId = operationId;
    result->url = url;
    m_operations[operationId] = result;
    return result;
}

void Http::unregisterOperation(int operationId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_operations.erase(operationId);
}

bool Http::shouldEmitProgress(ticks_t& lastEmit, int progress)
{
    const ticks_t now = stdext::millis();
    if (progress >= 100 || lastEmit == 0 || now - lastEmit >= 100) {
        lastEmit = now;
        return true;
    }
    return false;
}

#endif
