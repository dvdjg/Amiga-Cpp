#pragma once

/// \file keys.hpp
/// **Teclas lógicas de UI** (`eng::ui`): el contrato que consume `UiEvent::key`. Los caracteres
/// imprimibles usan su **código ASCII/Latin-1**; las teclas de edición/navegación usan códigos
/// **fuera** de ese rango (para no confundirse con texto). Traducir del **rawkey Amiga** (lo que
/// entrega `ui_bridge`/`KeyProducer`) a este contrato es responsabilidad de la capa de entrada
/// (keymap); hoy esa traducción está pendiente.

#include <eng/core/types.hpp>

namespace eng::ui {

inline constexpr eng::u16 kKeyBackspace = 0x0008u; ///< ASCII BS
inline constexpr eng::u16 kKeyTab = 0x0009u;       ///< ASCII HT (cambia el foco)
inline constexpr eng::u16 kKeyReturn = 0x000du;    ///< ASCII CR
inline constexpr eng::u16 kKeyEsc = 0x001bu;       ///< ASCII ESC
inline constexpr eng::u16 kKeyDelete = 0x007fu;    ///< ASCII DEL
inline constexpr eng::u16 kKeyLeft = 0x0100u;
inline constexpr eng::u16 kKeyRight = 0x0101u;
inline constexpr eng::u16 kKeyUp = 0x0102u;
inline constexpr eng::u16 kKeyDown = 0x0103u;
inline constexpr eng::u16 kKeyHome = 0x0104u;
inline constexpr eng::u16 kKeyEnd = 0x0105u;

/// ¿`key` es un carácter imprimible (ASCII, Latin-1 o cirílico U+04xx)?
[[nodiscard]] constexpr bool is_printable_key(eng::u16 key) noexcept {
	return (key >= 0x20u && key <= 0x7eu) || (key >= 0xa0u && key <= 0xffu) ||
	       (key >= 0x400u && key <= 0x4ffu);
}

} // namespace eng::ui
