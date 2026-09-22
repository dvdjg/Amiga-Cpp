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
/// Las **teclas comunes** (0x40–0x5F: Space, Backspace, Tab, Return, Esc, Delete y cursores) están
/// **validadas contra la AHRM 3.ª** (tabla «RAW Keycodes 40-5F», ver HOST-302). Las **teclas
/// muertas** (Alt + letra → acento pendiente que compone con la siguiente letra) se resuelven con
/// `compose`/`rawkey_to_char`. **Pendiente**: la asignación de carácter de cada posición nacional y
/// los Alt+tecla exactos son *best-effort* (las posiciones base son el QWERTY US posicional de la
/// AHRM) hasta volcarlos de `DEVS:Keymaps` del ROM. Los caracteres de RU son **cirílicos (U+04xx)**,
/// cubiertos por `Font8`.

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

/// Acento muerto (prefijo que compone con la siguiente letra). Ver `compose`.
enum class DeadKey : eng::u8 {
	None = 0,
	Acute,      ///< ´  (Alt+H)
	Grave,      ///< `  (Alt+G)
	Circumflex, ///< ^  (Alt+J)
	Tilde,      ///< ~  (Alt+K)
	Diaeresis,  ///< ¨  (Alt+L)
	Cedilla,    ///< ¸  (Alt+C)
	Ring,       ///< °  (Alt+O)
};

/// Acento muerto asociado a un rawkey con Alt (o `None`). La asignación es **best-effort**:
/// el ejemplo documentado es Alt+H = agudo (`amiga-bootcamp/11_libraries/keymap.md`); el resto
/// sigue el uso mnemotécnico y queda pendiente de validar contra el keymap del ROM.
[[nodiscard]] constexpr DeadKey dead_key_of(eng::u8 raw) noexcept {
	switch (raw) {
	case 0x25u: return DeadKey::Acute;      // H
	case 0x24u: return DeadKey::Grave;      // G
	case 0x26u: return DeadKey::Circumflex; // J
	case 0x27u: return DeadKey::Tilde;      // K
	case 0x28u: return DeadKey::Diaeresis;  // L
	case 0x33u: return DeadKey::Cedilla;    // C
	case 0x18u: return DeadKey::Ring;       // O
	default: return DeadKey::None;
	}
}

/// Compone un acento muerto con una letra base; devuelve el code point **Latin-1 precompuesto**
/// (p. ej. agudo + `e` → `é`) o `0` si esa combinación no existe.
[[nodiscard]] constexpr eng::u16 compose(DeadKey mark, eng::u16 base) noexcept {
	switch (mark) {
	case DeadKey::Acute:
		switch (base) {
		case 'a': return 0xe1u; case 'e': return 0xe9u; case 'i': return 0xedu;
		case 'o': return 0xf3u; case 'u': return 0xfau; case 'y': return 0xfdu;
		case 'A': return 0xc1u; case 'E': return 0xc9u; case 'I': return 0xcdu;
		case 'O': return 0xd3u; case 'U': return 0xdau; case 'Y': return 0xddu;
		default: return 0u;
		}
	case DeadKey::Grave:
		switch (base) {
		case 'a': return 0xe0u; case 'e': return 0xe8u; case 'i': return 0xecu;
		case 'o': return 0xf2u; case 'u': return 0xf9u;
		case 'A': return 0xc0u; case 'E': return 0xc8u; case 'I': return 0xccu;
		case 'O': return 0xd2u; case 'U': return 0xd9u;
		default: return 0u;
		}
	case DeadKey::Circumflex:
		switch (base) {
		case 'a': return 0xe2u; case 'e': return 0xeau; case 'i': return 0xeeu;
		case 'o': return 0xf4u; case 'u': return 0xfbu;
		case 'A': return 0xc2u; case 'E': return 0xcau; case 'I': return 0xceu;
		case 'O': return 0xd4u; case 'U': return 0xdbu;
		default: return 0u;
		}
	case DeadKey::Tilde:
		switch (base) {
		case 'a': return 0xe3u; case 'n': return 0xf1u; case 'o': return 0xf5u;
		case 'A': return 0xc3u; case 'N': return 0xd1u; case 'O': return 0xd5u;
		default: return 0u;
		}
	case DeadKey::Diaeresis:
		switch (base) {
		case 'a': return 0xe4u; case 'e': return 0xebu; case 'i': return 0xefu;
		case 'o': return 0xf6u; case 'u': return 0xfcu; case 'y': return 0xffu;
		case 'A': return 0xc4u; case 'E': return 0xcbu; case 'I': return 0xcfu;
		case 'O': return 0xd6u; case 'U': return 0xdcu;
		default: return 0u;
		}
	case DeadKey::Cedilla:
		switch (base) {
		case 'c': return 0xe7u; case 'C': return 0xc7u;
		default: return 0u;
		}
	case DeadKey::Ring:
		switch (base) {
		case 'a': return 0xe5u; case 'A': return 0xc5u;
		default: return 0u;
		}
	case DeadKey::None:
	default:
		return 0u;
	}
}

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

/// Carácter imprimible de un rawkey del área principal (0x00–0x3F) según `layout` y `shift`; `0`
/// si esa posición no produce carácter (p. ej. las reservadas). Hereda de US si el layout no la
/// redefine.
[[nodiscard]] constexpr eng::u16 printable_char(eng::u8 raw, bool shift,
					       KeyboardLayout layout = KeyboardLayout::Us) noexcept {
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

/// Traduce un rawkey Amiga (`0..0x7f`) a la tecla lógica de `keys.hpp` según `layout`; `0` si no
/// mapea. Las teclas especiales (0x40–0x7F) son comunes a todas las distribuciones.
[[nodiscard]] constexpr eng::u16 rawkey_to_key(eng::u8 raw, bool shift,
					      KeyboardLayout layout = KeyboardLayout::Us) noexcept {
	// Teclas comunes 0x40-0x5F (AHRM 3.ª, tabla «RAW Keycodes 40-5F»).
	switch (raw) {
	case 0x40u:
		return ' '; // Space
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
	case 0x4cu:
		return kKeyUp; // AHRM: 0x4C = cursor up
	case 0x4du:
		return kKeyDown; // AHRM: 0x4D = cursor down
	default:
		break;
	}
	return printable_char(raw, shift, layout);
}

/// Estado de composición de teclas muertas: a lo sumo un acento pendiente.
struct DeadKeyState {
	DeadKey pending = DeadKey::None;
};

/// Resultado de `rawkey_to_char`.
struct KeyChar {
	eng::u16 ch = 0u;       ///< code point producido (0 = ninguno todavía)
	bool consumed = false;  ///< `true` si fue una tecla muerta (no produce carácter aún)
};

/// Traduce un rawkey a **carácter**, gestionando teclas muertas:
///  - si `alt` y la tecla es un acento muerto (`dead_key_of`), lo deja pendiente en `st` y
///    devuelve `{0, consumed=true}` (no produce carácter);
///  - si había un acento pendiente, lo compone con la letra (`compose`); si no combina, la
///    muerta se descarta y se devuelve la letra tal cual;
///  - si no, devuelve el carácter imprimible normal (`printable_char`).
[[nodiscard]] constexpr KeyChar rawkey_to_char(DeadKeyState& st, eng::u8 raw, bool shift, bool alt,
					       KeyboardLayout layout = KeyboardLayout::Us) noexcept {
	if (alt) {
		const DeadKey d = dead_key_of(raw);
		if (d != DeadKey::None) {
			st.pending = d;
			return {0u, true};
		}
	}
	const eng::u16 ch = printable_char(raw, shift, layout);
	if (st.pending != DeadKey::None) {
		const DeadKey d = st.pending;
		st.pending = DeadKey::None;
		if (ch != 0u) {
			const eng::u16 composed = compose(d, ch);
			if (composed != 0u) {
				return {composed, false};
			}
		}
	}
	return {ch, false};
}

} // namespace eng::ui
