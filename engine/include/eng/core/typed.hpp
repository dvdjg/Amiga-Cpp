#pragma once

/// \file typed.hpp
/// Fundamento del **sistema de tipos internos** (`INTERNAL_TYPE_SYSTEM.md`): vistas
/// contiguas con *tag* de dominio y unidades fuertes. El objetivo es que un error
/// de dominio no compile: un buffer de audio no puede usarse como origen de un
/// Blitter gráfico sin una conversión explícita.
///
/// - `Bytes<Tag>` / `ByteView<Tag>`: rango de bytes de un dominio concreto.
/// - `Words<Tag>` / `WordView<Tag>`: rango de words (u16) de un dominio concreto.
/// - Vocabulario: `PlaneIndex`/`PlaneCount`, `RowBytes`, `PlaneBytes`,
///   `PixelWidth`/`PixelHeight`, `WordCount`, `ColorCount`, `ByteStride`.
/// - Direcciones/base: `BitmapBase`, `FrontBase`, `ChipAddress`.
///
/// Coste: envoltorios trivialmente copiables del mismo tamaño que `Span`; sin
/// virtuals, sin heap, `constexpr`. `raw()` es la frontera explícita hacia la capa
/// unsafe (backend). Ver reglas en `CODING_STYLE.md`.

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>

namespace eng {

namespace detail {
/// Detiene la CPU ante un índice de plano inválido (como `Span::at`).
[[noreturn]] inline void typed_range_error() { __builtin_trap(); }
} // namespace detail

// --- Vistas de bytes con TAG de dominio --------------------------------------

// Declaraciones adelantadas: las vistas se convierten entre sí (`as_words`/`as_bytes`).
template <class Tag> class Bytes;
template <class Tag> class ByteView;
template <class Tag> class Words;
template <class Tag> class WordView;

/// Rango mutable de bytes de un dominio. `Tag` es un struct vacío por dominio.
/// Replica la ergonomía de `Span`: constructor de array (deduce el tamaño),
/// iteradores para range-for y conversiones de dominio explícitas.
template <class Tag>
class Bytes {
public:
	using value_type = eng::u8;
	using iterator = eng::u8*;
	using const_iterator = const eng::u8*;
	using size_type = eng::usize;

	constexpr Bytes() noexcept = default;
	constexpr Bytes(eng::u8* data, size_type count) noexcept : m_span(data, count) {}
	template <size_type N>
	constexpr Bytes(eng::u8 (&arr)[N]) noexcept : m_span(arr, N) {}
	static constexpr Bytes from(Span<eng::u8> s) noexcept { return Bytes(s.data(), s.size()); }

	/// Frontera explícita hacia la capa unsafe.
	[[nodiscard]] constexpr Span<eng::u8> raw() const noexcept { return m_span; }
	[[nodiscard]] constexpr eng::u8* data() const noexcept { return m_span.data(); }
	[[nodiscard]] constexpr size_type size() const noexcept { return m_span.size(); }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_span.empty(); }

	[[nodiscard]] constexpr iterator begin() const noexcept { return m_span.data(); }
	[[nodiscard]] constexpr iterator end() const noexcept { return m_span.data() + m_span.size(); }
	[[nodiscard]] constexpr const_iterator cbegin() const noexcept { return m_span.data(); }
	[[nodiscard]] constexpr const_iterator cend() const noexcept { return m_span.data() + m_span.size(); }

	constexpr eng::u8& operator[](size_type i) const noexcept { return m_span[i]; }
	constexpr eng::u8& at(size_type i) const noexcept { return m_span.at(i); }
	[[nodiscard]] constexpr eng::u8& front() const noexcept { return m_span[0]; }
	[[nodiscard]] constexpr eng::u8& back() const noexcept { return m_span[m_span.size() - 1u]; }

	[[nodiscard]] constexpr Bytes subspan(size_type off, size_type n) const noexcept {
		return Bytes(m_span.data() + off, n);
	}
	[[nodiscard]] constexpr Bytes subspan(size_type off) const noexcept {
		return Bytes(m_span.data() + off, m_span.size() - off);
	}
	/// Vista de solo lectura de la misma memoria.
	[[nodiscard]] constexpr ByteView<Tag> as_const() const noexcept {
		return ByteView<Tag>(m_span.data(), m_span.size());
	}
	/// Reinterpretación explícita a words del MISMO dominio (requiere tamaño par y
	/// alineación a 2 del inicio; el contrato lo documenta el llamador).
	[[nodiscard]] constexpr Words<Tag> as_words() const noexcept {
		return Words<Tag>{reinterpret_cast<eng::u16*>(m_span.data()), m_span.size() / 2u};
	}
	constexpr void fill(eng::u8 v) const noexcept { m_span.fill(v); }

private:
	Span<eng::u8> m_span {};
};

/// Rango de solo lectura de bytes de un dominio.
template <class Tag>
class ByteView {
public:
	using value_type = eng::u8;
	using iterator = const eng::u8*;
	using const_iterator = const eng::u8*;
	using size_type = eng::usize;

	constexpr ByteView() noexcept = default;
	constexpr ByteView(const eng::u8* data, size_type count) noexcept : m_span(data, count) {}
	template <size_type N>
	constexpr ByteView(const eng::u8 (&arr)[N]) noexcept : m_span(arr, N) {}
	static constexpr ByteView from(Span<const eng::u8> s) noexcept { return ByteView(s.data(), s.size()); }

	[[nodiscard]] constexpr Span<const eng::u8> raw() const noexcept { return m_span; }
	[[nodiscard]] constexpr const eng::u8* data() const noexcept { return m_span.data(); }
	[[nodiscard]] constexpr size_type size() const noexcept { return m_span.size(); }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_span.empty(); }

	[[nodiscard]] constexpr iterator begin() const noexcept { return m_span.data(); }
	[[nodiscard]] constexpr iterator end() const noexcept { return m_span.data() + m_span.size(); }
	[[nodiscard]] constexpr const_iterator cbegin() const noexcept { return m_span.data(); }
	[[nodiscard]] constexpr const_iterator cend() const noexcept { return m_span.data() + m_span.size(); }

	constexpr const eng::u8& operator[](size_type i) const noexcept { return m_span[i]; }
	constexpr const eng::u8& at(size_type i) const noexcept { return m_span.at(i); }
	[[nodiscard]] constexpr const eng::u8& front() const noexcept { return m_span[0]; }
	[[nodiscard]] constexpr const eng::u8& back() const noexcept { return m_span[m_span.size() - 1u]; }

	[[nodiscard]] constexpr ByteView subspan(size_type off, size_type n) const noexcept {
		return ByteView(m_span.data() + off, n);
	}
	[[nodiscard]] constexpr ByteView subspan(size_type off) const noexcept {
		return ByteView(m_span.data() + off, m_span.size() - off);
	}
	[[nodiscard]] constexpr WordView<Tag> as_words() const noexcept {
		return WordView<Tag>{reinterpret_cast<const eng::u16*>(m_span.data()), m_span.size() / 2u};
	}

private:
	Span<const eng::u8> m_span {};
};

/// Rango mutable de words (u16) de un dominio.
template <class Tag>
class Words {
public:
	using value_type = eng::u16;
	using iterator = eng::u16*;
	using const_iterator = const eng::u16*;
	using size_type = eng::usize;

	constexpr Words() noexcept = default;
	constexpr Words(eng::u16* data, size_type count) noexcept : m_span(data, count) {}
	template <size_type N>
	constexpr Words(eng::u16 (&arr)[N]) noexcept : m_span(arr, N) {}
	static constexpr Words from(Span<eng::u16> s) noexcept { return Words(s.data(), s.size()); }

	[[nodiscard]] constexpr Span<eng::u16> raw() const noexcept { return m_span; }
	[[nodiscard]] constexpr eng::u16* data() const noexcept { return m_span.data(); }
	[[nodiscard]] constexpr size_type size() const noexcept { return m_span.size(); }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_span.empty(); }

	[[nodiscard]] constexpr iterator begin() const noexcept { return m_span.data(); }
	[[nodiscard]] constexpr iterator end() const noexcept { return m_span.data() + m_span.size(); }
	[[nodiscard]] constexpr const_iterator cbegin() const noexcept { return m_span.data(); }
	[[nodiscard]] constexpr const_iterator cend() const noexcept { return m_span.data() + m_span.size(); }

	constexpr eng::u16& operator[](size_type i) const noexcept { return m_span[i]; }
	constexpr eng::u16& at(size_type i) const noexcept { return m_span.at(i); }
	[[nodiscard]] constexpr eng::u16& front() const noexcept { return m_span[0]; }
	[[nodiscard]] constexpr eng::u16& back() const noexcept { return m_span[m_span.size() - 1u]; }

	[[nodiscard]] constexpr Words subspan(size_type off, size_type n) const noexcept {
		return Words(m_span.data() + off, n);
	}
	[[nodiscard]] constexpr Words subspan(size_type off) const noexcept {
		return Words(m_span.data() + off, m_span.size() - off);
	}
	[[nodiscard]] constexpr WordView<Tag> as_const() const noexcept {
		return WordView<Tag>(m_span.data(), m_span.size());
	}
	/// Reinterpretación explícita a bytes del MISMO dominio (mismo tamaño).
	[[nodiscard]] constexpr Bytes<Tag> as_bytes() const noexcept {
		return Bytes<Tag>{reinterpret_cast<eng::u8*>(m_span.data()), m_span.size() * 2u};
	}

private:
	Span<eng::u16> m_span {};
};

/// Rango de solo lectura de words (u16) de un dominio.
template <class Tag>
class WordView {
public:
	using value_type = eng::u16;
	using iterator = const eng::u16*;
	using const_iterator = const eng::u16*;
	using size_type = eng::usize;

	constexpr WordView() noexcept = default;
	constexpr WordView(const eng::u16* data, size_type count) noexcept : m_span(data, count) {}
	template <size_type N>
	constexpr WordView(const eng::u16 (&arr)[N]) noexcept : m_span(arr, N) {}
	static constexpr WordView from(Span<const eng::u16> s) noexcept { return WordView(s.data(), s.size()); }

	[[nodiscard]] constexpr Span<const eng::u16> raw() const noexcept { return m_span; }
	[[nodiscard]] constexpr const eng::u16* data() const noexcept { return m_span.data(); }
	[[nodiscard]] constexpr size_type size() const noexcept { return m_span.size(); }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_span.empty(); }

	[[nodiscard]] constexpr iterator begin() const noexcept { return m_span.data(); }
	[[nodiscard]] constexpr iterator end() const noexcept { return m_span.data() + m_span.size(); }
	[[nodiscard]] constexpr const_iterator cbegin() const noexcept { return m_span.data(); }
	[[nodiscard]] constexpr const_iterator cend() const noexcept { return m_span.data() + m_span.size(); }

	constexpr const eng::u16& operator[](size_type i) const noexcept { return m_span[i]; }
	constexpr const eng::u16& at(size_type i) const noexcept { return m_span.at(i); }
	[[nodiscard]] constexpr const eng::u16& front() const noexcept { return m_span[0]; }
	[[nodiscard]] constexpr const eng::u16& back() const noexcept { return m_span[m_span.size() - 1u]; }

	[[nodiscard]] constexpr WordView subspan(size_type off, size_type n) const noexcept {
		return WordView(m_span.data() + off, n);
	}
	[[nodiscard]] constexpr WordView subspan(size_type off) const noexcept {
		return WordView(m_span.data() + off, m_span.size() - off);
	}
	[[nodiscard]] constexpr ByteView<Tag> as_bytes() const noexcept {
		return ByteView<Tag>{reinterpret_cast<const eng::u8*>(m_span.data()), m_span.size() * 2u};
	}

private:
	Span<const eng::u16> m_span {};
};

// --- Unidades fuertes (evitan intercambiar parámetros) -----------------------

// --- Direcciones y bases (semántica distinta a propósito) --------------------
//
// Nota de estilo: NO se envuelven escalares (ancho/alto/stride/planes…) en tipos
// fuertes. Solo se tipan buffers/punteros y direcciones: envolver enteros no aporta
// seguridad real y ensucia las llamadas. Los escalares van como `u8`/`u16`/`u32`.

/// Base de la reserva de un bitmap (lo que va a `BPLxPT`).
struct BitmapBase { eng::u8* value = nullptr; };
/// Buffer de escritura de un bitmap (con `frontbase_offset`).
struct FrontBase { eng::u8* value = nullptr; };
/// Dirección DMA-visible (chip RAM), en formato entero.
struct ChipAddress { eng::uintptr value = 0; };

} // namespace eng
