#pragma once

/// \file tile_animation.hpp
/// **Animación de tiles cocinada** para capas vivas de un campo X-Limited.
///
/// El cooker emite grupos (frames contiguos en un banco planar + máscaras 1bpp),
/// mapas dispersos de celdas (`group + 1`, 0 = vacío) y duraciones en ticks de
/// juego. El runtime no parsea Tiled ni cuantiza: resuelve `frame(group, tick)` y
/// emite un blit enmascarado planar por plano en el mismo destino físico que el
/// bloque base. Los frames se empaquetan planar sin guarda porque las celdas están
/// alineadas a tile; no hay subpíxel horizontal.
///
/// `XLimitedPlayfield::patch_animations` redibuja solo celdas visibles de grupos
/// que cambiaron de frame, repartidas en 6 fases para no desbordar el FramePlan.
/// El bake omite las celdas de capas vivas; la composición inicial pinta su
/// frame actual y el parche restaura el bloque base de la capa antes del overlay.
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/graphics/bob.hpp>

#include <eng/graphics/frame_plan.hpp>

namespace eng::playfield {

inline constexpr u8 kTileAnimationMaxFrames = 8u;
inline constexpr u8 kTileAnimationMaxGroups = 255u;
inline constexpr u8 kTileAnimationMaxLayers = 8u;
inline constexpr u16 kTileAnimationMaxCells = 256u;
inline constexpr u8 kTileAnimationFlagMasked = 1u;

/// Un grupo de animación: frames contiguos en `sheet`/`mask` y duración por
/// frame en ticks de juego (50 Hz). `frame_count` <= `kTileAnimationMaxFrames`.
struct TileAnimationGroupView {
	u16 first_frame = 0u;
	u8 frame_count = 0u;
	u8 flags = 0u;
	u16 first_cell = 0u;
	u16 cell_count = 0u;
	u8 durations[kTileAnimationMaxFrames] {};
};

/// Celda animada dispersa: coordenada de mapa y capa (índice en `layers`).
struct TileAnimationCellView {
	u16 x = 0u;
	u16 y = 0u;
	u8 layer = 0u;
	u8 pad = 0u;
};

/// Capa de overlay animada: celdas `group + 1` (0 = vacío). `wrap_x/y` en
/// celdas; 0 = borde acotado (se usa clamp al dibujar).
struct TileAnimationLayerView {
	const u16* cells = nullptr;
	u16 width = 0u;
	u16 height = 0u;
	u16 wrap_x = 0u;
	u16 wrap_y = 0u;
};

/// Resultado de un parche de animación, para telemetría y diagnóstico de carga.
///
/// El parche recorre los grupos, descarta los que no cambian o no tocan en este
/// turno de paridad y puede dejar grupos pendientes si no caben en el
/// `FramePlan`. Estos contadores permiten distinguir trabajo útil de barrido
/// redundante sin instrumentar el interior del motor:
///
/// - `groups_scanned`: grupos visitados por el bucle.
/// - `groups_patched`: grupos con al menos una celda visible repintada.
/// - `groups_deferred`: grupos que tocaban pero no cupieron en el plan.
/// - `cells_patched`: celdas visibles repintadas.
/// - `cells_culled`: celdas descartadas por quedar fuera de la ventana.
/// - `base_restores`: bloques base repintados bajo overlays transparentes.
struct TilePatchStats {
	u16 groups_scanned = 0u;
	u16 groups_patched = 0u;
	u16 groups_deferred = 0u;
	u16 cells_patched = 0u;
	u16 cells_culled = 0u;
	u16 base_restores = 0u;
	u16 cells_patched_by_layer[kTileAnimationMaxLayers] {};
	u16 base_restores_by_layer[kTileAnimationMaxLayers] {};
};

/// Reloj y dibujo de las animaciones cocinadas. No posee los bancos ni los mapas.
class TileAnimator {
public:
	constexpr TileAnimator() noexcept = default;

	/// Configura el animador para `planes` planos y tiles cuadrados de `tile`
	/// píxeles. `tile` debe ser múltiplo de 16 (una palabra Amiga por fila);
	/// `planes` puede ser 1..6 y no se fija a los 6 de la demo 207. El ancho
	/// de frame, máscara y plano se valida en `configured()`.
	void configure(eng::Span<const TileAnimationGroupView> groups,
		       eng::Span<const TileAnimationLayerView> layers,
		       eng::Span<const TileAnimationCellView> cells,
		       const u8* sheet, const u8* masks, u16 frame_stride, u16 mask_stride,
		       u8 planes, u16 tile = 16u) noexcept {
		m_groups = groups;
		m_layers = layers;
		m_cells = cells;
		m_sheet = sheet;
		m_masks = masks;
		m_frame_stride = frame_stride;
		m_mask_stride = mask_stride;
		m_planes = planes;
		m_tile = tile;
		for (u8 i = 0u; i < kTileAnimationMaxGroups; ++i) {
			m_frame[i] = 0u;
			m_changed[i] = 0u;
			m_group_layer[i] = 0u;
		}
		for (u8 i = 0u; i < kTileAnimationMaxLayers; ++i) m_layer_enabled[i] = 1u;
		for (u16 g = 0u; g < groups.size() && g < kTileAnimationMaxGroups; ++g) {
			const TileAnimationGroupView& grp = groups[g];
			m_group_layer[g] = (cells.empty() || grp.first_cell >= cells.size())
				? 0u : cells[grp.first_cell].layer;
		}
		m_tick = 0u;
		m_primed = false;
	}

	[[nodiscard]] constexpr bool configured() const noexcept {
		if (m_groups.empty() || m_layers.empty() || m_sheet == nullptr ||
		    m_masks == nullptr || m_frame_stride == 0u || m_planes == 0u ||
		    m_tile < 16u || (m_tile & 15u) != 0u) {
			return false;
		}
		// Cada fila de un plano ocupa palabras completas; el frame apila
		// `m_planes` planos de `m_tile` filas y la máscara tiene una fila por
		// fila de tile. Validarlo aquí evita leer fuera del bake.
		const u16 words_per_row = static_cast<u16>((m_tile + 15u) / 16u);
		const u32 source_plane_stride = static_cast<u32>(words_per_row) * 2u * m_tile;
		return m_frame_stride >= source_plane_stride * m_planes &&
		       m_mask_stride >= static_cast<u32>(words_per_row) * 2u * m_tile;
	}
	/// Número de planos del frame; también es el número de jobs por celda en
	/// `draw_group` cuando no hay cruce vertical.
	[[nodiscard]] constexpr u8 plane_count() const noexcept { return m_planes; }
	/// Lado del tile en píxeles; debe ser múltiplo de 16 para que los frames
	/// planares queden alineados a palabra.
	[[nodiscard]] constexpr u16 tile() const noexcept { return m_tile; }
	[[nodiscard]] constexpr u16 group_count() const noexcept {
		return static_cast<u16>(m_groups.size());
	}
	[[nodiscard]] constexpr u16 layer_count() const noexcept {
		return static_cast<u16>(m_layers.size());
	}
	[[nodiscard]] constexpr const TileAnimationLayerView& layer(u16 index) const noexcept {
		return m_layers[index];
	}
	[[nodiscard]] constexpr const TileAnimationGroupView& group(u16 index) const noexcept {
		return m_groups[index];
	}
	[[nodiscard]] constexpr const TileAnimationCellView& cell(u16 index) const noexcept {
		return m_cells[index];
	}
	[[nodiscard]] constexpr u8 frame_of(u16 group) const noexcept {
		return group < m_groups.size() ? m_frame[group] : 0u;
	}
	[[nodiscard]] constexpr bool any_changed() const noexcept { return m_any_changed; }
	[[nodiscard]] constexpr u16 changed_count() const noexcept { return m_changed_count; }
	[[nodiscard]] constexpr bool group_changed(u16 group) const noexcept {
		return group < m_groups.size() && m_changed[group] != 0u;
	}
	void clear_group(u16 g) const noexcept {
		if (g < kTileAnimationMaxGroups) m_changed[g] = 0u;
		bool any = false;
		for (u16 i = 0u; i < m_groups.size() && i < kTileAnimationMaxGroups; ++i) any = any || m_changed[i] != 0u;
		m_any_changed = any;
		if (!any) m_changed_count = 0u;
	}
	void clear_changes() const noexcept {
		m_any_changed = false;
		m_changed_count = 0u;
		for (u8 i = 0u; i < m_groups.size() && i < kTileAnimationMaxGroups; ++i) m_changed[i] = 0u;
	}
	/// Fija el tick absoluto de juego y recalcula el frame de cada grupo.
	void set_tick(u32 tick) noexcept {
		if (m_primed && tick == m_tick) return;
		if (!m_primed) {
			for (u16 g = 0u; g < m_groups.size() && g < kTileAnimationMaxGroups; ++g) {
				m_frame[g] = 0u;
				m_acc[g] = 0u;
			}
			m_primed = true;
			m_tick = tick;
			m_any_changed = false;
			m_changed_count = 0u;
			return;
		}
		// Camino caliente: el tick avanza de uno en uno; sin `%` por grupo.
		if (tick == m_tick + 1u) {
			for (u16 g = 0u; g < m_groups.size() && g < kTileAnimationMaxGroups; ++g) {
				const TileAnimationGroupView& grp = m_groups[g];
				if (grp.frame_count <= 1u) continue;
				const u8 d = grp.durations[m_frame[g]] != 0u ? grp.durations[m_frame[g]] : 1u;
				if (++m_acc[g] >= d) {
					m_acc[g] = 0u;
					m_frame[g] = static_cast<u8>(m_frame[g] + 1u);
					if (m_frame[g] >= grp.frame_count) m_frame[g] = 0u;
					m_changed[g] = 1u;
					m_any_changed = true;
					++m_changed_count;
				}
			}
			m_tick = tick;
			return;
		}
		// Salto de reloj (reset/carga): resolver por grupo, no es el camino por frame.
		m_any_changed = false;
		for (u16 g = 0u; g < m_groups.size() && g < kTileAnimationMaxGroups; ++g) {
			const TileAnimationGroupView& grp = m_groups[g];
			if (grp.frame_count == 0u) continue;
			u32 period = 0u;
			for (u8 f = 0u; f < grp.frame_count; ++f) period += grp.durations[f] != 0u ? grp.durations[f] : 1u;
			u32 local = period != 0u ? tick % period : 0u;
			u8 frame = 0u;
			while (frame + 1u < grp.frame_count) {
				const u8 d = grp.durations[frame] != 0u ? grp.durations[frame] : 1u;
				if (local < d) break;
				local -= d;
				++frame;
			}
			m_changed[g] = frame != m_frame[g] ? 1u : 0u;
			if (m_changed[g]) { m_any_changed = true; ++m_changed_count; }
			m_frame[g] = frame;
			m_acc[g] = static_cast<u8>(local);
		}
		m_tick = tick;
	}
	[[nodiscard]] constexpr u32 tick() const noexcept { return m_tick; }

	void set_layer_animation_enabled(u8 layer, bool enabled) noexcept {

		if (layer < kTileAnimationMaxLayers) m_layer_enabled[layer] = enabled ? 1u : 0u;

	}

	[[nodiscard]] constexpr bool layer_animation_enabled(u8 layer) const noexcept {

		return layer < kTileAnimationMaxLayers && m_layer_enabled[layer] != 0u;

	}
	[[nodiscard]] constexpr u16 cell_count() const noexcept { return static_cast<u16>(m_cells.size()); }

	/// Filtro de diagnóstico: si `cell` está en rango, solo se pinta/parchea
	/// esa celda. `0xffff` desactiva el filtro.
	void set_test_cell(u16 cell) noexcept { m_test_cell = cell; }
	[[nodiscard]] constexpr bool test_cell_allowed(u16 cell) const noexcept {
		return m_test_cell == 0xffffu || cell == m_test_cell;
	}

	/// Dibuja todas las capas para la celda de mapa `(mapx,mapy)` en el destino
	/// físico `(x, ring_y)` (x en píxeles, y en filas del anillo).
	[[nodiscard]] bool draw_block(graphics::FramePlan& plan, const graphics::BobTarget& target,
				      u16 ring_height, u16 x, u16 ring_y,
				      u16 mapx, u16 mapy) const noexcept {
		for (u16 l = 0u; l < m_layers.size(); ++l) {
			const u16 group = cell_group(m_layers[l], mapx, mapy);
			if (group == 0u) continue;
			if (!draw_group(plan, target, ring_height, x, ring_y,
					static_cast<u16>(group - 1u))) return false;
		}
		return true;
	}
	/// Fuente planar y máscara del frame actual de un grupo (para el parche).
	[[nodiscard]] bool frame_data(u16 group, eng::Span<const u16>& src,
				    eng::Span<const u16>& mask) const noexcept {
		if (group >= m_groups.size() || m_sheet == nullptr || m_masks == nullptr) return false;
		const TileAnimationGroupView& grp = m_groups[group];
		if (grp.frame_count == 0u) return false;
		const u8 frame = m_frame[group] < grp.frame_count ? m_frame[group] : 0u;
		const u32 idx = static_cast<u32>(grp.first_frame + frame);
		const u8* image = m_sheet + idx * m_frame_stride;
		const u8* m = m_masks + idx * m_mask_stride;
		const u16 words_per_row = static_cast<u16>((m_tile + 15u) / 16u);
		src = eng::Span<const u16> {reinterpret_cast<const u16*>(image),
					    static_cast<usize>(m_planes) * words_per_row * m_tile};
		mask = eng::Span<const u16> {reinterpret_cast<const u16*>(m), m_mask_stride / 2u};
		return true;
	}	/// Dibuja un grupo concreto (usado por el parche de animación).
	[[nodiscard]] bool draw_group(graphics::FramePlan& plan, const graphics::BobTarget& target,
				      u16 ring_height, u16 x, u16 ring_y, u16 group, bool opaque = false) const noexcept {
		if (group >= m_groups.size() || m_sheet == nullptr || m_masks == nullptr) return false;
		if (!layer_animation_enabled(m_group_layer[group])) return true;
		const TileAnimationGroupView& grp = m_groups[group];
		if (grp.frame_count == 0u) return false;
		const u8 frame = m_frame[group] < grp.frame_count ? m_frame[group] : 0u;
		const u16 frame_index = static_cast<u16>(grp.first_frame + frame);
		if (m_frame_stride == 0u || m_mask_stride == 0u) return false;
		const u8* sheet = m_sheet + static_cast<u32>(frame_index) * m_frame_stride;
		const u8* mask = m_masks + static_cast<u32>(frame_index) * m_mask_stride;
		const u16 planes = m_planes;
		const u16 tile = m_tile;
		const u16 words_per_row = static_cast<u16>((tile + 15u) / 16u);
		const u16 source_row_bytes = static_cast<u16>(words_per_row * 2u);
		const u32 source_plane_stride = static_cast<u32>(source_row_bytes) * tile;
		if (planes == 0u || tile < 16u || (tile & 15u) != 0u ||
		    ring_height == 0u) {
			return false;
		}
		u16 first_row = 0u;
		u16 remaining = tile;
		s16 ry = static_cast<s16>(ring_y);
		while (remaining > 0u) {
			const u16 seg = static_cast<u16>(static_cast<u32>(ry) + remaining > ring_height
				? ring_height - static_cast<u16>(ry) : remaining);
			for (u8 p = 0u; p < planes; ++p) {
				const u8* src = sheet + static_cast<u32>(p) * source_plane_stride +
						static_cast<u32>(first_row) * source_row_bytes;
				const u8* msk = mask + static_cast<u32>(first_row) * source_row_bytes;
				graphics::BlitJob& job = plan.begin_blit_job(opaque ? graphics::BlitJobKind::CopyRect : graphics::BlitJobKind::MaskedBobCookieCut);
				job.source = graphics::BlitPtr::from_storage(reinterpret_cast<const u16*>(src));
				job.mask = graphics::BlitPtr::from_storage(opaque ? nullptr : reinterpret_cast<const u16*>(msk));
				job.destination = graphics::BlitPtr::from_storage(
					reinterpret_cast<u16*>(target.data() +
						(static_cast<u32>(ry) * planes + p) * target.row_bytes + (x >> 3u)));
				job.words_per_row = words_per_row;
				job.height = seg;
				job.source_modulo_bytes = 0;
				job.destination_modulo_bytes = eng::graphics::mod16(
					static_cast<s32>(target.row_bytes) * static_cast<s32>(target.plane_count) -
					static_cast<s32>(words_per_row) * 2);
				job.bitplane_count = 1u;
				job.source_shift = 0u;
				job.source_plane_stride_bytes = source_plane_stride;
				job.destination_plane_stride_bytes = static_cast<u32>(target.row_bytes) * static_cast<u32>(target.plane_count);
				job.minterm = opaque ? 0x00f0u : 0x00cau;
				job.interleaved = false;
				job.source_words_per_row = 0u;
				job.descending = false;
				if (!plan.commit_blit_job()) return false;
			}
			remaining = static_cast<u16>(remaining - seg);
			first_row = static_cast<u16>(first_row + seg);
			ry = 0;
		}		return true;
	}

	/// Dibuja solo la capa `layer_index` de una celda (para `TileLayerStack`).
	[[nodiscard]] bool draw_layer_block(graphics::FramePlan& plan,
					    const graphics::BobTarget& target,
					    u16 ring_height, u16 x, u16 ring_y,
					    u16 mapx, u16 mapy, u16 layer_index,
					    bool opaque = false) const noexcept {
		if (layer_index >= m_layers.size()) return false;
		const u16 group = cell_group(m_layers[layer_index], mapx, mapy);
		if (group == 0u) return true;
		return draw_group(plan, target, ring_height, x, ring_y,
				  static_cast<u16>(group - 1u), opaque);
	}

	/// Grupo de una celda (`0` = vacío), con wrap o clamp según la capa.
	[[nodiscard]] static u16 cell_group(const TileAnimationLayerView& layer,
					    s32 x, s32 y) noexcept {
		if (layer.cells == nullptr || layer.width == 0u || layer.height == 0u) return 0u;
		if (layer.wrap_x != 0u) {
			const s32 w = static_cast<s32>(layer.wrap_x);
			while (x >= w) x -= w;
			while (x < 0) x += w;
		} else if (x < 0 || x >= static_cast<s32>(layer.width)) return 0u;
		if (layer.wrap_y != 0u) {
			const s32 h = static_cast<s32>(layer.wrap_y);
			while (y >= h) y -= h;
			while (y < 0) y += h;
		} else if (y < 0 || y >= static_cast<s32>(layer.height)) return 0u;
		return layer.cells[static_cast<u32>(y) * layer.width + static_cast<u32>(x)];
	}

private:
	eng::Span<const TileAnimationGroupView> m_groups {};
	eng::Span<const TileAnimationLayerView> m_layers {};
	eng::Span<const TileAnimationCellView> m_cells {};
	const u8* m_sheet = nullptr;
	const u8* m_masks = nullptr;
	u16 m_frame_stride = 0u;
	u16 m_mask_stride = 0u;
	u8 m_planes = 0u;
	u16 m_tile = 16u;
	u32 m_tick = 0u;
	bool m_primed = false;
	u8 m_frame[kTileAnimationMaxGroups] {};
	u8 m_acc[kTileAnimationMaxGroups] {};
	mutable u8 m_changed[kTileAnimationMaxGroups] {};
	mutable bool m_any_changed = false;
	mutable u16 m_changed_count = 0u;
	u8 m_group_layer[kTileAnimationMaxGroups] {};
	u8 m_layer_enabled[kTileAnimationMaxLayers] {};
	u16 m_test_cell = 0xffffu;
};

} // namespace eng::playfield
