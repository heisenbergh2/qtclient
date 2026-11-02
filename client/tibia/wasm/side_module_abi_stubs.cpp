// WASM MAIN_MODULE stubs for symbols SIDE_MODULE packs import via env.
// Qt wasm builds often omit EH; packs linked with libc++ may still import
// __cxa_throw. host_exports.json keepalive can only export defined symbols.
#if defined(__EMSCRIPTEN__)

#include <cstdlib>

extern "C" void __cxa_throw(void* /*thrown*/, void* /*tinfo*/, void (* /*dest*/)(void*))
{
    std::abort();
}

#endif
