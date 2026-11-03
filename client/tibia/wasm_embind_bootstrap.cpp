// SPDX-License-Identifier: GPL-2.0-or-later
//
// Embind registers primitives in EMSCRIPTEN_BINDINGS(builtin) via a C++ static
// InitFunc (default init priority). Other TUs (Qt wasm platform) can run
// dynamic initializers first and call emscripten::val::as<uint32_t>() — Itanium
// mangled name "j" — while registeredTypes is still empty, aborting
// __wasm_call_ctors ("emval::as has unknown type j").
//
// GCC/Clang constructor(101) runs before C++ priority-65535 dynamic init, so
// we mirror embind's builtin registrations here. browser/embind-ignore-dupes.js
// makes the later EMSCRIPTEN_BINDINGS(builtin) pass a no-op on duplicates.

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <cstdint>
#include <limits>
#include <string>

namespace {

template <typename T>
void registerInteger(const char *name)
{
    emscripten::internal::_embind_register_integer(
        emscripten::internal::TypeID<T>::get(), name, sizeof(T),
        std::numeric_limits<T>::min(), std::numeric_limits<T>::max());
}

template <typename T>
void registerBigint(const char *name)
{
    emscripten::internal::_embind_register_bigint(
        emscripten::internal::TypeID<T>::get(), name, sizeof(T),
        std::numeric_limits<T>::min(), std::numeric_limits<T>::max());
}

template <typename T>
void registerFloat(const char *name)
{
    emscripten::internal::_embind_register_float(
        emscripten::internal::TypeID<T>::get(), name, sizeof(T));
}

void registerPrimitives()
{
    using emscripten::internal::TypeID;
    using emscripten::internal::_embind_register_bool;
    using emscripten::internal::_embind_register_emval;
    using emscripten::internal::_embind_register_std_string;
    using emscripten::internal::_embind_register_std_wstring;
    using emscripten::internal::_embind_register_void;

    _embind_register_void(TypeID<void>::get(), "void");
    _embind_register_bool(TypeID<bool>::get(), "bool", true, false);

    registerInteger<char>("char");
    registerInteger<signed char>("signed char");
    registerInteger<unsigned char>("unsigned char");
    registerInteger<signed short>("short");
    registerInteger<unsigned short>("unsigned short");
    registerInteger<signed int>("int");
    registerInteger<unsigned int>("unsigned int");
#if __wasm64__
    registerBigint<signed long>("long");
    registerBigint<unsigned long>("unsigned long");
#else
    registerInteger<signed long>("long");
    registerInteger<unsigned long>("unsigned long");
#endif
    registerBigint<int64_t>("int64_t");
    registerBigint<uint64_t>("uint64_t");

    registerFloat<float>("float");
    registerFloat<double>("double");

    _embind_register_std_string(TypeID<std::string>::get(), "std::string");
    _embind_register_std_wstring(TypeID<std::wstring>::get(), sizeof(wchar_t),
                                 "std::wstring");
    _embind_register_std_wstring(TypeID<std::u16string>::get(), sizeof(char16_t),
                                 "std::u16string");
    _embind_register_std_wstring(TypeID<std::u32string>::get(), sizeof(char32_t),
                                 "std::u32string");
    _embind_register_emval(TypeID<emscripten::val>::get());
}

} // namespace

// Priority 101: before C++ dynamic initializers (default 65535).
__attribute__((constructor(101))) static void softEmbindBootstrap()
{
    registerPrimitives();
}
