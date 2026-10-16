#pragma once

/// \file text.hpp
/// **Texto sobre `StringView`** (`eng::util`): recorte, troceado, parseo de enteros,
/// conversión a decimal (entera, escalada y de escalar fraccionario) y comparación sin
/// distinguir mayúsculas.
///
/// Es la pareja de lectura/escritura de `StringView`/`StaticString` para configuración,
/// HUD y depuración. Todo sin heap y sin coma flotante.
///
/// El paso a decimal evita la **división** y las multiplicaciones de 32 bits: extrae cada
/// dígito con una tabla de potencias de diez y restas, porque `v / 10` en `u32` acabaría
/// llamando a libgcc (`__udivsi3`/`__mulsi3`) en el 68000.
///
/// Escritura:
///   eng::util::StaticString<16> s;
///   eng::util::to_chars(s, eng::u16(1234));    // "1234" (cualquier ancho de entero)
///   eng::util::to_chars_scaled(s, 5960u, 2u);  // "59.60" (el valor ya va ×10^2)
///   eng::util::to_chars(s, q12{6144}, 2u);     // "1.50"  (escalar fraccionario, k≤4)
///
/// Lectura:
///   eng::u16 n = 0u;
///   if (eng::util::parse(eng::util::StringView("123"), n)) { ... }   // rango de `n`
///   q12 v {};
///   if (eng::util::parse(eng::util::StringView("1.5"), v, 2u)) { ... } // al ulp de q12
///
/// Los escalares fraccionarios (`Fixed`, …) se convierten a texto a través de su punto de
/// extensión `eng::math::scalar_decimal<S>` (`scalar_fwd.hpp`): esta cabecera **no conoce
/// ninguna representación concreta** (AGENTS §1.11).
///
/// Uso de tokens:
///   eng::util::StringView rest = {"a,b,c"};
///   while (!rest.empty()) { const auto tok = eng::util::split_next(rest, ','); }

#include <eng/core/math/scalar_fwd.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/static_string.hpp>
#include <eng/core/util/string_view.hpp>
#include <eng/core/util/type_traits.hpp>

namespace eng::util {

/// ¿`c` es espacio en blanco (espacio, tabulador, CR, LF)?
[[nodiscard]] constexpr bool is_space(char c) noexcept {
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/// ¿`c` es dígito decimal?
[[nodiscard]] constexpr bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

/// Recorta espacios por ambos extremos.
[[nodiscard]] constexpr StringView trim(StringView text) noexcept {
	usize begin = 0u;
	usize end = text.size();
	while (begin < end && is_space(text[begin])) {
		++begin;
	}
	while (end > begin && is_space(text[end - 1u])) {
		--end;
	}
	return text.substr(begin, end - begin);
}

/// Siguiente token de `rest` separado por `sep`: lo devuelve y deja en `rest` lo que
/// queda tras el separador. Con `rest` vacío devuelve una vista vacía.
[[nodiscard]] constexpr StringView split_next(StringView& rest, char sep) noexcept {
	if (rest.empty()) {
		return {};
	}
	const usize pos = rest.find(sep);
	if (pos == StringView::npos) {
		const StringView token = rest;
		rest = StringView {rest.data() + rest.size(), 0u};
		return token;
	}
	const StringView token = rest.substr(0u, pos);
	rest = rest.substr(pos + 1u);
	return token;
}

/// Compara dos textos sin distinguir mayúsculas (ASCII A-Z).
[[nodiscard]] constexpr bool equal_ci(StringView a, StringView b) noexcept {
	if (a.size() != b.size()) {
		return false;
	}
	for (usize i = 0; i < a.size(); ++i) {
		char ca = a[i];
		char cb = b[i];
		if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
		if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
		if (ca != cb) {
			return false;
		}
	}
	return true;
}

/// Parsea un entero sin signo decimal. `false` si está vacío, hay un carácter que no
/// es dígito o desborda `u32`.
[[nodiscard]] constexpr bool parse_u32(StringView text, u32& out) noexcept {
	if (text.empty()) {
		return false;
	}
	u32 value = 0u;
	for (usize i = 0u; i < text.size(); ++i) {
		const char c = text[i];
		if (!is_digit(c)) {
			return false;
		}
		const u32 d = static_cast<u32>(c - '0');
		// Guarda de desbordamiento sin división (4294967295 = 429496729·10 + 5).
		if (value > 429496729u || (value == 429496729u && d > 5u)) {
			return false;
		}
		value = value * 10u + d;
	}
	out = value;
	return true;
}

/// Parsea un entero con signo decimal (un `-` inicial opcional).
[[nodiscard]] constexpr bool parse_s32(StringView text, s32& out) noexcept {
	bool negative = false;
	if (!text.empty() && text[0] == '-') {
		negative = true;
		text.remove_prefix(1u);
	}
	u32 magnitude = 0u;
	if (!parse_u32(text, magnitude)) {
		return false;
	}
	if (negative) {
		if (magnitude > 0x80000000u) {
			return false;
		}
		out = (magnitude == 0x80000000u) ? (-0x7fffffff - 1)
						 : -static_cast<s32>(magnitude);
	} else {
		if (magnitude > 0x7fffffffu) {
			return false;
		}
		out = static_cast<s32>(magnitude);
	}
	return true;
}

namespace detail {

/// Potencias de diez que caben en `u32` (el núcleo del decimal: nada de `*`/`/` en runtime).
inline constexpr u32 kPow10[10] = {1u,       10u,      100u,      1000u,     10000u,
				   100000u,  1000000u, 10000000u, 100000000u, 1000000000u};

/// Cota de la parte entera por nº de decimales: máximo `x` con `x·10^k ≤ 2^31-1`
/// (`2147483647/10^k`, hacia abajo). Mantiene en `s32` el valor escalado de `parse` sin
/// hacer la división en runtime.
inline constexpr u32 kScaledWholeMax[5] = {0x7fffffffu, 214748364u, 21474836u,
					   2147483u, 214748u};

/// Máximo de un entero **sin signo** del ancho de `T` (u8 → 255, u32 → 4294967295).
template <class T>
constexpr u32 uint_max() noexcept {
	return static_cast<u32>(static_cast<T>(~static_cast<T>(0)));
}

/// Máximo (positivo) de un entero **con signo** del ancho de `T` (s8 → 127).
template <class T>
constexpr s32 int_max() noexcept {
	constexpr u32 kBits = static_cast<u32>(sizeof(T) * 8u);
	return static_cast<s32>(kBits >= 32u ? 0x7fffffffu : ((1u << (kBits - 1u)) - 1u));
}

/// Escribe `mag` (magnitud) en decimal con el punto tras `frac_digits` dígitos;
/// `negative` antepone el `-`. Pre-chequea la capacidad **incluido el signo**: o escribe
/// el número completo o no toca `out` (nada de medias escrituras).
template <usize N>
constexpr bool write_scaled(StaticString<N>& out, u32 mag, u8 frac_digits,
			    bool negative) noexcept {
	if (frac_digits > 9u) {
		return false;
	}
	int hi = 9;
	while (hi > 0 && mag < kPow10[hi]) {
		--hi;
	}
	const usize digits = static_cast<usize>(hi) + 1u;
	const bool dot = frac_digits != 0u;
	// Longitud final: dígitos + punto. Con `digits > frac_digits` → "int.frac"; si no,
	// "0." + ceros de relleno + dígitos (5 con k=2 → "0.05").
	usize need = digits;
	if (dot) {
		need = (digits > frac_digits ? digits : static_cast<usize>(frac_digits) + 1u) + 1u;
	}
	if (negative) {
		++need;
	}
	if (out.size() + need > out.capacity()) {
		return false;
	}
	if (negative) {
		(void)out.append('-');
	}
	if (dot && digits <= frac_digits) {
		(void)out.append('0');
		(void)out.append('.');
		for (usize i = digits; i < frac_digits; ++i) {
			(void)out.append('0');
		}
	}
	for (int i = hi; i >= 0; --i) {
		if (dot && digits > frac_digits && i == static_cast<int>(frac_digits) - 1) {
			(void)out.append('.');
		}
		u8 digit = 0u;
		while (mag >= kPow10[i]) { // sin división
			mag -= kPow10[i];
			++digit;
		}
		(void)out.append(static_cast<char>('0' + digit));
	}
	return true;
}

} // namespace detail

/// Escribe `value` (valor × 10^frac_digits, 0..9 decimales) en decimal con el punto
/// insertado: `5960, 2 → "59.60"`, `5, 2 → "0.05"`, `123, 0 → "123"`. Sin `*` ni `/`.
template <usize N>
constexpr bool to_chars_scaled(StaticString<N>& out, u32 value, u8 frac_digits) noexcept {
	return detail::write_scaled(out, value, frac_digits, false);
}

/// Igual que el anterior con signo: `-5960, 2 → "-59.60"` (el `-0.00` de un valor negativo
/// truncado a cero se conserva, como `printf`). `false` sin escribir nada si no cabe.
template <usize N>
constexpr bool to_chars_scaled(StaticString<N>& out, s32 value, u8 frac_digits) noexcept {
	const bool negative = value < 0;
	const u32 mag = negative ? (static_cast<u32>(-(value + 1)) + 1u) : static_cast<u32>(value);
	return detail::write_scaled(out, mag, frac_digits, negative);
}

/// Enteros de la API decimal (8/16/32 bits; `bool` se excluye: no es un número de dominio).
template <class T>
concept decimal_integer = is_integral_v<T> && !is_same_v<T, bool>;

/// Igual que `to_chars`, pero `value` ya está escalado por 10^frac_digits (cualquier ancho
/// de entero): lo usa el formateo de contadores en centésimas/milésimas sin `float`.
template <usize N, class T>
	requires decimal_integer<T>
constexpr bool to_chars_scaled(StaticString<N>& out, T value, u8 frac_digits) noexcept {
	if constexpr (is_unsigned_v<T>) {
		return to_chars_scaled(out, static_cast<u32>(value), frac_digits);
	} else {
		return to_chars_scaled(out, static_cast<s32>(value), frac_digits);
	}
}

/// Escribe `value` (cualquier entero de 8/16/32 bits) en decimal, sin punto: `u16(1234)`
/// → "1234", `s16(-7)` → "-7". Atajo de `to_chars_scaled(…, 0)`.
template <usize N, class T>
	requires decimal_integer<T>
constexpr bool to_chars(StaticString<N>& out, T value) noexcept {
	return to_chars_scaled(out, value, 0u);
}

/// Escribe `value` sin signo en decimal (núcleo de 32 bits; equivale a `to_chars`).
template <usize N>
constexpr bool to_chars_u32(StaticString<N>& out, u32 value) noexcept {
	return to_chars_scaled(out, value, 0u);
}

/// Escribe `value` con signo en decimal (núcleo de 32 bits; equivale a `to_chars`).
template <usize N>
constexpr bool to_chars_s32(StaticString<N>& out, s32 value) noexcept {
	return to_chars_scaled(out, value, 0u);
}

/// ¿`S` declara su conversión decimal (`eng::math::scalar_decimal<S>`, `scalar_fwd.hpp`)?
/// Los escalares fraccionarios que quieran entrar/salir por texto la especializan y este
/// concepto los admite en `to_chars`/`parse` sin nombrar el tipo.
template <class S>
concept decimal_scalar = requires(S x, s32 scaled_q, u8 k, S& target) {
	{ eng::math::scalar_decimal<S>::scaled(x, k) } -> same_as<s32>;
	{ eng::math::scalar_decimal<S>::from_scaled(scaled_q, k, target) } -> same_as<bool>;
};

/// Escribe un escalar **fraccionario** (`Fixed`, …) en decimal con `frac_digits` decimales
/// (0..`eng::math::kDecimalDigitsMax`, contrato de `scalar_decimal`): `q12{6144}, 2` →
/// "1.50". El redondeo lo fija el escalar; `false` sin escribir si no cabe.
template <usize N, class S>
	requires decimal_scalar<S>
constexpr bool to_chars(StaticString<N>& out, S value, u8 frac_digits) noexcept {
	if (frac_digits > eng::math::kDecimalDigitsMax) {
		return false;
	}
	return to_chars_scaled(out, eng::math::scalar_decimal<S>::scaled(value, frac_digits),
			       frac_digits);
}

/// Parsea `text` como entero del tipo `T` (8/16/32 bits) comprobando el rango de `T`:
/// `false` si hay un carácter que no es dígito, está vacío o el valor no cabe (no satura:
/// avisa con `false`, `out` sin cambios).
template <class T>
	requires decimal_integer<T>
constexpr bool parse(StringView text, T& out) noexcept {
	if constexpr (is_unsigned_v<T>) {
		u32 value = 0u;
		if (!parse_u32(text, value) || value > detail::uint_max<T>()) {
			return false;
		}
		out = static_cast<T>(value);
	} else {
		s32 value = 0;
		if (!parse_s32(text, value)) {
			return false;
		}
		if (value > detail::int_max<T>() || value < -detail::int_max<T>() - 1) {
			return false;
		}
		out = static_cast<T>(value);
	}
	return true;
}

/// Parsea un escalar **fraccionario** (`"[-]int[.frac]"`, hasta `frac_digits` decimales) y
/// lo cuantiza con `scalar_decimal<S>::from_scaled` (al ulp más cercano del escalar).
/// `false` si el texto no es válido (parte entera obligatoria: "0.5", no ".5"; nada de
/// decimales de más), o el valor no cabe en `S` (`out` sin cambios).
template <class S>
	requires decimal_scalar<S>
constexpr bool parse(StringView text, S& out, u8 frac_digits) noexcept {
	if (frac_digits > eng::math::kDecimalDigitsMax) {
		return false;
	}
	bool negative = false;
	if (!text.empty() && text[0] == '-') {
		negative = true;
		text.remove_prefix(1u);
	}
	if (text.empty()) {
		return false;
	}
	const usize dot = text.find('.');
	u32 whole = 0u;
	u32 frac = 0u;
	if (dot == StringView::npos) {
		if (!parse_u32(text, whole)) {
			return false;
		}
	} else {
		if (dot == 0u) {
			return false; // parte entera obligatoria ("0.5", no ".5")
		}
		if (!parse_u32(text.substr(0u, dot), whole)) {
			return false;
		}
		const StringView frac_text = text.substr(dot + 1u);
		if (frac_text.empty() || frac_text.size() > frac_digits) {
			return false;
		}
		if (!parse_u32(frac_text, frac)) {
			return false;
		}
		// Escala la fracción a `frac_digits` decimales: "1.5" con k=4 → 5000 (5·10^3).
		for (usize i = frac_text.size(); i < frac_digits; ++i) {
			frac = static_cast<u32>(frac * 10u);
		}
	}
	// `q = signo·(whole·10^k + frac)`; las cotas garantizan que cabe en `s32` sin `*`/`/`.
	if (whole > detail::kScaledWholeMax[frac_digits]) {
		return false;
	}
	u32 magnitude = whole * detail::kPow10[frac_digits];
	if (magnitude > 0x7fffffffu - frac) {
		return false;
	}
	magnitude += frac;
	const s32 scaled_q = negative ? -static_cast<s32>(magnitude)
				      : static_cast<s32>(magnitude);
	return eng::math::scalar_decimal<S>::from_scaled(scaled_q, frac_digits, out);
}

/// Añade `parts` separadas por `sep`. `false` (sin cambio parcial) si no cabe.
template <usize N>
constexpr bool join(StaticString<N>& out, Span<const StringView> parts, char sep) noexcept {
	for (usize i = 0; i < parts.size(); ++i) {
		if (i != 0u && !out.append(sep)) {
			return false;
		}
		if (!out.append(parts[i])) {
			return false;
		}
	}
	return true;
}

} // namespace eng::util
