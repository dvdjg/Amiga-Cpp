#pragma once

/// \file text_line.hpp
/// `eng::util::TextLine<N>`: **builder de texto** de capacidad fija con `operator<<`.
///
/// Es la fachada de composición de HUD/diagnóstico: el llamador encadena texto, enteros
/// y `hex(v, dígitos)`; por dentro usa `StaticString`/`to_chars`/`to_chars_hex`, sin heap,
/// sin iostream y sin excepciones. Si una escritura no cabe, `truncated()` queda a `true`
/// y las siguientes no tocan memoria (el texto se trunca, nunca desborda).
///
/// ```cpp
/// eng::util::TextLine<96> line;
/// line << "frames: " << frame << "  last: 0x" << eng::util::hex(last, 4u);
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
