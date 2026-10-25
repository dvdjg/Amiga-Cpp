#pragma once

/// \file asset.hpp
/// **Vista de un `MapBundle` v1** (`eng::map`): valida y expone el mapa cocinado
/// big-endian sin poseer memoria ni parsear XML. Es la primera capa del runtime de
/// contenido Tiled: banco externo + una o más capas de celdas `u16` + paleta.
///
/// ```text
/// MapBundle (bytes, .rodata/INCBIN)
///   header (40 B) ──► MapAssetView
///   LayerDesc[]   ──► MapAssetView::Layer (cells span)
///   palette u16[] ──► palette_word()
///   cells u16[]   ──► LayerView (TileMap: width/height/wrap/empty/tile_at)
///                      │
///                      ▼
///                 XlimitedScene / ScrollEngine (sin conocer Tiled)
/// ```
///
/// El **banco de tiles** no viaja en el bundle: es un asset DMA aparte y el cooker
/// declara sus bytes en la cabecera. Esto permite tener el bundle en Fast/ROM y el
/// banco en Chip. La versión 1 es **baked**: celdas = índice de banco; el catálogo
/// `TileRef`/páginas y las capas vivas se añadirán en versiones posteriores.

#include <eng/core/math/fast_div.hpp>
#include <eng/core/types/ptr.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/field/tile_source.hpp>

namespace eng::map {

/// Modo de composición declarado por el cooker (los valores coinciden con el
/// `MapBundle` v1 y con `MapLayerMode` del documento de runtime).
enum class LayerMode : u8 {
	Baked = 0,
	Base = 1,
	Overlay = 2,
	Occluder = 3,
	Parallax = 4,
	CollisionOnly = 5,
};

inline constexpr u16 kMapBundleVersion = 1u;
inline constexpr u32 kMapBundleHeaderSize = 40u;
inline constexpr u32 kMapLayerDescSize = 32u;
inline constexpr u16 kInvalidMapLayer = 0xffffu;

[[nodiscard]] constexpr u16 read_be16(Span<const u8> bytes, u32 offset) noexcept {
	return static_cast<u16>((static_cast<u16>(bytes[offset]) << 8u) | bytes[offset + 1u]);
}
[[nodiscard]] constexpr u32 read_be32(Span<const u8> bytes, u32 offset) noexcept {
	return (static_cast<u32>(bytes[offset]) << 24u) | (static_cast<u32>(bytes[offset + 1u]) << 16u) |
	       (static_cast<u32>(bytes[offset + 2u]) << 8u) | static_cast<u32>(bytes[offset + 3u]);
}

/// Vista validada de un `MapBundle` v1. No reserva memoria y no usa heap.
class MapAssetView {
public:
	/// Una capa del bundle: metadatos + vista de sus celdas `u16` BE.
	struct Layer {
		Span<const u8> cells {};
		u16 id = 0u;
		LayerMode mode = LayerMode::Baked;
		u16 width = 0u;
		u16 height = 0u;
		s16 origin_x = 0;
		s16 origin_y = 0;
		u16 wrap_x = 0u;
		u16 wrap_y = 0u;
		u16 empty_tile = 0xffffu;

		/// Celda en coordenadas **ya resueltas** (`0 <= x < width`, `0 <= y < height`).
		[[nodiscard]] constexpr u16 cell(u16 x, u16 y) const noexcept {
			return read_be16(cells, (static_cast<u32>(y) * width + x) * 2u);
		}
	};

	constexpr MapAssetView() noexcept = default;
	/// \param bytes  bundle completo; debe sobrevivir a la vista.
	constexpr explicit MapAssetView(Span<const u8> bytes) noexcept : m_data(bytes) {}

	/// `true` si magic/version/tablas/celdas/paleta caen dentro del span.
	[[nodiscard]] constexpr bool valid() const noexcept {
		if (m_data.size() < kMapBundleHeaderSize) return false;
		if (m_data[0] != 'E' || m_data[1] != 'M' || m_data[2] != 'A' || m_data[3] != 'P') return false;
		if (read_be16(m_data, 4) != kMapBundleVersion) return false;
		const u32 layers_offset = read_be32(m_data, 24);
		const u32 palette_offset = read_be32(m_data, 28);
		if (layers_offset < kMapBundleHeaderSize || !has(layers_offset, static_cast<u32>(layer_count()) * kMapLayerDescSize)) return false;
		if (!has(palette_offset, static_cast<u32>(palette_count()) * 2u)) return false;
		for (u16 i = 0u; i < layer_count(); ++i) {
			const u32 at = layers_offset + static_cast<u32>(i) * kMapLayerDescSize;
			if (!has(read_be32(m_data, at + 20u), read_be32(m_data, at + 24u))) return false;
		}
		return true;
	}

	[[nodiscard]] constexpr u16 version() const noexcept { return read_be16(m_data, 4); }
	[[nodiscard]] constexpr u16 flags() const noexcept { return read_be16(m_data, 6); }
	[[nodiscard]] constexpr u8 tile_width() const noexcept { return m_data[8]; }
	[[nodiscard]] constexpr u8 tile_height() const noexcept { return m_data[9]; }
	[[nodiscard]] constexpr u8 planes() const noexcept { return m_data[10]; }
	[[nodiscard]] constexpr u8 chunk_log2() const noexcept { return m_data[11]; }
	[[nodiscard]] constexpr u16 layer_count() const noexcept { return read_be16(m_data, 12); }
	[[nodiscard]] constexpr u16 palette_count() const noexcept { return read_be16(m_data, 14); }
	[[nodiscard]] constexpr u16 tile_count() const noexcept { return read_be16(m_data, 16); }
	[[nodiscard]] constexpr u32 bank_bytes() const noexcept { return read_be32(m_data, 20); }

	/// Paleta en palabras Amiga `0x0RGB` (BE en el bundle).
	[[nodiscard]] constexpr u16 palette_word(u16 index) const noexcept {
		return index < palette_count() ? read_be16(m_data, read_be32(m_data, 28) + static_cast<u32>(index) * 2u) : 0u;
	}

	/// Capa `index`; si el bundle no es válido o el índice no cabe, devuelve una capa vacía.
	[[nodiscard]] constexpr Layer layer(u16 index) const noexcept {
		if (m_data.size() < kMapBundleHeaderSize || index >= layer_count()) return {};
		const u32 layers_offset = read_be32(m_data, 24);
		if (layers_offset < kMapBundleHeaderSize ||
		    !has(layers_offset, (static_cast<u32>(index) + 1u) * kMapLayerDescSize)) return {};
		const u32 at = layers_offset + static_cast<u32>(index) * kMapLayerDescSize;
		const u32 cells_offset = read_be32(m_data, at + 20u);
		const u32 cells_bytes = read_be32(m_data, at + 24u);
		Layer out {};
		out.cells = m_data.subspan(cells_offset, cells_bytes);
		out.id = read_be16(m_data, at + 0u);
		out.mode = static_cast<LayerMode>(read_be16(m_data, at + 2u));
		out.width = read_be16(m_data, at + 4u);
		out.height = read_be16(m_data, at + 6u);
		out.origin_x = static_cast<s16>(read_be16(m_data, at + 8u));
		out.origin_y = static_cast<s16>(read_be16(m_data, at + 10u));
		out.wrap_x = read_be16(m_data, at + 12u);
		out.wrap_y = read_be16(m_data, at + 14u);
		out.empty_tile = read_be16(m_data, at + 16u);
		return out;
	}

private:
	[[nodiscard]] constexpr bool has(u32 offset, u32 size) const noexcept {
		return offset <= m_data.size() && size <= m_data.size() - offset;
	}

	Span<const u8> m_data {};
};

/// Adaptador de una capa del bundle al contrato `playfield::TileMap`. No posee el
/// `MapAssetView`: este debe sobrevivir a la vista.
class LayerView {
public:
	constexpr LayerView() noexcept = default;
	/// \param view   bundle validado que contiene la capa.
	/// \param index  índice de capa (`0..view.layer_count()-1`).
	constexpr LayerView(const MapAssetView& view, u16 index) noexcept : m_view(view), m_index(index) {
		if (!view.valid() || index >= view.layer_count()) return;
		const MapAssetView::Layer layer = view.layer(index);
		width = layer.width;
		height = layer.height;
		wrap_x = layer.wrap_x;
		wrap_y = layer.wrap_y;
		edge_tile = 0u;
		empty_tile = layer.empty_tile;
	}

	/// `true` si la capa existe y el bundle es válido.
	[[nodiscard]] constexpr bool has_data() const noexcept {
		return m_view.valid() && m_view->valid() && m_index < m_view->layer_count();
	}
	/// `true` si `tile` es el centinela de vacío de esta capa.
	[[nodiscard]] constexpr bool is_empty(u16 tile) const noexcept { return tile == empty_tile; }
	/// Tile de la celda, con wrap toroidal; `edge_tile` si cae fuera de un eje acotado.
	[[nodiscard]] constexpr u16 tile_at(s32 x, s32 y) const noexcept {
		if (!has_data()) return edge_tile;
		if (wrap_x != 0u) x = eng::wrap_period(x, wrap_x);
		else if (x < 0 || x >= static_cast<s32>(width)) return edge_tile;
		if (wrap_y != 0u) y = eng::wrap_period(y, wrap_y);
		else if (y < 0 || y >= static_cast<s32>(height)) return edge_tile;
		return m_view->layer(m_index).cell(static_cast<u16>(x), static_cast<u16>(y));
	}

	// Contrato `TileMap`.
	u16 width = 0u;
	u16 height = 0u;
	u16 wrap_x = 0u;
	u16 wrap_y = 0u;
	u16 edge_tile = 0u;
	u16 empty_tile = 0xffffu;

private:
	Ref<const MapAssetView> m_view {};
	u16 m_index = 0u;
};

static_assert(playfield::TileMap<LayerView>, "LayerView cumple TileMap");

} // namespace eng::map
