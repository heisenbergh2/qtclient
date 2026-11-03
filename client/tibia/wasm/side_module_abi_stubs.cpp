// WASM MAIN_MODULE stubs for symbols SIDE_MODULE packs import via env.
// Qt wasm builds often omit EH; packs linked with libc++ may still import
// __cxa_throw. host_exports.json keepalive can only export defined symbols.
// Do not abort the Soft tip — fail-closed ModLoader must leave stock chrome up.
#if defined(__EMSCRIPTEN__)

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <emscripten.h>

extern "C" void __cxa_throw(void* thrown, void* /*tinfo*/, void (*dest)(void*))
{
    std::fputs(
        "tibia.wasm: __cxa_throw from SIDE_MODULE (host has no EH); "
        "destroying thrown object and continuing\n",
        stderr);
    if (dest && thrown)
        dest(thrown);
}

// Soft never calls memcmp in a way that survives DCE/inlining as an exportable
// wasm symbol (&std::memcmp keepalive was insufficient — Client:ci still missed
// it). Provide a Soft-owned definition so host_exports --export=memcmp works and
// IdleOT SIDE_MODULE env imports resolve. libc's memcmp is not linked when this
// strong definition is present.
extern "C" EMSCRIPTEN_KEEPALIVE int memcmp(const void* s1, const void* s2, std::size_t n)
{
    const auto* a = static_cast<const std::uint8_t*>(s1);
    const auto* b = static_cast<const std::uint8_t*>(s2);
    for (std::size_t i = 0; i < n; ++i) {
        if (a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}

#endif
