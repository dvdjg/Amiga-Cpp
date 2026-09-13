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

/// Rango mutable de bytes de un dominio. `Tag` es un struct vacío por dominio.
template <class Tag>
class Bytes {
public:
	constexpr Bytes() noexcept = default;
	constexpr Bytes(eng::u8* data, eng::usize count) noexcept : m_span(data, count) {}
	static constexpr Bytes from(Span<eng::u8> s) noexcept { return Bytes(s.data(), s.size()); }

	/// Frontera explícita hacia la capa unsafe.
	[[nodiscard]] constexpr Span<eng::u8> raw() const noexcept { return m_span; }
	[[nodiscard]] constexpr eng::u8* data() const noexcept { return m_span.data(); }
	[[nodiscard]] constexpr eng::usize size() const noexcept { return m_span.size(); }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_span.empty(); }
	constexpr eng::u8& operator[](eng::usize i) const noexcept { return m_span[i]; }
	constexpr eng::u8& at(eng::usize i) const noexcept { return m_span.at(i); }
	[[nodiscard]] constexpr Bytes subspan(eng::usize off, eng::usize n) const noexcept {
		return Bytes(m_span.data() + off, n);
	}
	/// Reinterpretación explícita a words del MISMO dominio (requiere tamaño par y
	/// alineación a 2 del inicio; el contrato lo documenta el llamador).
	[[nodiscard]] constexpr Span<eng::u16> as_words() const noexcept {
		return Span<eng::u16>(reinterpret_cast<eng::u16*>(m_span.data()), m_span.size() / 2u);
	}
	constexpr void fill(eng::u8 v) const noexcept { m_span.fill(v); }

private:
	Span<eng::u8> m_span {};
};

/// Rango de solo lectura de bytes de un dominio.
template <class Tag>
class ByteView {
public:
	constexpr ByteView() noexcept = default;
	constexpr ByteView(const eng::u8* data, eng::usize count) noexcept : m_span(data, count) {}
	static constexpr ByteView from(Span<const eng::u8> s) noexcept { return ByteView(s.data(), s.size()); }

	[[nodiscard]] constexpr Span<const eng::u8> raw() const noexcept { return m_span; }
	[[nodiscard]] constexpr const eng::u8* data() const noexcept { return m_span.data(); }
	[[nodiscard]] constexpr eng::usize size() const noexcept { return m_span.size(); }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_span.empty(); }
	constexpr const eng::u8& operator[](eng::usize i) const noexcept { return m_span[i]; }
	constexpr const eng::u8& at(eng::usize i) const noexcept { return m_span.at(i); }
	[[nodiscard]] constexpr ByteView subspan(eng::usize off, eng::usize n) const noexcept {
		return ByteView(m_span.data() + off, n);
	}
	[[nodiscard]] constexpr Span<const eng::u16> as_words() const noexcept {
		return Span<const eng::u16>(reinterpret_cast<const eng::u16*>(m_span.data()), m_span.size() / 2u);
	}

private:
	Span<const eng::u8> m_span {};
};

/// Rango mutable de words (u16) de un dominio.
template <class Tag>
class Words {
public:
	constexpr Words() noexcept = default;
	constexpr Words(eng::u16* data, eng::usize count) noexcept : m_span(data, count) {}
	static constexpr Words from(Span<eng::u16> s) noexcept { return Words(s.data(), s.size()); }

	[[nodiscard]] constexpr Span<eng::u16> raw() const noexcept { return m_span; }
	[[nodiscard]] constexpr eng::u16* data() const noexcept { return m_span.data(); }
	[[nodiscard]] constexpr eng::usize size() const noexcept { return m_span.size(); }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_span.empty(); }
	constexpr eng::u16& operator[](eng::usize i) const noexcept { return m_span[i]; }
	constexpr eng::u16& at(eng::usize i) const noexcept { return m_span.at(i); }
	[[nodiscard]] constexpr Words subspan(eng::usize off, eng::usize n) const noexcept {
		return Words(m_span.data() + off, n);
	}

private:
	Span<eng::u16> m_span {};
};

/// Rango de solo lectura de words (u16) de un dominio.
template <class Tag>
class WordView {
public:
	constexpr WordView() noexcept = default;
	constexpr WordView(const eng::u16* data, eng::usize count) noexcept : m_span(data, count) {}
	static constexpr WordView from(Span<const eng::u16> s) noexcept { return WordView(s.data(), s.size()); }

	[[nodiscard]] constexpr Span<const eng::u16> raw() const noexcept { return m_span; }
	[[nodiscard]] constexpr const eng::u16* data() const noexcept { return m_span.data(); }
	[[nodiscard]] constexpr eng::usize size() const noexcept { return m_span.size(); }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_span.empty(); }
	constexpr const eng::u16& operator[](eng::usize i) const noexcept { return m_span[i]; }
	constexpr const eng::u16& at(eng::usize i) const noexcept { return m_span.at(i); }
	[[nodiscard]] constexpr WordView subspan(eng::usize off, eng::usize n) const noexcept {
		return WordView(m_span.data() + off, n);
	}

private:
	Span<const eng::u16> m_span {};
};

// --- Unidades fuertes (evitan intercambiar parámetros) -----------------------

struct PlaneCount { eng::u8 value = 0; constexpr explicit PlaneCount(eng::u8 v) noexcept : value(v) {} };

/// Índice de plano validado contra un `PlaneCount`.
struct PlaneIndex {
	eng::u8 value = 0;
	constexpr PlaneIndex() noexcept = default;
	constexpr explicit PlaneIndex(eng::u8 v) noexcept : value(v) {}
	/// Construye validando rango; en violación dispara `illegal` (como `Span::at`).
	static constexpr PlaneIndex make(eng::u8 v, PlaneCount c) noexcept {
		if (v >= c.value) detail::typed_range_error();
		return PlaneIndex(v);
	}
	[[nodiscard]] constexpr bool valid(PlaneCount c) const noexcept { return value < c.value; }
};

struct RowBytes { eng::u16 value = 0; constexpr explicit RowBytes(eng::u16 v) noexcept : value(v) {} };
struct PlaneBytes { eng::u32 value = 0; constexpr explicit PlaneBytes(eng::u32 v) noexcept : value(v) {} };
struct PixelWidth { eng::u16 value = 0; constexpr explicit PixelWidth(eng::u16 v) noexcept : value(v) {} };
struct PixelHeight { eng::u16 value = 0; constexpr explicit PixelHeight(eng::u16 v) noexcept : value(v) {} };
struct WordCount { eng::u16 value = 0; constexpr explicit WordCount(eng::u16 v) noexcept : value(v) {} };
struct ColorCount { eng::u8 value = 0; constexpr explicit ColorCount(eng::u8 v) noexcept : value(v) {} };
struct ByteStride { eng::u32 value = 0; constexpr explicit ByteStride(eng::u32 v) noexcept : value(v) {} };

// --- Direcciones y bases (semántica distinta a propósito) --------------------

/// Base de la reserva de un bitmap (lo que va a `BPLxPT`).
struct BitmapBase { eng::u8* value = nullptr; };
/// Buffer de escritura de un bitmap (con `frontbase_offset`).
struct FrontBase { eng::u8* value = nullptr; };
/// Dirección DMA-visible (chip RAM), en formato entero.
struct ChipAddress { eng::uintptr value = 0; };

} // namespace eng
