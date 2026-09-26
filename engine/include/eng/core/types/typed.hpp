#pragma once

/// \file typed.hpp
/// Fundamento del **sistema de tipos internos** (`INTERNAL_TYPE_SYSTEM.md`): vistas
/// contiguas con *tag* de dominio y unidades fuertes. El objetivo es que un error
/// de dominio no compile: un buffer de audio no puede usarse como origen de un
/// Blitter gráfico sin una conversión explícita.
///
/// Las vistas son **una sola clase** `TaggedSpan<T, Tag>` (elemento `T` — `u8`/`u16`, mutable o
/// `const` — + tag de dominio) con cuatro alias cómodos:
/// - `Bytes<Tag>` / `ByteView<Tag>`: bytes, mutable / solo lectura.
/// - `Words<Tag>` / `WordView<Tag>`: words (u16), mutable / solo lectura.
///
/// ```text
///   reserva de arena                vistas tipadas (Tag)                frontera unsafe
///   ──────────────────              ────────────────────────────       ───────────────
///   Block<Tag,Bank> (medio+dom) ──► Bytes<Tag> / ByteView<Tag> ─raw()─► u8* / u16* (backend)
///                                   Words<Tag> / WordView<Tag>          (Blitter / DMA / Copper)
///   Address<MemoryKind::Chip>: direccion DMA-visible (el rol lo da el nombre del metodo)
///   un uso de dominio cruzado (p. ej. audio como plano grafico) NO compila
///   Span<T> (SIN tag): la vista contigua corriente; TaggedSpan anade el TAG encima
/// ```
///
/// Vocabulario deliberadamente **sin escalares fuertes**: ancho, alto, `row_bytes`,
/// `plane_bytes` y número de planos van como enteros a secas (ver §3.3 de
/// `INTERNAL_TYPE_SYSTEM.md`); solo se tipan buffers, punteros, direcciones y roles.
///
/// Coste: envoltorios trivialmente copiables del mismo tamaño que `Span`; sin
/// virtuals, sin heap, `constexpr`. `raw()` es la frontera explícita hacia la capa
/// unsafe (backend). Ver reglas en `CODING_STYLE.md`.

#include <eng/core/types/memory_kind.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/type_traits.hpp>

namespace eng {

namespace detail {
/// Detiene la CPU ante un índice de plano inválido (como `Span::at`).
[[noreturn]] inline void typed_range_error() { __builtin_trap(); }

/// `U` con el `const` de `From` propagado (para `as_const`/`as_words`/`as_bytes`).
template <class From, class U>
using const_prop_t =
	eng::util::conditional_t<eng::util::detail::is_const_qualified<From>::value, const U, U>;
} // namespace detail

// Direcciones DMA-visible de chip RAM: `Address<MemoryKind::Chip>` (ver `memory_kind.hpp`). El
// **rol** (base para `BPLxPT` vs buffer de escritura) lo expresa el nombre del método
// (`Bitmap::base()`/`front()`), no un tipo aparte: el eje que cambia la corrección es el **medio**
// (Chip), y ese ya va en `Address<Chip>`.

// --- Vista contigua con TAG de dominio (UNA sola clase; 4 alias) -------------

template <class T, class Tag>
class TaggedSpan;

/// Bytes de un dominio: mutable (`Bytes`) o solo lectura (`ByteView`).
template <class Tag> using Bytes = TaggedSpan<eng::u8, Tag>;
template <class Tag> using ByteView = TaggedSpan<const eng::u8, Tag>;
/// Words (u16) de un dominio: mutable (`Words`) o solo lectura (`WordView`).
template <class Tag> using Words = TaggedSpan<eng::u16, Tag>;
template <class Tag> using WordView = TaggedSpan<const eng::u16, Tag>;

/// Vista contigua de elementos `T` (u8/u16, `T` puede ser `const`) con **tag de dominio** `Tag`.
/// Replica la ergonomía de `Span` (array deduce tamaño, iteradores, `operator[]`, `subspan`).
/// La mutabilidad va en `T` (`const`), como en `Span`; así no hacen falta `Bytes`/`ByteView`/
/// `Words`/`WordView` como clases distintas, solo como alias.
template <class T, class Tag>
class TaggedSpan {
public:
	using value_type = eng::util::remove_const_t<T>;
	using iterator = T*;
	using const_iterator = const T*;
	using size_type = eng::usize;

	constexpr TaggedSpan() noexcept = default;
	constexpr TaggedSpan(T* data, size_type count) noexcept : m_span(data, count) {}
	template <size_type N>
	constexpr TaggedSpan(T (&arr)[N]) noexcept : m_span(arr, N) {}
	static constexpr TaggedSpan from(Span<T> s) noexcept { return TaggedSpan(s.data(), s.size()); }

	/// Frontera explícita hacia la capa unsafe.
	[[nodiscard]] constexpr Span<T> raw() const noexcept { return m_span; }
	/// Dirección DMA-visible del inicio (o de `off`, que puede ser negativo). Solo para vistas de
	/// **byte**; la procedencia (Chip) la certifica quien la construye (`gfx::Bitmap`, `ChipStorage`,
	/// `Block<Tag, Chip>`), no la vista.
	[[nodiscard]] constexpr Address<eng::MemoryKind::Chip> address(eng::s32 off = 0) const noexcept
		requires (sizeof(T) == 1u) {
		return Address<eng::MemoryKind::Chip>::from_storage(m_span.data() + off);
	}
	[[nodiscard]] constexpr T* data() const noexcept { return m_span.data(); }
	[[nodiscard]] constexpr size_type size() const noexcept { return m_span.size(); }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_span.empty(); }

	[[nodiscard]] constexpr iterator begin() const noexcept { return m_span.data(); }
	[[nodiscard]] constexpr iterator end() const noexcept { return m_span.data() + m_span.size(); }
	[[nodiscard]] constexpr const_iterator cbegin() const noexcept { return m_span.data(); }
	[[nodiscard]] constexpr const_iterator cend() const noexcept { return m_span.data() + m_span.size(); }

	constexpr T& operator[](size_type i) const noexcept { return m_span[i]; }
	constexpr T& at(size_type i) const noexcept { return m_span.at(i); }
	[[nodiscard]] constexpr T& front() const noexcept { return m_span[0]; }
	[[nodiscard]] constexpr T& back() const noexcept { return m_span[m_span.size() - 1u]; }

	[[nodiscard]] constexpr TaggedSpan subspan(size_type off, size_type n) const noexcept {
		return TaggedSpan(m_span.data() + off, n);
	}
	[[nodiscard]] constexpr TaggedSpan subspan(size_type off) const noexcept {
		return TaggedSpan(m_span.data() + off, m_span.size() - off);
	}
	/// Vista de solo lectura de la misma memoria.
	[[nodiscard]] constexpr TaggedSpan<const T, Tag> as_const() const noexcept {
		return {m_span.data(), m_span.size()};
	}
	/// Reinterpretación explícita a words del MISMO dominio (requiere bytes: `sizeof(T)==1`).
	[[nodiscard]] constexpr TaggedSpan<detail::const_prop_t<T, eng::u16>, Tag> as_words() const noexcept
		requires (sizeof(T) == 1u) {
		using U = detail::const_prop_t<T, eng::u16>;
		return TaggedSpan<U, Tag>{reinterpret_cast<U*>(m_span.data()), m_span.size() / 2u};
	}
	/// Reinterpretación explícita a bytes del MISMO dominio (requiere words: `sizeof(T)==2`).
	[[nodiscard]] constexpr TaggedSpan<detail::const_prop_t<T, eng::u8>, Tag> as_bytes() const noexcept
		requires (sizeof(T) == 2u) {
		using U = detail::const_prop_t<T, eng::u8>;
		return TaggedSpan<U, Tag>{reinterpret_cast<U*>(m_span.data()), m_span.size() * 2u};
	}
	/// Rellena con `v` (solo vistas mutables).
	constexpr void fill(T v) const noexcept requires (!eng::util::detail::is_const_qualified<T>::value) {
		m_span.fill(v);
	}

private:
	Span<T> m_span {};
};

// --- Bloque tipado (resultado de una reserva) --------------------------------

/// Bloque de memoria tipado: vista `Bytes<Tag>` de la reserva **y** su medio.
///
/// `Bank` es el banco en el **tipo** (`MemoryKind`): `Block<Tag, MemoryKind::Chip>` da una
/// `Address<Chip>` (DMA), mientras que `Block<Tag>` (`Bank = Any`) lleva el medio **como dato**
/// (`kind`, el que decide el setup/arena). Un solo tipo cubre los dos casos: no hace falta un
/// `TypedBlock` aparte (es su alias). `valid()` = reserva con datos.
template <class Tag, MemoryKind Bank = MemoryKind::Any>
struct Block {
	Bytes<Tag> view {};
	MemoryKind kind = Bank;

	constexpr Block() noexcept = default;
	constexpr Block(Bytes<Tag> v, MemoryKind k = Bank) noexcept : view(v), kind(k) {}
	[[nodiscard]] constexpr bool valid() const noexcept { return !view.empty(); }
	/// Dirección tipada por el banco. `Bank == Any` = dirección sin banco (no DMA);
	/// un banco concreto la vuelve DMA-safe y no compila en APIs de otro banco.
	[[nodiscard]] constexpr Address<Bank> address() const noexcept {
		return Address<Bank>::from_storage(view.data());
	}
	[[nodiscard]] constexpr eng::u8* data() const noexcept { return view.data(); }
	[[nodiscard]] constexpr eng::usize size() const noexcept { return view.size(); }
	[[nodiscard]] constexpr Bytes<Tag>& operator*() noexcept { return view; }
	[[nodiscard]] constexpr const Bytes<Tag>& operator*() const noexcept { return view; }
	[[nodiscard]] constexpr Bytes<Tag>* operator->() noexcept { return &view; }
	[[nodiscard]] constexpr const Bytes<Tag>* operator->() const noexcept { return &view; }
};

/// Bloque de un banco **concreto** (compile-time): alias de `Block<Tag, K>`.
template <class Tag, MemoryKind K>
using TypedBlock = Block<Tag, K>;

} // namespace eng
