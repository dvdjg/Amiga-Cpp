#pragma once

/// \file types.hpp
/// Tipos fundamentales del engine.
///
/// Esta cabecera evita depender de `stdint.h` o de la STL porque el toolchain
/// `m68k-amiga-elf` del plugin no ofrece un entorno C++ hosted completo. La idea
/// es que todo el codigo compartido use estos alias y structs pequenos, de modo
/// que luego pueda compilarse tambien para otros backends retro.

namespace amg {

/// Enteros con tamano esperado en 68000/Bartman GCC.
///
/// En m68k-amiga-elf `short` es de 16 bits y `long` de 32 bits. Usamos tipos del
/// lenguaje en vez de cabeceras estandar para mantener el runtime freestanding.
using u8 = unsigned char;
using u16 = unsigned short;
using u32 = unsigned long;
using s8 = signed char;
using s16 = signed short;
using s32 = signed long;
using usize = __SIZE_TYPE__;
using uintptr = __UINTPTR_TYPE__;

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

} // namespace amg
