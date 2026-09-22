#pragma once

/// \file world_layer.hpp
/// Puentes entre un **mundo empaquetado** (`eng::assets::WorldView`, chunk
/// `WorldMap`) y el motor de scroll (`TileSource`/`TileMap`):
///
///   - `WorldLayerSource`: una capa del `WorldView` como `TileMap` directo (mundo
///     entero residente en RAM). Sin copia de celdas.
///   - `WorldMapChunkLoader`: `Loader` de `StreamingWorldMap` que sirve cada chunk
///     desde el blob del `WorldView` (Loader-RAM). Así el scroll consume el formato
///     de mundo con acceso de solo-residentes, sin conocer el contenedor.
///
/// Formato: `docs/engine/architecture/WORLD_FORMAT.md`. Procedencia de bytes:
/// `docs/engine/architecture/STREAMING_LOADER.md`.

#include <eng/assets/uaf.hpp>
#include <eng/core/ptr.hpp>
#include <eng/field/streaming_map.hpp>
#include <eng/field/tile_source.hpp>

namespace eng::field {

/// Una capa de un `WorldView` como `TileMap` (width/height/wrap/edge + acceso).
/// Satisface el contrato que consume `XLimitedPlayfield`; el wrap/borde lo resuelve
/// el propio `WorldView::tile_at`.
class WorldLayerSource {
public:
	eng::u16 width = 0, height = 0, wrap_x = 0, wrap_y = 0, empty_tile = 0xFFFFu;

	/// Enlaza con la capa `layer` del mundo (valida índice y datos).
	bool bind(const eng::assets::WorldView& world, eng::u32 layer) {
		if (!world.valid() || layer >= world.layer_count()) return false;
		m_world = eng::Ref<const eng::assets::WorldView>(&world);
		m_layer = layer;
		width = world.layer_width(layer);
		height = world.layer_height(layer);
		wrap_x = world.layer_wrap_x(layer);
		wrap_y = world.layer_wrap_y(layer);
		empty_tile = world.layer_empty_tile(layer);
		return true;
	}

	constexpr bool has_data() const { return m_world.valid(); }
	constexpr bool is_empty(eng::u16 g) const { return !m_world.valid() || g == empty_tile; }
	eng::u16 tile_at(eng::s32 x, eng::s32 y) const {
		return m_world.valid() ? m_world->tile_at(m_layer, x, y) : empty_tile;
	}

private:
	eng::Ref<const eng::assets::WorldView> m_world {}; ///< mundo (referencia no propietaria)
	eng::u32 m_layer = 0;
};

/// Fuente de chunks (`ChunkSource`) que sirve cada chunk desde el blob de un
/// `WorldView` (Loader-RAM). `chunk_size` debe coincidir con el `ChunkSize` del
/// `StreamingWorldMap` (misma potencia de dos); si no, devuelve `Empty`.
class WorldMapChunkLoader {
public:
	const eng::assets::WorldView* world = nullptr;
	eng::u32 layer = 0;
	eng::u16 chunk_size = 16u;

	[[nodiscard]] eng::field::LoadResult load(eng::s32 cx, eng::s32 cy,
	                                          eng::TileBankBuffer dst) const {
		if (world == nullptr || dst.data() == nullptr) {
			return eng::field::LoadResult::Empty;
		}
		if (chunk_size != world->chunk_size()) return eng::field::LoadResult::Empty;
		const eng::s32 idx = world->find_chunk(layer, cx, cy);
		if (idx < 0) return eng::field::LoadResult::Empty;
		if (!world->decode_chunk(layer, static_cast<eng::u32>(idx), dst)) {
			return eng::field::LoadResult::Empty;
		}
		return eng::field::LoadResult::Ready;
	}
};

} // namespace eng::field
