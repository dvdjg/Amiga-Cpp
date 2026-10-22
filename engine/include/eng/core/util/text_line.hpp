#pragma once

/// \file text_line.hpp
/// `eng::util::TextLine<N>`: **builder de texto** de capacidad fija con `operator<<`.
///
/// Es la fachada de composición de HUD/diagnóstico: el llamador encadena texto, enteros
/// y `hex(v, dígitos)`/`dec(v, ancho)`/`fixed(valor, decimales)`; por dentro usa
/// `StaticString`/`to_chars`/`to_chars_hex`, sin heap,
/// sin iostream y sin excepciones. Si una escritura no cabe, `truncated()` queda a `true`
/// y las siguientes no tocan memoria (el texto se trunca, nunca desborda).
///
/// ```cpp
/// eng::util::TextLine<96> line;
/// line << "frames: " << frame << "  last: 0x" << eng::util::hex(last, 4u);
/// line << "  fps: " << eng::util::fixed(fps, 2u) << "  n: " << eng::util::dec(n, 3u);
/// overlay.text(x, y, line.c_str(), rgb);
/// ```
///
/// El `operator<<` es un **DSL de dominio**, no `std::ostream`: no hay formato dinámico,
/// locale ni asignación. La capacidad `N` incluye el terminador.

#include <eng/core/types/types.hpp>
#include <eng/core/util/static_string.hpp>
#include <eng/core/util/string_view.hpp>
#include <eng/core/util/text.hpp>

namespace eng::util {

/// Manipulador de hexadecimal para `TextLine`: `hex(value, digits)`.
struct Hex {
	u32 value = 0u;
	u8 digits = 1u;
};

[[nodiscard]] constexpr Hex hex(u32 value, u8 digits = 1u) noexcept {
	return Hex {value, digits};
}

/// Manipulador decimal con **ancho mínimo** (relleno de ceros): `dec(7, 3)` → `007`.
struct Decimal {
	u32 value = 0u;
	u8 min_digits = 1u;
};

[[nodiscard]] constexpr Decimal dec(u32 value, u8 min_digits = 1u) noexcept {
	return Decimal {value, min_digits};
}

/// Manipulador de un entero **ya escalado** por 10^digits: `scaled(5960, 2)` → `59.60`.
struct Scaled {
	u32 value = 0u;
	u8 digits = 0u;
};

[[nodiscard]] constexpr Scaled scaled(u32 value, u8 digits = 0u) noexcept {
	return Scaled {value, digits};
}

/// Manipulador de un escalar fraccionario (`Fixed`, …) con `digits` decimales.
template <class S>
struct FixedValue {
	S value {};
	u8 digits = 0u;
};

template <class S>
	requires decimal_scalar<S>
[[nodiscard]] constexpr FixedValue<S> fixed(S value, u8 digits) noexcept {
	return FixedValue<S> {value, digits};
}

template <usize N>
class TextLine {
	static_assert(N >= 1u, "TextLine: N incluye el terminador, debe ser >= 1");

public:
	constexpr TextLine() noexcept = default;

	/// Texto literal (incluye el tamaño en compilación; no hay `strlen`).
	template <usize M>
	TextLine& operator<<(const char (&text)[M]) noexcept {
		add(StringView {text, M - 1u});
		return *this;
	}

	TextLine& operator<<(StringView text) noexcept {
		add(text);
		return *this;
	}

	TextLine& operator<<(const char* text) noexcept {
		if (text != nullptr) {
			add(StringView {text});
		}
		return *this;
	}

	TextLine& operator<<(char c) noexcept {
		if (!m_truncated && !m_text.append(c)) {
			m_truncated = true;
		}
		return *this;
	}

	template <class T>
		requires decimal_integer<T> && (!same_as<T, char>)
	TextLine& operator<<(T value) noexcept {
		if (!m_truncated && !to_chars(m_text, value)) {
			m_truncated = true;
		}
		return *this;
	}

	TextLine& operator<<(Hex value) noexcept {
		if (!m_truncated && !to_chars_hex(m_text, value.value, value.digits)) {
			m_truncated = true;
		}
		return *this;
	}

	TextLine& operator<<(Decimal value) noexcept {
		if (m_truncated) {
			return *this;
		}
		StaticString<11> digits;
		if (!to_chars(digits, value.value)) {
			m_truncated = true;
			return *this;
		}
		for (u8 i = digits.size(); i < value.min_digits; ++i) {
			if (!m_text.append('0')) {
				m_truncated = true;
				return *this;
			}
		}
		add(digits.view());
		return *this;
	}

	TextLine& operator<<(Scaled value) noexcept {
		if (!m_truncated && !to_chars_scaled(m_text, value.value, value.digits)) {
			m_truncated = true;
		}
		return *this;
	}

	template <class S>
		requires decimal_scalar<S>
	TextLine& operator<<(FixedValue<S> value) noexcept {
		if (!m_truncated && !to_chars(m_text, value.value, value.digits)) {
			m_truncated = true;
		}
		return *this;
	}

	[[nodiscard]] constexpr const char* c_str() const noexcept { return m_text.c_str(); }
	[[nodiscard]] constexpr StringView view() const noexcept { return m_text.view(); }
	[[nodiscard]] constexpr usize size() const noexcept { return m_text.size(); }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_text.empty(); }
	[[nodiscard]] constexpr bool truncated() const noexcept { return m_truncated; }

	constexpr void clear() noexcept {
		m_text.clear();
		m_truncated = false;
	}

private:
	constexpr void add(StringView text) noexcept {
		if (!m_truncated && !m_text.append(text)) {
			m_truncated = true;
		}
	}

	StaticString<N> m_text;
	bool m_truncated = false;
};

} // namespace eng::util
