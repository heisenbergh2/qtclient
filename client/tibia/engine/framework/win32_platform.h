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

// Single Windows include order for every engine TU (including those that
// /FI framework/pch.h). winsock2 before windows.h; full SDK (no
// WIN32_LEAN_AND_MEAN) so minizip/miniaudio see a complete surface; strip
// rpcndr's `small`/`byte` macros so CIP/C++ identifiers stay usable.

#ifndef NOMINMAX
#define NOMINMAX
#endif

// No WIN32_LEAN_AND_MEAN: deps need a complete SDK surface; rpcndr's `small`
// is cleared with #undef below instead of excluding headers.

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

// rpcndr.h defines `small` as a macro (char). Leave FAR/near/far alone —
// zlib's zconf.h needs FAR to stay an empty macro (undef'ing `far` turns
// `typedef Byte FAR Bytef` into a bogus identifier).
#ifdef small
#undef small
#endif
