#pragma once

/// \file types.hpp
/// Tipos fundamentales del engine.
///
/// Esta cabecera evita depender de `stdint.h` o de la STL porque el toolchain
/// `m68k-amiga-elf` del plugin no ofrece un entorno C++ hosted completo. La idea
/// es que todo el codigo compartido use estos alias y structs pequenos, de modo
/// que luego pueda compilarse tambien para otros backends retro.

namespace eng {

/// Enteros con ancho exacto, portables entre m68k y host LP64.
///
/// Se usan los builtins `__UINT*_TYPE__`/`__INT*_TYPE__` en vez de los tipos del
/// lenguaje porque el ancho de `int`/`long` cambia entre plataformas: en
/// m68k-amiga-elf `int` es de 16 bits y `long` de 32, pero en un host Linux LP64
/// `long` es de 64. Los builtins garantizan 8/16/32/64 bits en ambos y siguen sin
/// depender de cabeceras de la libreria estandar (runtime freestanding).
using u8 = __UINT8_TYPE__;
using u16 = __UINT16_TYPE__;
using u32 = __UINT32_TYPE__;
using u64 = __UINT64_TYPE__;
using s8 = __INT8_TYPE__;
using s16 = __INT16_TYPE__;
using s32 = __INT32_TYPE__;
using s64 = __INT64_TYPE__;
using usize = __SIZE_TYPE__;
using uintptr = __UINTPTR_TYPE__;

/// **Coordenada/índice de píxel de pantalla** dependiente de la plataforma.
///
/// En **68000** conviene `s16`: las coordenadas de pantalla caben de sobra (320×256, 640×512) y la
/// aritmética de 16 bits es mucho más barata que la de 32 (no arrastra `mulu32`/`divs32` ni
/// registros completos). En **host** (64 bits) se usa `s32` para no desbordar en tests y algoritmos
/// grandes. No es «el `int` de C» (dependiente de ABI): es explícito por plataforma.
///
/// No confundir con `eng::coord` (coordenada de simulación, `Fixed`) ni con `eng::intw` (entero de
/// palabra natural): `pix` es el tipo de dominio de las primitivas de rasterizado.
///
/// Regla de uso: el **tipo de dominio** de coordenadas de píxel es `eng::pix`; los **intermedios que
/// pueden desbordar** (`dx*dy`, divisiones de línea, áreas, `x + w`) se calculan en `s32`/`s64`
/// **local y explícito**, con comentario. Ver `docs/guides/optimization/OPTIMIZACION_GPP_68000.md` §12.
#if defined(__m68k__)
using pix = s16;
#else
using pix = s32;
#endif

namespace detail {
/// `is_same` minimo (sin STL): lo usan `Ref`/`Span` para restringir sus conversiones.
template <typename A, typename B>
struct same {
	static constexpr bool value = false;
};
template <typename A>
struct same<A, A> {
	static constexpr bool value = true;
};
} // namespace detail

/// Tamano 2D pequeno para resoluciones, tiles, sprites y buffers.
struct Size2u {
	u16 width;
	u16 height;
};

/// Punto 2D con signo. Suficiente para coordenadas de pantalla y offsets locales.
struct Point2s {
	s16 x;
	s16 y;
};

/// Resultado comun para APIs del engine que no deben lanzar excepciones.
///
/// Las excepciones estan desactivadas en el runtime Amiga. Las funciones que puedan
/// fallar devuelven valores explicitos o bloques invalidos.
enum class Result : u8 {
	Ok,
	OutOfMemory,
	InvalidArgument,
	Unsupported,
	HardwareLimit,
};

/// Alinea un entero hacia arriba.
///
/// Se usa para asegurar que bitplanes, copperlists, sprites y buffers del blitter
/// quedan en limites apropiados. El valor de `alignment` debe ser potencia de dos.
constexpr u32 align_up(u32 value, u32 alignment) {
	return (value + alignment - 1u) & ~(alignment - 1u);
}

/// Variante de `align_up` para direcciones.
constexpr uintptr align_up_ptr(uintptr value, uintptr alignment) {
	return (value + alignment - 1u) & ~(alignment - 1u);
}

} // namespace eng
