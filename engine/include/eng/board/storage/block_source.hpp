#pragma once

/// \file block_source.hpp
/// **Fuente de bloques** del conocimiento de `eng::board`: el motor pide bloques
/// (libro de aperturas, tablas de finales, patrones) y la fuente los entrega. Es la
/// frontera que mantiene el footprint bajo: en RAM solo quedan el índice y una caché
/// pequeña; el resto vive en almacenamiento externo.
///
/// El contrato es un **concept** (`BlockSource`), no un puntero a función: cada
/// backend es un tipo con `block_size()`, `block_count()` y `fetch(id, Span<u8>)`.
/// Así no hay `void*`, ni punteros a función, ni despacho inseguro; el compilador
/// comprueba la interfaz y `BlockCache` se especializa por fuente.
///
/// Contrato de tres estados, **el mismo del streaming del engine**
/// (`eng::field::LoadResult`, ver `docs/engine/architecture/STREAMING_LOADER.md`):
///
///   | Estado    | Significado                          | Uso del motor          |
///   |-----------|--------------------------------------|------------------------|
///   | `Ready`   | el bloque se escribió en `dst`       | se usa                 |
///   | `Empty`   | el bloque no existe                  | no se reintenta        |
///   | `Pending` | lectura asíncrona aún no completada  | se reintenta más tarde |
///
/// Backends previstos (aún no implementados, pero enchufables con este contrato):
/// - **RAM** (`RamBlockSource`): bloque incbinado o precargado; ya funcional.
/// - **Disquete Amiga**: trackloader de hardware (CIA-B + Paula + Disk DMA).
/// - **PC (host)**: fichero mapeado o lectura por bloques del sistema de archivos.
///
/// Verificación: HOST-146.

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace eng::board {

/// Resultado de una lectura de bloque (espeja `eng::field::LoadResult`).
enum class BlockStatus : u8 {
	Ready = 0u,
	Empty = 1u,
	Pending = 2u,
};

/// Contrato de una fuente de bloques. Estático (templates), sin punteros.
template <class Source>
concept BlockSource = requires(const Source& source, u32 block_id, eng::Span<u8> dst) {
	{ source.block_size() };
	{ source.block_count() };
	{ source.fetch(block_id, dst) };
};

/// Fuente en RAM: los bloques son regiones contiguas de un blob de solo lectura.
/// Base funcional para tests y para el preload antes del takeover.
class RamBlockSource {
public:
	constexpr RamBlockSource() noexcept = default;
	constexpr RamBlockSource(eng::Span<const u8> data, u32 block_size) noexcept
	    : m_data(data), m_block_size(block_size),
	      m_block_count(block_size == 0u ? 0u : static_cast<u32>(data.size() / block_size)) {}

	[[nodiscard]] constexpr u32 block_size() const noexcept { return m_block_size; }
	[[nodiscard]] constexpr u32 block_count() const noexcept { return m_block_count; }

	[[nodiscard]] BlockStatus fetch(u32 block_id, eng::Span<u8> dst) const noexcept {
		if (block_id >= m_block_count) {
			return BlockStatus::Empty;
		}
		const eng::usize offset = static_cast<eng::usize>(block_id) * m_block_size;
		const eng::usize count = (dst.size() < m_block_size) ? dst.size() : m_block_size;
		for (eng::usize i = 0u; i < count; ++i) {
			dst[i] = m_data[offset + i];
		}
		return BlockStatus::Ready;
	}

private:
	eng::Span<const u8> m_data {};
	u32 m_block_size = 0u;
	u32 m_block_count = 0u;
};

/// Fuente vacía (sin backend): todo ausente. Para arrancar sin conocimiento.
class NullBlockSource {
public:
	[[nodiscard]] constexpr u32 block_size() const noexcept { return 0u; }
	[[nodiscard]] constexpr u32 block_count() const noexcept { return 0u; }
	[[nodiscard]] constexpr BlockStatus fetch(u32, eng::Span<u8>) const noexcept {
		return BlockStatus::Empty;
	}
};

static_assert(BlockSource<RamBlockSource>, "RamBlockSource cumple el contrato BlockSource");
static_assert(BlockSource<NullBlockSource>, "NullBlockSource cumple el contrato BlockSource");

} // namespace eng::board
