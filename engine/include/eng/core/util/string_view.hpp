#pragma once

/// \file string_view.hpp
/// `eng::util::StringView`: vista de texto no propietaria (`std::string_view`).
///
/// El engine maneja cadenas de longitud variable sin poder asignar memoria: texto
/// de HUD, nombres de recurso, mensajes de depuración. `StringView` es un puntero
/// más un tamaño; no copia ni posee, así que se pasa por valor y describe tanto un
/// literal (`"PUNTOS"`) como un tramo de un buffer mayor. Encaja con el decodificador
/// UTF-8 (`eng/core/data/utf8.hpp`) y con `Surface::draw_text`.
///
/// Construcción desde `const char*` calcula la longitud (recorrido hasta el NUL);
/// desde `(puntero, tamaño)` no recorre nada. **No** hay constructor desde array C
/// para no confundir un literal con un buffer sin terminador: un buffer de tamaño
/// conocido se construye como `StringView(buf, n)`.
///
/// Uso:
///   void draw(eng::util::StringView text);
///   draw("VIDAS");                       // literal
///   draw(eng::util::StringView(buf, 8)); // tramo

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace eng::util {

class StringView {
public:
	static constexpr usize npos = static_cast<usize>(-1);

	constexpr StringView() noexcept = default;

	/// Desde cadena terminada en NUL (recorre hasta el terminador).
	constexpr StringView(const char* text) noexcept : m_data(text), m_size(length(text)) {}

	/// Desde puntero y longitud (no recorre la memoria).
	constexpr StringView(const char* text, usize size) noexcept : m_data(text), m_size(size) {}

	/// Desde una vista contigua de caracteres (segura, sin puntero suelto).
	constexpr explicit StringView(eng::Span<const char> text) noexcept
	    : m_data(text.data()), m_size(text.size()) {}

	[[nodiscard]] constexpr const char* data() const noexcept { return m_data; }
	[[nodiscard]] constexpr usize size() const noexcept { return m_size; }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0u; }

	[[nodiscard]] constexpr const char* begin() const noexcept { return m_data; }
	[[nodiscard]] constexpr const char* end() const noexcept { return m_data + m_size; }

	[[nodiscard]] constexpr char operator[](usize index) const noexcept { return m_data[index]; }

	[[nodiscard]] constexpr char front() const noexcept { return m_data[0]; }
	[[nodiscard]] constexpr char back() const noexcept { return m_data[m_size - 1u]; }

	/// Vista contigua de los bytes (para APIs que piden `Span`).
	[[nodiscard]] constexpr eng::Span<const char> span() const noexcept {
		return eng::Span<const char> {m_data, m_size};
	}

	/// Subcadena a partir de `pos` (a lo sumo `count` caracteres). Si `pos` está
	/// fuera, la vista queda vacía al final.
	[[nodiscard]] constexpr StringView substr(usize pos, usize count = npos) const noexcept {
		if (pos > m_size) {
			return StringView(m_data + m_size, 0u);
		}
		const usize rest = m_size - pos;
		const usize n = count < rest ? count : rest;
		return StringView(m_data + pos, n);
	}

	constexpr void remove_prefix(usize n) noexcept {
		const usize k = n < m_size ? n : m_size;
		m_data += k;
		m_size -= k;
	}

	constexpr void remove_suffix(usize n) noexcept {
		const usize k = n < m_size ? n : m_size;
		m_size -= k;
	}

	[[nodiscard]] constexpr bool starts_with(char c) const noexcept {
		return m_size != 0u && m_data[0] == c;
	}
	[[nodiscard]] constexpr bool starts_with(StringView prefix) const noexcept {
		if (prefix.m_size > m_size) {
			return false;
		}
		for (usize i = 0; i < prefix.m_size; ++i) {
			if (m_data[i] != prefix.m_data[i]) {
				return false;
			}
		}
		return true;
	}
	[[nodiscard]] constexpr bool ends_with(char c) const noexcept {
		return m_size != 0u && m_data[m_size - 1u] == c;
	}
	[[nodiscard]] constexpr bool ends_with(StringView suffix) const noexcept {
		if (suffix.m_size > m_size) {
			return false;
		}
		const usize offset = m_size - suffix.m_size;
		for (usize i = 0; i < suffix.m_size; ++i) {
			if (m_data[offset + i] != suffix.m_data[i]) {
				return false;
			}
		}
		return true;
	}

	/// Primera aparición de `c` desde `pos`; `npos` si no está.
	[[nodiscard]] constexpr usize find(char c, usize pos = 0u) const noexcept {
		for (usize i = pos; i < m_size; ++i) {
			if (m_data[i] == c) {
				return i;
			}
		}
		return npos;
	}

	/// Primera aparición de `needle` desde `pos`; `npos` si no está.
	[[nodiscard]] constexpr usize find(StringView needle, usize pos = 0u) const noexcept {
		if (needle.m_size == 0u) {
			return pos <= m_size ? pos : npos;
		}
		if (needle.m_size > m_size) {
			return npos;
		}
		for (usize i = pos; i + needle.m_size <= m_size; ++i) {
			if (substr(i, needle.m_size) == needle) {
				return i;
			}
		}
		return npos;
	}

	[[nodiscard]] friend constexpr bool operator==(StringView a, StringView b) noexcept {
		if (a.m_size != b.m_size) {
			return false;
		}
		for (usize i = 0; i < a.m_size; ++i) {
			if (a.m_data[i] != b.m_data[i]) {
				return false;
			}
		}
		return true;
	}
	[[nodiscard]] friend constexpr bool operator!=(StringView a, StringView b) noexcept {
		return !(a == b);
	}

private:
	[[nodiscard]] static constexpr usize length(const char* text) noexcept {
		usize n = 0;
		if (text != nullptr) {
			while (text[n] != '\0') {
				++n;
			}
		}
		return n;
	}

	const char* m_data = nullptr;
	usize m_size = 0u;
};

} // namespace eng::util
