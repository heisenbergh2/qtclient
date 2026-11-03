// WASM MAIN_MODULE stubs for symbols SIDE_MODULE packs import via env.
// Qt wasm builds often omit EH; packs linked with libc++ may still import
// __cxa_throw. host_exports.json keepalive can only export defined symbols.
// Do not abort the Soft tip — fail-closed ModLoader must leave stock chrome up.
#if defined(__EMSCRIPTEN__)

#include <cstdio>
#include <cstring>

extern "C" void __cxa_throw(void* thrown, void* /*tinfo*/, void (*dest)(void*))
{
    std::fputs(
        "tibia.wasm: __cxa_throw from SIDE_MODULE (host has no EH); "
        "destroying thrown object and continuing\n",
        stderr);
    if (dest && thrown)
        dest(thrown);
}

// Soft may never call memcmp (inlined / DCE). IdleOT SIDE_MODULEs still import
// it via env; --export-if-defined=memcmp only works if the symbol is linked.
// Keep a live reference so host_exports.rsp can export it (Client:ci wasm).
extern "C" {
__attribute__((used)) static void* const qtclient_keepalive_memcmp =
    reinterpret_cast<void*>(static_cast<int (*)(const void*, const void*, std::size_t)>(
        &std::memcmp));
}

#endif
