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

#include "event.h"

#include <algorithm>

#include <QCoreApplication>
#include <QMetaObject>
#include <QThread>
#include <QTimer>

Event::Event(std::function<void()> callback, std::string function) :
    m_function(std::move(function)),
    m_callback(std::move(callback)) {}

Event::~Event()
{
    stopTimer();
}

void Event::stopTimer()
{
    if (!m_timer)
        return;
    m_timer->stop();
    m_timer->deleteLater();
    m_timer = nullptr;
}

void Event::execute()
{
    stopTimer();
    if (!m_canceled && !m_executed && m_callback) {
        auto callback = std::move(m_callback);
        m_executed = true;
        callback();
    }
    m_callback = nullptr;
}

void Event::cancel()
{
    m_canceled = true;
    stopTimer();
    m_callback = nullptr;
}

void Event::armQueued()
{
    const auto self = std::static_pointer_cast<Event>(shared_from_this());
    auto* app = QCoreApplication::instance();
    if (!app) {
        self->execute();
        return;
    }
    QMetaObject::invokeMethod(app, [self] { self->execute(); }, Qt::QueuedConnection);
}

void Event::armTimer(const int delayMs, const bool repeat)
{
    const auto self = std::static_pointer_cast<Event>(shared_from_this());
    auto start = [self, delayMs, repeat] {
        if (self->isCanceled())
            return;
        auto* app = QCoreApplication::instance();
        auto* timer = new QTimer(app);
        self->m_timer = timer;
        timer->setSingleShot(!repeat);
        timer->setInterval(std::max(0, delayMs));
        QObject::connect(timer, &QTimer::timeout, timer, [self, repeat] {
            if (self->isCanceled())
                return;
            if (repeat) {
                if (self->m_callback)
                    self->m_callback();
                return;
            }
            self->execute();
        });
        timer->start();
    };
    auto* app = QCoreApplication::instance();
    if (!app || QThread::currentThread() == app->thread())
        start();
    else
        QMetaObject::invokeMethod(app, std::move(start), Qt::QueuedConnection);
}
