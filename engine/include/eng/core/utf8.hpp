#pragma once

/// \file utf8.hpp
/// Decodificador UTF-8 mínimo y decodificación EN COMPILE-TIME (C++20/23).
///
/// Las cadenas de editores (VS Code) vienen en UTF-8 sin BOM. El engine cubre
/// ASCII + LATIN-1 (U+0000..U+00FF): con eso basta para mostrar acentos,
/// diéresis y ñ/Ñ en cualquier configuración.
///
/// Dos usos:
///   - Runtime: `eng::utf8::decode(p)` sobre un `const u8*&` (1-2 bytes por code
///     point, barato en 68000).
///   - Compile-time: `eng::utf8::fixed_string<"Você">` es un NTTP estructural
///     (P0732) que captura el literal como argumento de plantilla; `Surface` lo
///     decodifica en compile-time y solo pinta píxeles en runtime (cero decode).

#include <eng/core/types.hpp>

namespace eng::utf8 {

/// Siguiente code point de `*p`, avanzando `*p` los bytes consumidos (1 o 2).
///
/// ASCII 1 byte (0x00..0x7F); latino 2 bytes (0xC2..0xDF + 10xxxxxx →
/// U+0080..U+07FF; de aquí interesamos U+0080..U+00FF). Devuelve 0 al final de
/// cadena o ante un byte inválido (en ese caso NO avanza el puntero).
constexpr u32 decode(const u8*& p) {
    const u8 b0 = *p;
    if (b0 < 0x80u) {
        ++p;
        return b0; // ASCII: 1 byte
    }
    if (b0 >= 0xC2u && b0 <= 0xDFu) {
        const u8 b1 = *(p + 1u);
        if ((b1 & 0xC0u) == 0x80u) {
            p += 2u;
            return static_cast<u32>((b0 & 0x1Fu) << 6u) |
                   static_cast<u32>(b1 & 0x3Fu);
        }
        return 0; // continuación inválida
    }
    return 0; // 3/4 bytes o continuación suelta: fuera de LATIN-1
}

/// NTTP estructural que captura un literal de cadena (C++20 P0732).
///
/// Uso:
///   template <eng::utf8::fixed_string S> void f();
///   f<"Você">();   // el literal va como argumento de plantilla
///
/// El objeto `S` (valor) es constante en compile-time; `size()` = bytes del
/// literal (sin el NUL), `operator[]` accede a cada byte.
template <usize N>
struct fixed_string {
    char data[N] {}; // incluye el NUL final; data[N-1]=='\0'

    constexpr fixed_string() = default;
    constexpr fixed_string(const char (&s)[N]) {
        for (usize i = 0; i < N; ++i) {
            data[i] = s[i];
        }
    }
    constexpr bool operator==(const fixed_string&) const = default;

    constexpr usize size() const { return N - 1u; } // bytes reales (sin '\0')
    constexpr char operator[](usize i) const { return data[i]; }
};

// Deducción implícita (P0732): fixed_string<"..."> deduce N del literal.
template <usize N>
fixed_string(const char (&)[N]) -> fixed_string<N>;

/// Resultado de decodificar un `fixed_string` (se obtiene con `decode_fixed`).
template <fixed_string Str>
struct decoded {
    static constexpr usize kMax = Str.size();
    u32 cp[kMax] {};
    usize count = 0;
};

/// Decodifica `Str` en compile-time y devuelve el array de code points.
/// Uso: `constexpr auto d = eng::utf8::decode_fixed<Str>();`
template <fixed_string Str>
constexpr decoded<Str> decode_fixed() {
    decoded<Str> d {};
    // Recorremos por índice (sin reinterpret_cast, no permitido en constexpr).
    usize pos = 0;
    while (pos < decoded<Str>::kMax) {
        const u8 b0 = static_cast<u8>(Str[pos]);
        if (b0 == 0u) {
            break; // fin de cadena
        }
        if (b0 < 0x80u) {
            d.cp[d.count++] = b0;
            ++pos;
            continue;
        }
        if (b0 >= 0xC2u && b0 <= 0xDFu && (pos + 1u) < decoded<Str>::kMax) {
            const u8 b1 = static_cast<u8>(Str[pos + 1u]);
            if ((b1 & 0xC0u) == 0x80u) {
                d.cp[d.count++] =
                    static_cast<u32>((b0 & 0x1Fu) << 6u) | static_cast<u32>(b1 & 0x3Fu);
                pos += 2u;
                continue;
            }
        }
        ++pos; // byte inválido o 3/4 bytes: saltamos y seguimos (o paramos)
    }
    return d;
}

/// Helper para `Surface::draw_text_literal<"...">`: acceso estático a los code
/// points de un literal, decodificados en compile-time.
template <fixed_string Str>
struct lit {
    static constexpr decoded<Str> kData = decode_fixed<Str>(); // eval. compile-time
    static constexpr const u32* cp = kData.cp;
    static constexpr usize count = kData.count;
};

} // namespace eng::utf8