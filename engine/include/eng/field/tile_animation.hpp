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
/// La base conserva el primer frame horneado; una celda que entra por scroll
/// muestra ese frame hasta el siguiente cambio de grupo.
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/graphics/bob.hpp>

#include <eng/graphics/frame_plan.hpp>

namespace eng::playfield {

inline constexpr u8 kTileAnimationMaxFrames = 8u;
inline constexpr u8 kTileAnimationMaxGroups = 64u;
inline constexpr u8 kTileAnimationMaxLayers = 4u;
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

/// Reloj y dibujo de las animaciones cocinadas. No posee los bancos ni los mapas.
class TileAnimator {
public:
	constexpr TileAnimator() noexcept = default;

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
		}
		m_tick = 0u;
		m_primed = false;
	}

	[[nodiscard]] constexpr bool configured() const noexcept {
		return !m_groups.empty() && !m_layers.empty() && m_sheet != nullptr &&
		       m_masks != nullptr && m_frame_stride != 0u && m_planes != 0u;
	}
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
	}	[[nodiscard]] constexpr u32 tick() const noexcept { return m_tick; }

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
		src = eng::Span<const u16> {reinterpret_cast<const u16*>(image),
					    static_cast<usize>(m_planes) * m_tile * m_tile / 2u};
		mask = eng::Span<const u16> {reinterpret_cast<const u16*>(m), m_mask_stride / 2u};
		return true;
	}	/// Dibuja un grupo concreto (usado por el parche de animación).
	[[nodiscard]] bool draw_group(graphics::FramePlan& plan, const graphics::BobTarget& target,
				      u16 ring_height, u16 x, u16 ring_y, u16 group) const noexcept {
		if (group >= m_groups.size() || m_sheet == nullptr || m_masks == nullptr) return false;
		const TileAnimationGroupView& grp = m_groups[group];
		if (grp.frame_count == 0u) return false;
		const u8 frame = m_frame[group] < grp.frame_count ? m_frame[group] : 0u;
		const u16 frame_index = static_cast<u16>(grp.first_frame + frame);
		if (m_frame_stride == 0u || m_mask_stride == 0u) return false;
		const u8* sheet = m_sheet + static_cast<u32>(frame_index) * m_frame_stride;
		const u8* mask = m_masks + static_cast<u32>(frame_index) * m_mask_stride;
		const u16 planes = m_planes;
		const u16 tile = m_tile;
		if (planes == 0u || tile == 0u || ring_height == 0u) return false;
		u16 first_row = 0u;
		u16 remaining = tile;
		s16 ry = static_cast<s16>(ring_y);
		while (remaining > 0u) {
			const u16 seg = static_cast<u16>(static_cast<u32>(ry) + remaining > ring_height
				? ring_height - static_cast<u16>(ry) : remaining);
			for (u8 p = 0u; p < planes; ++p) {
				const u8* src = sheet + static_cast<u32>(p) * 32u + static_cast<u32>(first_row) * 2u;
				const u8* msk = mask + static_cast<u32>(first_row) * 2u;
				graphics::BlitJob& job = plan.begin_blit_job(graphics::BlitJobKind::MaskedBobCookieCut);
				job.source = graphics::BlitPtr::from_storage(reinterpret_cast<const u16*>(src));
				job.mask = graphics::BlitPtr::from_storage(reinterpret_cast<const u16*>(msk));
				job.destination = graphics::BlitPtr::from_storage(
					reinterpret_cast<u16*>(target.data() +
						(static_cast<u32>(ry) * planes + p) * target.row_bytes + (x >> 3u)));
				job.words_per_row = 1u;
				job.height = seg;
				job.source_modulo_bytes = 0;
				job.destination_modulo_bytes = eng::graphics::mod16(
					static_cast<s32>(target.row_bytes) * static_cast<s32>(target.plane_count) - 2);
				job.bitplane_count = 1u;
				job.source_shift = 0u;
				job.source_plane_stride_bytes = 0u;
				job.destination_plane_stride_bytes = 0u;
				job.minterm = 0x00cau;
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
};

} // namespace eng::playfield
