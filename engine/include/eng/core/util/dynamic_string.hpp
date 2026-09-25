#pragma once

/// \file dynamic_string.hpp
/// `eng::util::DynamicString<A>`: cadena de texto **que crece** sobre un
/// `eng::util::Vector<char, A>`, con `StringView` para leerla. Es la pareja
/// dinamica de `StaticString<N>`: se usa en **herramientas host** que construyen
/// salida larga (generadores de codigo, ensambladores, volcados de diagnostico)
/// sin STL y sin `std::string`.
///
/// El formateo de enteros evita la division (tabla de potencias y restas) para
/// que, si algun dia se compila en 68000, no arrastre `__udivsi3`; el hex se
/// resuelve con desplazamientos. No lanza: `append*` devuelve `false` si el
/// asignador no puede crecer.
///
/// Uso:
///   eng::util::DynamicString<eng::util::HeapAlloc> out;
///   out.append("LDA #$");
///   out.append_hex(0x2Au, 2u);      // -> "LDA #$2a"

#include <eng/core/types/types.hpp>
#include <eng/core/util/allocator.hpp>
#include <eng/core/util/heap_alloc.hpp>
#include <eng/core/util/string_view.hpp>
#include <eng/core/util/vector.hpp>

namespace eng::util {

template <class A = HeapAlloc>
class DynamicString {
public:
	constexpr DynamicString() noexcept = default;
	explicit constexpr DynamicString(A alloc) noexcept : m_buf(alloc) {}

	DynamicString(const DynamicString&) = delete;
	DynamicString& operator=(const DynamicString&) = delete;
	constexpr DynamicString(DynamicString&&) noexcept = default;
	constexpr DynamicString& operator=(DynamicString&&) noexcept = default;

	[[nodiscard]] usize size() const noexcept { return m_buf.size(); }
	[[nodiscard]] bool empty() const noexcept { return m_buf.empty(); }
	[[nodiscard]] char* data() noexcept { return m_buf.data(); }
	[[nodiscard]] const char* data() const noexcept { return m_buf.data(); }
	[[nodiscard]] StringView view() const noexcept {
		return StringView(m_buf.data(), m_buf.size());
	}

	constexpr void clear() noexcept { m_buf.clear(); }
	[[nodiscard]] bool reserve(usize n) noexcept { return m_buf.reserve(n); }

	/// Anade un caracter. `false` si el asignador no puede crecer.
	bool append(char c) noexcept { return m_buf.push_back(c); }

	/// Anade un texto (se reserva de golpe para no crecer caracter a caracter).
	bool append(StringView text) noexcept {
		if (text.empty()) {
			return true;
		}
		if (!m_buf.reserve(m_buf.size() + text.size())) {
			return false;
		}
		for (usize i = 0; i < text.size(); ++i) {
			if (!m_buf.push_back(text[i])) {
				return false;
			}
		}
		return true;
	}
	bool append(const char* cstr) noexcept { return append(StringView(cstr)); }

	/// Repite `c` `count` veces.
	bool append_repeat(char c, usize count) noexcept {
		if (!m_buf.reserve(m_buf.size() + count)) {
			return false;
		}
		for (usize i = 0; i < count; ++i) {
			if (!m_buf.push_back(c)) {
				return false;
			}
		}
		return true;
	}

	/// Anade `value` en decimal (sin division), con relleno a la izquierda
	/// opcional hasta `min_width`.
	bool append_u32(u32 value, usize min_width = 0u, char pad = ' ') noexcept {
		constexpr u32 kPow10[10] = {1u,       10u,      100u,      1000u,     10000u,
					    100000u,  1000000u, 10000000u, 100000000u, 1000000000u};
		int hi = 9;
		while (hi > 0 && value < kPow10[hi]) {
			--hi;
		}
		const usize digits = static_cast<usize>(hi + 1);
		for (usize i = digits; i < min_width; ++i) {
			if (!append(pad)) {
				return false;
			}
		}
		for (int i = hi; i >= 0; --i) {
			u8 d = 0u;
			while (value >= kPow10[i]) {
				value -= kPow10[i];
				++d;
			}
			if (!append(static_cast<char>('0' + d))) {
				return false;
			}
		}
		return true;
	}

	/// Anade `value` con signo en decimal.
	bool append_s32(s32 value) noexcept {
		if (value < 0) {
			if (!append('-')) {
				return false;
			}
			const u32 magnitude = (value == -0x7fffffff - 1)
						      ? 0x80000000u
						      : static_cast<u32>(-value);
			return append_u32(magnitude);
		}
		return append_u32(static_cast<u32>(value));
	}

	/// Anade `value` en hexadecimal (minusculas por defecto), con un minimo de
	/// `digits` digitos rellenando con ceros a la izquierda.
	bool append_hex(u32 value, usize digits = 0u, bool upper = false) noexcept {
		static constexpr char kLower[] = "0123456789abcdef";
		static constexpr char kUpper[] = "0123456789ABCDEF";
		const char* tab = upper ? kUpper : kLower;
		int hi = 7;
		while (hi > 0 && ((value >> (hi * 4)) & 0xFu) == 0u) {
			--hi;
		}
		const usize sig = static_cast<usize>(hi + 1);
		for (usize i = sig; i < digits; ++i) {
			if (!append('0')) {
				return false;
			}
		}
		for (int i = hi; i >= 0; --i) {
			if (!append(tab[(value >> (i * 4)) & 0xFu])) {
				return false;
			}
		}
		return true;
	}

private:
	Vector<char, A> m_buf {};
};

} // namespace eng::util
