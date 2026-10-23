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
	u32 value = 0u; ///< valor a escribir en hexadecimal
	u8 digits = 1u; ///< mínimo de dígitos (relleno de ceros a la izquierda)
};

/// Construye un manipulador hexadecimal.
/// \param value valor u32 a formatear.
/// \param digits mínimo de dígitos (1..8 útil; no recorta el valor).
/// \return manipulador para `TextLine::operator<<`.
[[nodiscard]] constexpr Hex hex(u32 value, u8 digits = 1u) noexcept {
	return Hex {value, digits};
}

/// Manipulador decimal con **ancho mínimo** (relleno de ceros): `dec(7, 3)` → `007`.
struct Decimal {
	u32 value = 0u;      ///< valor decimal a escribir
	u8 min_digits = 1u;  ///< mínimo de dígitos; si el valor tiene más, no se recorta
};

/// Construye un manipulador decimal con ancho mínimo.
/// \param value valor decimal a escribir.
/// \param min_digits mínimo de dígitos (relleno de ceros).
/// \return manipulador para `TextLine::operator<<`.
[[nodiscard]] constexpr Decimal dec(u32 value, u8 min_digits = 1u) noexcept {
	return Decimal {value, min_digits};
}

/// Manipulador de un entero **ya escalado** por 10^digits: `scaled(5960, 2)` → `59.60`.
struct Scaled {
	u32 value = 0u; ///< valor entero ya multiplicado por 10^`digits`
	u8 digits = 0u; ///< decimales a insertar (0 = entero; 1..9 útil)
};

/// Construye un manipulador de entero escalado.
/// \param value valor ya escalado por 10^digits.
/// \param digits decimales a insertar (0 = sin punto).
/// \return manipulador para `TextLine::operator<<`.
[[nodiscard]] constexpr Scaled scaled(u32 value, u8 digits = 0u) noexcept {
	return Scaled {value, digits};
}

/// Manipulador de un escalar fraccionario (`Fixed`, …) con `digits` decimales.
/// \tparam S escalar que cumple `decimal_scalar` (p. ej. `Fixed` o `float` si aplica).
template <class S>
struct FixedValue {
	S value {};    ///< valor del escalar a formatear
	u8 digits = 0u; ///< decimales a escribir (0 = sin parte fraccionaria)
};

/// Construye un manipulador `fixed(value, digits)`.
/// \tparam S escalar decimal (`decimal_scalar`) que se pasa a `to_chars`.
/// \param value valor a formatear.
/// \param digits decimales a escribir (0..9 útil; el formateador trunca, no redondea).
/// \return manipulador para `TextLine::operator<<`.
template <class S>
	requires decimal_scalar<S>
[[nodiscard]] constexpr FixedValue<S> fixed(S value, u8 digits) noexcept {
	return FixedValue<S> {value, digits};
}

/// Builder de texto de capacidad fija con terminador incluido.
/// \tparam N Capacidad total en bytes, **incluido el terminador** (`N >= 1`). El texto
///         útil máximo es `N - 1`; al superarlo se trunca y `truncated()` pasa a `true`.
template <usize N>
class TextLine {
	static_assert(N >= 1u, "TextLine: N incluye el terminador, debe ser >= 1");

public:
	/// Construye una línea vacía (`c_str()` devuelve `""`).
	constexpr TextLine() noexcept = default;

	/// Añade un literal de tamaño conocido en compilación (sin `strlen`).
	/// \tparam M tamaño del array literal (incluye el terminador).
	/// \param text literal a añadir; se copia hasta `M - 1`.
	/// \return `*this` para encadenar.
	template <usize M>
	TextLine& operator<<(const char (&text)[M]) noexcept {
		add(StringView {text, M - 1u});
		return *this;
	}

	/// Añade una vista de texto con tamaño explícito.
	/// \param text vista a copiar (puede ser vacía).
	/// \return `*this` para encadenar.
	TextLine& operator<<(StringView text) noexcept {
		add(text);
		return *this;
	}

	/// Añade una cadena C (null-terminated); `nullptr` se ignora.
	/// \param text cadena a añadir, o `nullptr`.
	/// \return `*this` para encadenar.
	TextLine& operator<<(const char* text) noexcept {
		if (text != nullptr) {
			add(StringView {text});
		}
		return *this;
	}

	/// Añade un carácter.
	/// \param c carácter a añadir.
	/// \return `*this` para encadenar.
	TextLine& operator<<(char c) noexcept {
		if (!m_truncated && !m_text.append(c)) {
			m_truncated = true;
		}
		return *this;
	}

	/// Añade un entero decimal (`u8`/`s16`/`u32`…), sin `std::` ni locale.
	/// \tparam T entero que cumple `decimal_integer` (excepto `char`).
	/// \param value valor a formatear.
	/// \return `*this` para encadenar.
	template <class T>
		requires decimal_integer<T> && (!same_as<T, char>)
	TextLine& operator<<(T value) noexcept {
		if (!m_truncated && !to_chars(m_text, value)) {
			m_truncated = true;
		}
		return *this;
	}

	/// Añade un valor hexadecimal con `digits` mínimos.
	/// \param value manipulador creado por `hex()`.
	/// \return `*this` para encadenar.
	TextLine& operator<<(Hex value) noexcept {
		if (!m_truncated && !to_chars_hex(m_text, value.value, value.digits)) {
			m_truncated = true;
		}
		return *this;
	}

	/// Añade un decimal con ancho mínimo (relleno de ceros).
	/// \param value manipulador creado por `dec()`.
	/// \return `*this` para encadenar.
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

	/// Añade un entero ya escalado (punto decimal insertado).
	/// \param value manipulador creado por `scaled()`.
	/// \return `*this` para encadenar.
	TextLine& operator<<(Scaled value) noexcept {
		if (!m_truncated && !to_chars_scaled(m_text, value.value, value.digits)) {
			m_truncated = true;
		}
		return *this;
	}

	/// Añade un escalar fraccionario con `digits` decimales.
	/// \tparam S escalar decimal (`decimal_scalar`).
	/// \param value manipulador creado por `fixed()`.
	/// \return `*this` para encadenar.
	template <class S>
		requires decimal_scalar<S>
	TextLine& operator<<(FixedValue<S> value) noexcept {
		if (!m_truncated && !to_chars(m_text, value.value, value.digits)) {
			m_truncated = true;
		}
		return *this;
	}

	/// Texto resultante como cadena C (válida hasta la próxima escritura).
	[[nodiscard]] constexpr const char* c_str() const noexcept { return m_text.c_str(); }
	/// Texto resultante como vista (tamaño + puntero no propietario).
	[[nodiscard]] constexpr StringView view() const noexcept { return m_text.view(); }
	/// Longitud actual en bytes, sin terminador.
	[[nodiscard]] constexpr usize size() const noexcept { return m_text.size(); }
	/// `true` si no se ha escrito nada (o se hizo `clear()`).
	[[nodiscard]] constexpr bool empty() const noexcept { return m_text.empty(); }
	/// `true` si alguna escritura no cupo; las siguientes se ignoran.
	[[nodiscard]] constexpr bool truncated() const noexcept { return m_truncated; }

	/// Vacía el texto y limpia `truncated()`.
	constexpr void clear() noexcept {
		m_text.clear();
		m_truncated = false;
	}

private:
	/// Añade una vista; si no cabe, marca truncado y no escribe parcialmente.
	/// \param text vista de origen (no propietaria).
	constexpr void add(StringView text) noexcept {
		if (!m_truncated && !m_text.append(text)) {
			m_truncated = true;
		}
	}

	StaticString<N> m_text;   ///< buffer de capacidad N (incluye terminador)
	bool m_truncated = false; ///< `true` tras la primera escritura que no cupo
};

} // namespace eng::util
