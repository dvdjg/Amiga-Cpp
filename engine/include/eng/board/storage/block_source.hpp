#pragma once

/// \file block_source.hpp
/// **Fuente de bloques** del conocimiento de `eng::board`: el motor pide bloques
/// (libro de aperturas, tablas de finales, patrones) por identificador y la fuente
/// los entrega. Es la frontera que mantiene el footprint bajo: en RAM solo quedan
/// el índice y una caché pequeña; el resto vive en almacenamiento externo.
///
/// Contrato de tres estados, **el mismo del streaming del engine**
/// (`eng::field::LoadResult`, ver `docs/engine/architecture/STREAMING_LOADER.md`):
///
///   | Estado    | Significado                         | Uso del motor          |
///   |-----------|-------------------------------------|------------------------|
///   | `Ready`   | el bloque se escribió en `dst`      | se usa                 |
///   | `Empty`   | el bloque no existe                 | no se reintenta        |
///   | `Pending` | lectura asíncrona aún no completada | se reintenta más tarde |
///
/// Backends previstos (aún no implementados, pero enchufables con este contrato):
/// - **RAM** (`RamBlockSource`): bloque incbinado o precargado; ya funcional.
/// - **Disquete Amiga**: trackloader de hardware (CIA-B + Paula + Disk DMA), que es
///   la única vía compatible con el display tomado; ver `STREAMING_LOADER.md` §5.
/// - **PC (host)**: fichero mapeado o lectura por bloques del sistema de archivos.
///
/// Verificación: HOST-146.

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>

namespace eng::board {

/// Resultado de una lectura de bloque (espeja `eng::field::LoadResult`).
enum class BlockStatus : u8 {
	Ready = 0u,
	Empty = 1u,
	Pending = 2u,
};

/// Lector de un bloque. Debe escribir hasta `dst.size()` bytes y devolver el estado.
using BlockReader = BlockStatus (*)(void* user, u32 block_id, eng::Span<u8> dst);

/// Descriptor de una fuente de bloques: lector + contexto + geometría.
struct BlockSource {
	BlockReader read = nullptr;
	void* user = nullptr;
	u32 block_size = 0u;
	u32 block_count = 0u;

	[[nodiscard]] bool valid() const noexcept { return read != nullptr; }

	[[nodiscard]] BlockStatus fetch(u32 block_id, eng::Span<u8> dst) const noexcept {
		if (read == nullptr || block_id >= block_count) {
			return BlockStatus::Empty;
		}
		return read(user, block_id, dst);
	}
};

/// Fuente en RAM: los bloques son regiones contiguas de un blob de solo lectura.
/// Es la base funcional para tests y para el preload antes del takeover.
class RamBlockSource {
public:
	RamBlockSource(eng::Span<const u8> data, u32 block_size) noexcept
	    : m_data(data), m_block_size(block_size), m_block_count(block_size == 0u
	                                                                   ? 0u
	                                                                   : static_cast<u32>(
	                                                                         data.size() /
	                                                                         block_size)) {}

	[[nodiscard]] u32 block_count() const noexcept { return m_block_count; }
	[[nodiscard]] u32 block_size() const noexcept { return m_block_size; }

	/// Descriptor listo para `BlockCache`/`knowledge`.
	[[nodiscard]] BlockSource source() noexcept {
		BlockSource s;
		s.read = &RamBlockSource::read_impl;
		s.user = this;
		s.block_size = m_block_size;
		s.block_count = m_block_count;
		return s;
	}

private:
	static BlockStatus read_impl(void* user, u32 block_id, eng::Span<u8> dst) noexcept {
		RamBlockSource* self = static_cast<RamBlockSource*>(user);
		if (block_id >= self->m_block_count) {
			return BlockStatus::Empty;
		}
		const u8* begin = self->m_data.data() + static_cast<eng::usize>(block_id) * self->m_block_size;
		for (u32 i = 0u; i < dst.size(); ++i) {
			dst[i] = begin[i];
		}
		return BlockStatus::Ready;
	}

	eng::Span<const u8> m_data;
	u32 m_block_size = 0u;
	u32 m_block_count = 0u;
};

} // namespace eng::board
