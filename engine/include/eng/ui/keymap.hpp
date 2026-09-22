#pragma once

/// \file keymap.hpp
/// **Traducción rawkey Amiga → tecla lógica de UI** (`eng::ui`). `ui_bridge` entrega el rawkey
/// crudo (`os::Msg payload.key.code`); este mapa lo lleva al contrato de `keys.hpp`
/// (ASCII/Latin-1 + edición/navegación), aplicando Shift (mayúsculas y símbolos). Ver
/// `docs/engine/architecture/MINI_OS_INPUT.md`.

#include <eng/core/types.hpp>
#include <eng/ui/keys.hpp>

namespace eng::ui {

namespace detail {

/// ASCII sin Shift (indice = rawkey Amiga, distribución US).
inline constexpr char kRawAscii[128] = {
	'`', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\\', 0, 0,
	'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0, 0, 0, 0,
	'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', 0, 0, 0, 0, 0,
	'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, 0, 0, 0, 0, 0,
	' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

/// ASCII con Shift.
inline constexpr char kRawAsciiShift[128] = {
	'~', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '|', 0, 0,
	'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 0, 0, 0, 0,
	'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', 0, 0, 0, 0, 0,
	'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, 0, 0, 0, 0, 0,
	' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

} // namespace detail

/// Traduce un rawkey Amiga (`0..0x7f`) a la tecla lógica de `keys.hpp`; `0` si no mapea.
[[nodiscard]] constexpr eng::u16 rawkey_to_key(eng::u8 raw, bool shift) noexcept {
	switch (raw) {
	case 0x41u:
		return kKeyBackspace;
	case 0x42u:
		return kKeyTab;
	case 0x44u:
		return kKeyReturn;
	case 0x45u:
		return kKeyEsc;
	case 0x46u:
		return kKeyDelete;
	case 0x4fu:
		return kKeyLeft;
	case 0x4eu:
		return kKeyRight;
	case 0x4du:
		return kKeyUp;
	case 0x4cu:
		return kKeyDown;
	default:
		break;
	}
	if (raw >= 0x80u) {
		return 0u;
	}
	return static_cast<eng::u16>(
		static_cast<eng::u8>(shift ? detail::kRawAsciiShift[raw] : detail::kRawAscii[raw]));
}

} // namespace eng::ui
