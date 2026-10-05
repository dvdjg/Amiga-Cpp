#pragma once

/// \file world_tile_map.hpp
/// Adaptador de capas tilemap retenidas al contrato lógico de XLimited.
/// Adaptador tipado de `scene::TileLayer` (`TileMap16` con `PackedTileCell`) al contrato
/// `playfield::TileMap` que consume `XlimitedScene`. El adaptador solo traduce celdas empaquetadas y
/// aplica límites, wrap, tile de borde y sentinel de vacío; no posee el mapa ni duplica el scroll.

#include <eng/core/types/ptr.hpp>
#include <eng/core/types/types.hpp>
#include <eng/field/playfield.hpp> // `eng::playfield` (nombre público del motor de playfields)
#include <eng/field/xlimited_scene.hpp>
#include <eng/scene/world.hpp>
#include <eng/scene/virtual_scene.hpp>

namespace eng::scene {

/// Vista `TileMap` sobre la capa retenida de World. `wrap_x/y == 0` significa mundo acotado.
class WorldTileMapView {
public:
	constexpr WorldTileMapView() noexcept = default;
	explicit WorldTileMapView(const Layer& layer, u16 wrap_x = 0u,
					  u16 wrap_y = 0u, u16 edge_tile = 0u,
					  u16 empty_tile = 0xffffu) noexcept
		: width(layer.is_tilemap() && layer.tilemap().map.valid()
				   ? layer.tilemap().map->width() : 0u),
		  height(layer.is_tilemap() && layer.tilemap().map.valid()
				 ? layer.tilemap().map->height() : 0u),
		  wrap_x(wrap_x), wrap_y(wrap_y), edge_tile(edge_tile), empty_tile(empty_tile), m_layer(layer) {}

	[[nodiscard]] constexpr bool has_data() const noexcept {
		return m_layer.valid() && m_layer->is_tilemap() && m_layer->tilemap().ok();
	}
	[[nodiscard]] constexpr bool is_empty(u16 tile) const noexcept { return tile == empty_tile; }
	[[nodiscard]] u16 tile_at(s32 x, s32 y) const noexcept {
		if (!has_data()) return edge_tile;
		if (wrap_x != 0u) x = playfield::wrap_period(x, wrap_x);
		else if (x < 0 || x >= width) return edge_tile;
		if (wrap_y != 0u) y = playfield::wrap_period(y, wrap_y);
		else if (y < 0 || y >= height) return edge_tile;
		return m_layer->tilemap().map->cell(static_cast<u16>(x), static_cast<u16>(y)).tile_index();
	}

public:
	u16 width = 0u;
	u16 height = 0u;
	u16 wrap_x = 0u;
	u16 wrap_y = 0u;
	u16 edge_tile = 0u;
	u16 empty_tile = 0xffffu;

private:
	Ref<const Layer> m_layer {};
};

static_assert(playfield::TileMap<WorldTileMapView>, "WorldTileMapView cumple TileMap");

/// La misma vista puede parametrizar el driver de escena XLimited sin un mapa paralelo.
using WorldXlimitedSceneConfig = playfield::XlimitedSceneConfigT<WorldTileMapView>;
static_assert(playfield::TileMap<decltype(WorldXlimitedSceneConfig::map)>,
	      "WorldXlimitedSceneConfig conserva el contrato TileMap");

} // namespace eng::scene
