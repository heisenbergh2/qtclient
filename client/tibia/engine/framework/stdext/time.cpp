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

#include "time.h"

#ifndef USE_PRECOMPILED_HEADERS
#include <chrono>
#include <ctime>
#include <thread>
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace stdext
{
#ifdef __EMSCRIPTEN__
    // Browser monotonic clock (ms since page load). Never throws — unlike
    // libc++ steady_clock::now(), which is noexcept but may __throw_system_error
    // on clock_gettime failure → std::terminate (seen as abort in
    // Game::isConnectionOk → timer::elapsed_millis → micros → steady_clock::now).
    static const double startup_ms = emscripten_get_now();

    ticks_t time() { return std::time(nullptr); }

    ticks_t millis()
    {
        return static_cast<ticks_t>(emscripten_get_now() - startup_ms);
    }

    ticks_t micros()
    {
        return static_cast<ticks_t>((emscripten_get_now() - startup_ms) * 1000.0);
    }
#else
    const static auto startup_time = std::chrono::high_resolution_clock::now();

    ticks_t time() { return std::time(nullptr); }

    ticks_t millis()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - startup_time).count();
    }

    ticks_t micros()
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - startup_time).count();
    }
#endif

    void microsleep(const size_t us) { std::this_thread::sleep_for(std::chrono::microseconds(us)); }

    void millisleep(const size_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
}
