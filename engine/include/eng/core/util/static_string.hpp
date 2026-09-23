#pragma once

/// \file static_string.hpp
/// `eng::util::StaticString<N>`: buffer de texto **de capacidad fija** (sin heap) que
/// siempre termina en `'\0'`.
///
/// Es la pareja de escritura de `StringView`: donde `StringView` describe texto
/// existente, `StaticString<N>` lo **construye** (HUD, marcadores, nombres de recurso
/// generados). `N` incluye el terminador, así que admite `N-1` caracteres. No copia en
/// el heap y no lanza: `append` devuelve `false` si no cabe (sin escribir a medias).
///
/// Uso:
///   eng::util::StaticString<16> score;
///   score.assign(eng::util::StringView("PUNTOS: "));
///   score.append('9');
///   draw(score.view());   // como StringView

#include <eng/core/types/types.hpp>
#include <eng/core/util/string_view.hpp>

namespace eng::util {

template <usize N>
class StaticString {
	static_assert(N >= 1u, "StaticString: N incluye el terminador, debe ser >= 1");

public:
	[[nodiscard]] static constexpr usize capacity() noexcept { return N - 1u; }

	constexpr StaticString() noexcept = default;

	explicit constexpr StaticString(StringView text) noexcept { assign(text); }

	/// Copia `text` (como mucho `capacity()` caracteres, recortando).
	constexpr void assign(StringView text) noexcept {
		const usize n = text.size() < capacity() ? text.size() : capacity();
		for (usize i = 0; i < n; ++i) {
			m_data[i] = text[i];
		}
		m_size = n;
		m_data[n] = '\0';
	}

	/// Añade `text`; `false` (sin cambio) si no cabe entero.
	constexpr bool append(StringView text) noexcept {
		if (text.size() > capacity() - m_size) {
			return false;
		}
		for (usize i = 0; i < text.size(); ++i) {
			m_data[m_size + i] = text[i];
		}
		m_size += text.size();
		m_data[m_size] = '\0';
		return true;
	}

	/// Añade un carácter; `false` (sin cambio) si está llena.
	constexpr bool append(char c) noexcept {
		if (m_size >= capacity()) {
			return false;
		}
		m_data[m_size++] = c;
		m_data[m_size] = '\0';
		return true;
	}

	constexpr void clear() noexcept {
		m_size = 0u;
		m_data[0] = '\0';
	}

	[[nodiscard]] constexpr usize size() const noexcept { return m_size; }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0u; }
	[[nodiscard]] constexpr bool full() const noexcept { return m_size == capacity(); }

	[[nodiscard]] constexpr const char* c_str() const noexcept { return m_data; }
	[[nodiscard]] constexpr const char* data() const noexcept { return m_data; }
	[[nodiscard]] constexpr char* data() noexcept { return m_data; }

	[[nodiscard]] constexpr char operator[](usize index) const noexcept { return m_data[index]; }

	/// Vista de solo lectura del contenido (para las APIs que piden `StringView`).
	[[nodiscard]] constexpr StringView view() const noexcept {
		return StringView {m_data, m_size};
	}

private:
	char m_data[N] {};
	usize m_size = 0u;
};

} // namespace eng::util
