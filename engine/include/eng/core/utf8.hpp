#pragma once

/// \file utf8.hpp
/// Decodificador UTF-8 mínimo para las funciones de texto (cadenas que vienen
/// de editores en UTF-8 sin BOM).
///
/// Solo interesa el subconjunto que cubren las fuentes del engine (ASCII +
/// LATIN-1, U+0000..U+00FF), porque con eso basta para mostrar bien acentos,
/// diéresis y ñ/Ñ en cualquier config de texto. Se implementa el caso común de
/// forma COMPACTA y barata en 68000:
///
///   - byte 0x00..0x7F              → ese mismo byte (ASCII, 1 byte)
///   - byte 0xC2..0xDF + continuación → code point 2 bytes latino (U+0080..U+07FF;
///                                       de estos solo alcanzan LATIN-1, ≤0xFF)
///   - cualquier otra cosa           → 0 (sin avance de bytes extra)
///
/// Los editores (VS Code…) guardan UTF-8 sin BOM; una cadena con solo caracteres
/// ASCII+LATIN-1 decodifica correctamente con esta función y sin coste.

#include <eng/core/types.hpp>

namespace eng::utf8 {

/// Siguiente code point de `*p`, avanzando `*p` los bytes consumidos (1 o 2).
///
/// Devuelve 0 al llegar al final o ante una secuencia fuera del subconjunto
/// (no consume el byte inválido; el llamador puede avanzar 1 si quisiera). Para
/// LATIN-1 (el subconjunto del engine) es suficiente.
inline u32 decode(const u8*& p) {
    const u8 b0 = *p;
    if (b0 < 0x80u) {
        ++p;
        return b0; // ASCII: 1 byte
    }
    // 2 bytes: 0xC2..0xDF + 10xxxxxx -> U+0080..U+07FF.
    // De aquí nos interesa U+0080..U+00FF (LATIN-1), que las fuentes pintan.
    if (b0 >= 0xC2u && b0 <= 0xDFu) {
        const u8 b1 = *(p + 1u);
        if ((b1 & 0xC0u) == 0x80u) {
            p += 2u;
            return static_cast<u32>((b0 & 0x1Fu) << 6u) |
                   static_cast<u32>(b1 & 0x3Fu);
        }
        // Continuación inválida: no consumimos el byte; terminamos.
        return 0;
    }
    // Fuera del subconjunto (3/4 bytes, o byte de continuación suelto): el
    // subconjunto del engine no lo pinta. Devolvemos 0 sin consumir extra.
    ++p;
    return 0;
}

} // namespace eng::utf8