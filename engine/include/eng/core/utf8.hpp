#pragma once

/// \file utf8.hpp
/// Decodificador UTF-8 freestanding (sin STL) para las funciones de texto.
///
/// Las cadenas del engine (literales, assets, teclado) pueden venir en UTF-8.
/// Las fuentes del engine cubren ASCII + LATIN-1 (ISO-8859-1): este decodificador
/// extrae el punto de código de una secuencia UTF-8 de 1..4 bytes y lo devuelve
/// como `u32`; el llamador lo pasa a la fuente (p. ej. `Font8::row`), que
/// devuelve 0 para puntos fuera de su cobertura.
///
/// Cubre el subconjunto que el engine expone (U+0000..U+00FF, LATIN-1) y además
/// decodifica correctamente cualquier punto de 1..4 bytes por si una fuente
/// futura amplía el rango (LATIN-N, etc.). En ese caso `row()` devuelve 0 hasta
/// que exista glifo.

#include <eng/core/types.hpp>

namespace eng::utf8 {

/// Devuelve el punto de código de la secuencia UTF-8 en `*p` y avanza `*p` al
/// siguiente code point. Si la secuencia es inválida devuelve 0 (y avanza 1
/// byte, el byte inválido).
///
/// Uso:
///   const u8* p = reinterpret_cast<const u8*>(text);
///   while (u32 cp = decode(p)) {
///       const u8 glifo = Font8::row(static_cast<u16>(cp), fila);
///       ...
///   }
inline u32 decode(const u8*& p) {
    const u8 b0 = *p++;
    if (b0 < 0x80u) {
        return b0; // ASCII: 1 byte
    }
    if ((b0 & 0xE0u) == 0xC0u) {
        // 2 bytes (U+0080..U+07FF)
        const u8 b1 = *p;
        if ((b1 & 0xC0u) != 0x80u) {
            return 0;
        }
        ++p;
        return static_cast<u32>((b0 & 0x1Fu) << 6u) |
               static_cast<u32>(b1 & 0x3Fu);
    }
    if ((b0 & 0xF0u) == 0xE0u) {
        // 3 bytes (U+0800..U+FFFF)
        const u8 b1 = *p;
        const u8 b2 = *(p + 1u);
        if ((b1 & 0xC0u) != 0x80u || (b2 & 0xC0u) != 0x80u) {
            return 0;
        }
        p += 2u;
        return static_cast<u32>(b0 & 0x0Fu) << 12u |
               static_cast<u32>(b1 & 0x3Fu) << 6u |
               static_cast<u32>(b2 & 0x3Fu);
    }
    if ((b0 & 0xF8u) == 0xF0u) {
        // 4 bytes (U+10000..U+10FFFF)
        const u8 b1 = *p;
        const u8 b2 = *(p + 1u);
        const u8 b3 = *(p + 2u);
        if ((b1 & 0xC0u) != 0x80u || (b2 & 0xC0u) != 0x80u ||
            (b3 & 0xC0u) != 0x80u) {
            return 0;
        }
        p += 3u;
        return static_cast<u32>(b0 & 0x07u) << 18u |
               static_cast<u32>(b1 & 0x3Fu) << 12u |
               static_cast<u32>(b2 & 0x3Fu) << 6u |
               static_cast<u32>(b3 & 0x3Fu);
    }
    return 0; // byte de continuación suelto o secuencia inválida
}

} // namespace eng::utf8