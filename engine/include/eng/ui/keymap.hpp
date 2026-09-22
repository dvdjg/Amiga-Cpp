#pragma once

/// \file keymap.hpp
/// **Traducción rawkey Amiga → carácter de UI** (`eng::ui`) por **distribución nacional**.
///
/// En Amiga el **rawkey es la posición física** de la tecla (matriz): el mismo código en todas
/// las máquinas. El **OS** (`keymap.library` + el keymap del país, `DEVS:Keymaps/*`) es quien lo
/// traduce a carácter, con variantes nacionales y **teclas muertas** (p. ej. Alt+H = agudo que
/// compone con la siguiente letra). Ver `amiga-bootcamp/11_libraries/keymap.md` y la wiki de
/// AmigaOS (`Keymap_Library`).
///
/// Aquí se ofrece un mapa **best-effort** de las distribuciones más habituales (US, ES, FR, IT,
/// DE, RU) para las teclas **0x00–0x3F** (área principal); las especiales (0x40–0x7F) son comunes.
/// **Pendiente**: componer teclas muertas y validar las tablas contra los keymaps reales del ROM
/// (`DEVS:Keymaps`). Los caracteres de RU son **cirílicos (U+04xx)**, fuera de la cobertura de las
/// fuentes del engine (`Font8`/`Font5x7`, ASCII+LATIN-1): el input los produce, el dibujo no.

#include <eng/core/types.hpp>
#include <eng/ui/keys.hpp>

namespace eng::ui {

/// Distribución de teclado.
enum class KeyboardLayout : eng::u8 {
	Us, ///< US English (QWERTY) — por defecto
	Es, ///< Español
	Fr, ///< Français (AZERTY)
	It, ///< Italiano
	De, ///< Deutsch (QWERTZ)
	Ru, ///< Русский (cirílico)
};

namespace detail {

/// Base US: 64 posiciones (rawkey 0x00–0x3F), sin Shift.
inline constexpr eng::u16 kUsLower[64] = {
	'`', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\\', 0, 0,
	'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0, 0, 0, 0,
	'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', 0, 0, 0, 0, 0,
	0, 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, 0, 0, 0, 0,
};
/// Base US: con Shift.
inline constexpr eng::u16 kUsUpper[64] = {
	'~', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '|', 0, 0,
	'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 0, 0, 0, 0,
	'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', 0, 0, 0, 0, 0,
	0, 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, 0, 0, 0, 0,
};

// Overrides nacionales: 0 = hereda US.
// Español: ñ en ';', ¡/¿ en '=', ç en '\', agudo/diéresis en '\'', º/ª y <>/ en las reservadas.
inline constexpr eng::u16 kEsLower[64] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '\'', 0xa1, 0xe7, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '`', '+', 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0xf1, 0xb4, 0xba, 0, 0, 0, 0,
	0x3c, 0, 0, 0, 0, 0, 0, 0, ',', '.', '-', 0, 0, 0, 0, 0,
};
inline constexpr eng::u16 kEsUpper[64] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '?', 0xbf, 0xc7, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '^', '*', 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0xd1, 0xa8, 0xaa, 0, 0, 0, 0,
	0x3e, 0, 0, 0, 0, 0, 0, 0, ';', ':', '_', 0, 0, 0, 0, 0,
};
// Alemán (QWERTZ): Y/Z intercambiadas; ü/ö/ä; ß; ´/`.
inline constexpr eng::u16 kDeLower[64] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xdf, 0xb4, '#', 0, 0,
	0, 0, 0, 0, 0, 'z', 0, 0, 0, 0, 0xfc, '+', 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0xf6, 0xe4, 0, 0, 0, 0, 0, 0,
	0, 'y', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
inline constexpr eng::u16 kDeUpper[64] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x60, '\'', 0, 0,
	0, 0, 0, 0, 0, 'Z', 0, 0, 0, 0, 0xdc, '*', 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0xd6, 0xc4, 0, 0, 0, 0, 0, 0,
	0, 'Y', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
// Francés (AZERTY): A/Q y Z/W; dígitos con símbolos; acentos; m en ';'.
inline constexpr eng::u16 kFrLower[64] = {
	0, '&', 0xe9, '"', '\'', '(', '-', 0xe8, '_', 0xe7, 0xe0, ')', '=', '*', 0, 0,
	'a', 'z', 0, 0, 0, 0, 0, 0, 0, 0, '^', '$', 0, 0, 0, 0,
	'q', 0, 0, 0, 0, 0, ',', ';', 'm', 0xf9, 0, 0, 0, 0, 0, 0,
	0, 'w', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
inline constexpr eng::u16 kFrUpper[64] = {
	0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 0xb0, '+', 0xb5, 0, 0,
	'A', 'Z', 0, 0, 0, 0, 0, 0, 0, 0, 0xa8, 0xa3, 0, 0, 0, 0,
	'Q', 0, 0, 0, 0, 0, '?', '.', 'M', '%', 0, 0, 0, 0, 0, 0,
	0, 'W', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
// Italiano: è/é, ò/ç, à/°, ì/^, ù/§.
inline constexpr eng::u16 kItLower[64] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xec, 0xf9, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xe8, '+', 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0xf2, 0xe0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
inline constexpr eng::u16 kItUpper[64] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '^', 0xa7, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xe9, '*', 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0xe7, 0xb0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
// Ruso (ЙЦУКЕН, cirílico). Fuera de la cobertura de las fuentes del engine.
inline constexpr eng::u16 kRuLower[64] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '\\', 0, 0,
	0x439, 0x446, 0x443, 0x43a, 0x435, 0x43d, 0x433, 0x448, 0x449, 0x437, 0x445, 0x44a, 0, 0, 0, 0,
	0x444, 0x44b, 0x432, 0x430, 0x43f, 0x440, 0x43e, 0x43b, 0x434, 0x436, 0x44d, 0, 0, 0, 0, 0,
	0, 0x44f, 0x447, 0x441, 0x43c, 0x438, 0x442, 0x44c, 0x431, 0x44e, '.', 0, 0, 0, 0, 0,
};
inline constexpr eng::u16 kRuUpper[64] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '/', 0, 0,
	0x419, 0x426, 0x423, 0x41a, 0x415, 0x41d, 0x413, 0x428, 0x429, 0x417, 0x425, 0x42a, 0, 0, 0, 0,
	0x424, 0x42b, 0x412, 0x410, 0x41f, 0x420, 0x41e, 0x41b, 0x414, 0x416, 0x42d, 0, 0, 0, 0, 0,
	0, 0x42f, 0x427, 0x421, 0x41c, 0x418, 0x422, 0x42c, 0x411, 0x42e, ',', 0, 0, 0, 0, 0,
};

/// Tabla de 64 posiciones (rawkey 0x00–0x3F) de `layout` para el estado de Shift pedido.
[[nodiscard]] constexpr const eng::u16* low_table(KeyboardLayout layout,
						  bool shift) noexcept {
	switch (layout) {
	case KeyboardLayout::Es:
		return shift ? kEsUpper : kEsLower;
	case KeyboardLayout::Fr:
		return shift ? kFrUpper : kFrLower;
	case KeyboardLayout::It:
		return shift ? kItUpper : kItLower;
	case KeyboardLayout::De:
		return shift ? kDeUpper : kDeLower;
	case KeyboardLayout::Ru:
		return shift ? kRuUpper : kRuLower;
	case KeyboardLayout::Us:
	default:
		return shift ? kUsUpper : kUsLower;
	}
}

} // namespace detail

/// Traduce un rawkey Amiga (`0..0x7f`) a la tecla lógica de `keys.hpp` según `layout`; `0` si no
/// mapea. Las teclas especiales (0x40–0x7F) son comunes a todas las distribuciones.
[[nodiscard]] constexpr eng::u16 rawkey_to_key(eng::u8 raw, bool shift,
					      KeyboardLayout layout = KeyboardLayout::Us) noexcept {
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
	if (raw >= 0x40u) {
		return 0u;
	}
	const eng::u16* const base = detail::low_table(layout, shift);
	const eng::u16 ov = base[raw];
	if (ov != 0u) {
		return ov;
	}
	return shift ? detail::kUsUpper[raw] : detail::kUsLower[raw];
}

} // namespace eng::ui
