#pragma once

/// \file text.hpp
/// **Texto sobre `StringView`** (`eng::util`): recorte, troceado, parseo de enteros,
/// conversión a decimal y comparación sin distinguir mayúsculas.
///
/// Es la pareja de lectura/escritura de `StringView`/`StaticString` para configuración,
/// HUD y depuración. Todo sin heap y sin `float`.
///
/// El paso a decimal evita la **división**: extrae cada dígito con una tabla de
/// potencias de diez y restas, porque `v / 10` en `u32` acabaría llamando a libgcc
/// (`__udivsi3`/`__mulsi3`) en el 68000.
///
/// Uso:
///   eng::util::StringView rest = {"a,b,c"};
///   while (!rest.empty()) { const auto tok = eng::util::split_next(rest, ','); }
///   eng::u32 n = 0;
///   if (eng::util::parse_u32(eng::util::StringView("123"), n)) { ... }
///   eng::util::StaticString<16> s;
///   eng::util::to_chars_u32(s, n);

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/static_string.hpp>
#include <eng/core/util/string_view.hpp>

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

/// Añade `value` en decimal a `out`. `false` (sin cambio) si no cabe.
template <usize N>
constexpr bool to_chars_u32(StaticString<N>& out, u32 value) noexcept {
	constexpr u32 kPow10[10] = {1u, 10u, 100u, 1000u, 10000u, 100000u,
				    1000000u, 10000000u, 100000000u, 1000000000u};
	int hi = 9;
	while (hi > 0 && value < kPow10[hi]) {
		--hi;
	}
	// ¿Cabe? (dígitos = hi+1).
	if (out.size() + static_cast<usize>(hi + 1) > out.capacity()) {
		return false;
	}
	for (int i = hi; i >= 0; --i) {
		u8 digit = 0u;
		while (value >= kPow10[i]) { // sin división
			value -= kPow10[i];
			++digit;
		}
		out.append(static_cast<char>('0' + digit));
	}
	return true;
}

/// Añade `value` con signo en decimal a `out`.
template <usize N>
constexpr bool to_chars_s32(StaticString<N>& out, s32 value) noexcept {
	if (value < 0) {
		if (out.size() + 1u > out.capacity()) {
			return false;
		}
		out.append('-');
		const u32 magnitude = (value == -0x7fffffff - 1)
					      ? 0x80000000u
					      : static_cast<u32>(-value);
		return to_chars_u32(out, magnitude);
	}
	return to_chars_u32(out, static_cast<u32>(value));
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
