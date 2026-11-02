// WASM MAIN_MODULE stubs for symbols SIDE_MODULE packs import via env.
// Qt wasm builds often omit EH; packs linked with libc++ may still import
// __cxa_throw. host_exports.json keepalive can only export defined symbols.
// Do not abort the Soft tip — fail-closed ModLoader must leave stock chrome up.
#if defined(__EMSCRIPTEN__)

#include <cstdio>

extern "C" void __cxa_throw(void* thrown, void* /*tinfo*/, void (*dest)(void*))
{
    std::fputs(
        "tibia.wasm: __cxa_throw from SIDE_MODULE (host has no EH); "
        "destroying thrown object and continuing\n",
        stderr);
    if (dest && thrown)
        dest(thrown);
}

#endif
