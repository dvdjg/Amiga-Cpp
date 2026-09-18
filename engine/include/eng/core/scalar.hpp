#pragma once

/// \file scalar.hpp
/// **Tipos generales** de la matematica, seleccionables en **compilacion** segun el
/// ancho de palabra objetivo. Su razon de ser es no repetir el codigo de simulacion por
/// target: en 68000 se eligen los escalares retro optimizados (`s16`, `Fixed<s16,E>`), en
/// 68020/m68k de 32 bits un `Fixed<s32,E>`, y en host/x64 los tipos nativos (`int`,
/// `float`). Los algoritmos siguen siendo **plantillas** (reciben `S`); estos alias son
/// solo la instanciacion por defecto que usan demos/juegos.
///
/// Seleccion (por target o forzada con `-D`):
///
///   ENG_SCALAR_RETRO16   s16 / Fixed<s16,12> / Fixed<s16,0>   (68000; defecto en m68k 16 bits)
///   ENG_SCALAR_RETRO32   s32 / Fixed<s32,12> / Fixed<s32,0>   (68020; defecto en m68k 32 bits)
///   ENG_SCALAR_NATIVE    int / float / int                    (host x86-64; defecto fuera de m68k)
///
/// Compilar en host con `-DENG_SCALAR_RETRO16` **simula** la version de 16 bits (lo que
/// hacen los tests con q12/MiniFloat16); sin macro en host sale la version nativa.
///
/// Regla de alcance: esto es para **escalares de simulacion**. Los tipos de **dominio**
/// (pixeles, tiles, registros, layout/ABI de hardware) siguen a 16 bits (`u8`/`s16`/`u16`)
/// y **no** se generalizan. Ver `docs/guides/roadmap/REFACTOR_SCALAR_GENERICO.md`.

#include <eng/core/fixed.hpp>
#include <eng/core/types.hpp>

// Modo efectivo: 1 = retro16, 2 = retro32, 3 = native.
#if defined(ENG_SCALAR_NATIVE)
#define ENG__SCALAR_MODE 3
#elif defined(ENG_SCALAR_RETRO16)
#define ENG__SCALAR_MODE 1
#elif defined(ENG_SCALAR_RETRO32)
#define ENG__SCALAR_MODE 2
#elif defined(__mc68020__)
#define ENG__SCALAR_MODE 2
#elif defined(__m68k__)
#define ENG__SCALAR_MODE 1
#else
#define ENG__SCALAR_MODE 3
#endif

namespace eng {

#if ENG__SCALAR_MODE == 1
inline constexpr const char* scalar_mode = "retro16";
/// Entero de palabra natural (equivalente al `int` de C): 16 bits en 68000.
using intw = s16;
/// Fraccionario por defecto: fixed 4.12 de 16 bits.
using real = math::Fixed<s16, 12>;
/// Coordenada de simulacion (unitaria).
using coord = math::Fixed<s16, 0>;
#elif ENG__SCALAR_MODE == 2
inline constexpr const char* scalar_mode = "retro32";
using intw = s32;
using real = math::Fixed<s32, 12>;
using coord = math::Fixed<s32, 0>;
#else
inline constexpr const char* scalar_mode = "native";
using intw = int;
using real = float;
using coord = int;
#endif

} // namespace eng

#undef ENG__SCALAR_MODE
