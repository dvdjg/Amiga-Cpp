#pragma once

/// \file tile_animation.hpp
/// **Animación de tiles cocinada** para capas vivas de un campo X-Limited.
///
/// El cooker emite grupos (frames contiguos en un banco planar + máscaras 1bpp),
/// mapas dispersos de celdas (`group + 1`, 0 = vacío) y duraciones en ticks de
/// juego. El runtime no parsea Tiled ni cuantiza: resuelve `frame(group, tick)` y
/// emite un blit enmascarado por segmento con todos los planos en el mismo destino
/// físico que el bloque base. Los frames se empaquetan planar sin guarda porque las
/// celdas están alineadas a tile; no hay subpíxel horizontal.
///
/// `XLimitedPlayfield::patch_animations` redibuja solo celdas visibles de grupos
/// que cambiaron de frame, por capas (primer plano primero) y con cursor
/// round-robin, con un tope de jobs para no desbordar el FramePlan.
/// El bake omite las celdas de capas vivas; la composición inicial pinta su
/// frame actual y el parche restaura el bloque base de la capa antes del overlay.
#include <eng/core/types/domains.hpp>
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
	// Grupos de un solo sprite (fusión de celdas contiguas): tamaño del frame en
	// píxeles (0 = lado del tile del animador), y layout de sus frames en la hoja.
	// Si `frame_stride`/`mask_stride` son 0, el grupo usa el stride global y
	// `first_frame`; si no, `sheet_offset`/`mask_offset` son el byte del primer
	// frame y el stride es el de este grupo (frames más grandes que un tile).
	u16 pixel_width = 0u;  ///< ancho del grupo en px (0 = tile del animador)
	u16 pixel_height = 0u; ///< alto del grupo en px (0 = tile del animador)
	u32 sheet_offset = 0u; ///< byte del primer frame en `sheet` (si `frame_stride` != 0)
	u32 frame_stride = 0u; ///< bytes entre frames del grupo (0 = stride global)
	u32 mask_offset = 0u;  ///< byte de la primera máscara (si `mask_stride` != 0)
	u32 mask_stride = 0u;  ///< bytes entre máscaras del grupo (0 = stride global)
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
/// turno y puede dejar grupos pendientes si no caben en el `FramePlan`. Estos
/// contadores permiten distinguir trabajo útil de barrido redundante sin
/// instrumentar el interior del motor:
///
/// - `groups_scanned`: grupos visitados por el bucle.
/// - `groups_patched`: grupos con al menos una celda visible repintada.
/// - `groups_deferred`: grupos que tocaban pero no cupieron en el plan.
/// - `cells_patched`: celdas visibles repintadas.
/// - `cells_culled`: celdas descartadas por quedar fuera de la ventana (también las que no caben en el save-under).
/// - `base_restores`: celdas cuyo fondo real se restauró con save-under (no se repinta el bake); `base_restores_by_layer` lo desglosa.
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
	/// \param groups grupos de animación (frames/duración/celdas).
	/// \param layers capas de overlay (mapas de celdas).
	/// \param cells celdas animadas dispersas (mapa/capa).
	/// \param sheet banco planar de frames en **Chip** (`ChipView<PatternTag>`).
	/// \param masks banco 1bpp de máscaras en **Chip** (`ChipView<MaskTag>`).
	/// \param frame_stride bytes entre frames consecutivos de `sheet`.
	/// \param mask_stride bytes entre máscaras consecutivas de `masks`.
	/// \param planes planos por frame (1..6).
	/// \param tile lado del tile en píxeles (múltiplo de 16).
	void configure(eng::Span<const TileAnimationGroupView> groups,
		       eng::Span<const TileAnimationLayerView> layers,
		       eng::Span<const TileAnimationCellView> cells,
		       eng::ChipView<eng::PatternTag> sheet,
		       eng::ChipView<eng::MaskTag> masks,
		       u16 frame_stride, u16 mask_stride,
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
		if (m_groups.empty() || m_layers.empty() || m_sheet.empty() ||
		    m_masks.empty() || m_frame_stride == 0u || m_planes == 0u ||
		    m_tile < 16u || (m_tile & 15u) != 0u) {
			return false;
		}
		// Cada fila de un plano ocupa palabras completas; el frame apila
		// `m_planes` planos de `m_tile` filas y la máscara tiene una fila por
		// fila de tile. Un grupo fusionado puede ser mayor: se valida con su
		// tamaño y su layout propios. Validarlo aquí evita leer fuera del bake.
		const u16 tile_words = static_cast<u16>((m_tile + 15u) / 16u);
		const u32 tile_plane_stride = static_cast<u32>(tile_words) * 2u * m_tile;
		if (m_frame_stride < tile_plane_stride * m_planes ||
		    m_mask_stride < static_cast<u32>(tile_words) * 2u * m_tile ||
		    m_sheet.size() < m_frame_stride || m_masks.size() < m_mask_stride) {
			return false;
		}
		for (const TileAnimationGroupView& grp : m_groups) {
			const u16 gw = grp.pixel_width != 0u ? grp.pixel_width : m_tile;
			const u16 gh = grp.pixel_height != 0u ? grp.pixel_height : m_tile;
			if (gw < 16u || (gw & 15u) != 0u || gh == 0u) return false;
			const u16 words = static_cast<u16>(gw / 16u);
			const u32 row_bytes = static_cast<u32>(words) * 2u;
			const u32 frame_bytes = row_bytes * gh * m_planes;
			const u32 mask_bytes = row_bytes * gh;
			const u32 frame_stride = grp.frame_stride != 0u ? grp.frame_stride : m_frame_stride;
			const u32 mask_stride = grp.mask_stride != 0u ? grp.mask_stride : m_mask_stride;
			if (frame_stride < frame_bytes || mask_stride < mask_bytes) return false;
			if (grp.frame_stride != 0u && grp.frame_count != 0u) {
				const u32 last = grp.sheet_offset +
					static_cast<u32>(grp.frame_count - 1u) * frame_stride;
				if (last + frame_bytes > m_sheet.size()) return false;
			}
			if (grp.mask_stride != 0u && grp.frame_count != 0u) {
				const u32 last = grp.mask_offset +
					static_cast<u32>(grp.frame_count - 1u) * mask_stride;
				if (last + mask_bytes > m_masks.size()) return false;
			}
		}
		return true;
	}
	/// Número de planos del frame. El dibujo agrupa todos los planos en un único
	/// job por segmento (`bitplane_count = plane_count`); el valor valida la
	/// hoja planar y la máscara del bake.
	[[nodiscard]] constexpr u8 plane_count() const noexcept { return m_planes; }
	/// Lado del tile en píxeles; debe ser múltiplo de 16 para que los frames
	/// planares queden alineados a palabra.
	[[nodiscard]] constexpr u16 tile() const noexcept { return m_tile; }
	/// Ancho efectivo del grupo `group` en píxeles (tile si no declara tamaño).
	[[nodiscard]] constexpr u16 group_pixel_width(u16 group) const noexcept {
		return group < m_groups.size() && m_groups[group].pixel_width != 0u
			? m_groups[group].pixel_width : m_tile;
	}
	/// Alto efectivo del grupo `group` en píxeles (tile si no declara tamaño).
	[[nodiscard]] constexpr u16 group_pixel_height(u16 group) const noexcept {
		return group < m_groups.size() && m_groups[group].pixel_height != 0u
			? m_groups[group].pixel_height : m_tile;
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
	/// Capa (índice de `layers`) a la que pertenece un grupo: la de su primera
	/// celda. El parche ordena por capa para que un objeto compuesto (varios
	/// grupos de una celda, p. ej. la fogata) se actualice antes de que las
	/// capas de tiles consuman el presupuesto del `FramePlan`.
	[[nodiscard]] constexpr u8 group_layer(u16 index) const noexcept {
		return index < m_groups.size() ? m_group_layer[index] : 0u;
	}
	[[nodiscard]] constexpr const TileAnimationCellView& cell(u16 index) const noexcept {
		return m_cells[index];
	}
	/// Grupo que posee la celda `index` (0xffff si la celda/layer no existe).
	/// Se resuelve con el mapa de capas del bake, no con un campo duplicado.
	[[nodiscard]] u16 group_for_cell(u16 index) const noexcept {
		if (index >= m_cells.size()) return 0xffffu;
		const TileAnimationCellView& c = m_cells[index];
		if (c.layer >= m_layers.size()) return 0xffffu;
		const u16 group_plus_one = cell_group(m_layers[c.layer], c.x, c.y);
		return group_plus_one == 0u ? 0xffffu : static_cast<u16>(group_plus_one - 1u);
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
	/// \param plan plan del frame donde se encolan los blits.
	/// \param target bitmap destino (anillo/interleaved).
	/// \param ring_height alto del anillo en filas (0 no se usa aquí).
	/// \param x columna de mundo en píxeles (destino físico).
	/// \param ring_y fila del anillo del borde superior (px).
	/// \param mapx columna lógica del mapa (índice de celda).
	/// \param mapy fila lógica del mapa (índice de celda).
	/// \return `false` si un grupo no se pudo encolar.
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
	/// Dibuja un grupo concreto (usado por el parche de animación).
	/// \param plan plan del frame donde se encolan los blits.
	/// \param target bitmap destino (anillo/interleaved).
	/// \param ring_height alto del anillo en filas; parte el tile en la costura.
	/// \param x columna de mundo en píxeles (destino físico; múltiplo de 16).
	/// \param ring_y fila del anillo del borde superior (px).
	/// \param group índice de grupo en el bake (0..group_count()-1).
	/// \param opaque `true` = copia opaca (sin máscara); `false` = cookie-cut.
	/// \return `false` si el grupo/asset/frame no es válido o el plan rechaza el job.
	[[nodiscard]] bool draw_group(graphics::FramePlan& plan, const graphics::BobTarget& target,
				      u16 ring_height, u16 x, u16 ring_y, u16 group, bool opaque = false) const noexcept {
		if (group >= m_groups.size() || m_sheet.empty() || m_masks.empty()) return false;
		if (!layer_animation_enabled(m_group_layer[group])) return true;
		const TileAnimationGroupView& grp = m_groups[group];
		if (grp.frame_count == 0u) return false;
		const u8 frame = m_frame[group] < grp.frame_count ? m_frame[group] : 0u; // frame actual del grupo
		const u16 frame_index = static_cast<u16>(grp.first_frame + frame); // índice en sheet/masks
		if (m_frame_stride == 0u || m_mask_stride == 0u) return false;
		const u16 planes = m_planes; // planos del overlay (1..6)
		const u16 gw = grp.pixel_width != 0u ? grp.pixel_width : m_tile;  // ancho del grupo (px)
		const u16 gh = grp.pixel_height != 0u ? grp.pixel_height : m_tile; // alto del grupo (px)
		const u16 words_per_row = static_cast<u16>(gw / 16u); // palabras/pintura por fila
		const u16 source_row_bytes = static_cast<u16>(words_per_row * 2u); // bytes por fila de un plano
		const u32 source_plane_stride = static_cast<u32>(source_row_bytes) * gh; // bytes entre planos
		if (planes == 0u || gw < 16u || (gw & 15u) != 0u || gh == 0u ||
		    ring_height == 0u) {
			return false;
		}
		const u32 frame_stride = grp.frame_stride != 0u ? grp.frame_stride : m_frame_stride;
		const u32 mask_stride = grp.mask_stride != 0u ? grp.mask_stride : m_mask_stride;
		const u32 frame_base = grp.frame_stride != 0u
			? grp.sheet_offset + static_cast<u32>(frame) * frame_stride
			: static_cast<u32>(frame_index) * m_frame_stride;
		const u32 mask_base = grp.mask_stride != 0u
			? grp.mask_offset + static_cast<u32>(frame) * mask_stride
			: static_cast<u32>(frame_index) * m_mask_stride;
		u16 first_row = 0u;   // primera fila del frame en este segmento
		u16 remaining = gh;   // filas que faltan por dibujar
		s16 ry = static_cast<s16>(ring_y); // línea del anillo del segmento
		while (remaining > 0u) {
			const u16 seg = static_cast<u16>(static_cast<u32>(ry) + remaining > ring_height
				? ring_height - static_cast<u16>(ry) : remaining); // filas hasta la costura o el final
			// Offsets en bytes (sin aritmética de punteros): frame + fila del
			// segmento; `BlitPtr` los aplica sobre la vista Chip del sheet/máscara.
			const u32 sheet_off = frame_base + static_cast<u32>(first_row) * source_row_bytes;
			const u32 mask_off = mask_base + static_cast<u32>(first_row) * source_row_bytes;
			const u32 dest_off = static_cast<u32>(ry) * planes * target.row_bytes + (x >> 3u);
			const graphics::BlitPtr source_ptr {m_sheet, static_cast<s32>(sheet_off)};
			const graphics::BlitPtr mask_ptr {m_masks, static_cast<s32>(mask_off)};
			const graphics::BlitPtr dest_ptr {target.planes, static_cast<s32>(dest_off)};
			// UN trabajo por segmento con `bitplane_count = planes`: el backend
			// recorre los planos con `source_plane_stride_bytes` (hoja planar) y
			// `destination_plane_stride_bytes` (paso entre planos del anillo).
			// Emitir un job por plano multiplicaba por 6 el coste de CPU del
			// parche (validación/copias del FramePlan) sin cambiar el Blitter.
			graphics::BlitJob& job = plan.begin_blit_job(opaque
				? graphics::BlitJobKind::CopyRect
				: graphics::BlitJobKind::MaskedBobCookieCut);
			job.source = source_ptr;
			job.mask = opaque ? graphics::BlitPtr {} : mask_ptr;
			job.destination = dest_ptr;
			job.words_per_row = words_per_row;
			job.height = seg;
			job.source_modulo_bytes = 0;
			const u32 target_row_step = target.interleaved()
				? static_cast<u32>(target.row_bytes) * target.plane_count
				: target.row_bytes;
			job.destination_modulo_bytes = eng::graphics::mod16(
				static_cast<s32>(target_row_step) - static_cast<s32>(words_per_row) * 2);
			job.bitplane_count = static_cast<u8>(planes);
			job.source_shift = 0u;
			job.source_plane_stride_bytes = source_plane_stride;
			job.destination_plane_stride_bytes = target.plane_pointer_step();
			job.minterm = opaque ? 0x00f0u : 0x00cau;
			job.interleaved = false;
			job.source_words_per_row = 0u;
			job.descending = false;
			if (!plan.commit_blit_job()) return false;
			remaining = static_cast<u16>(remaining - seg);
			first_row = static_cast<u16>(first_row + seg);
			ry = 0;
		}
		return true;
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
	eng::ChipView<eng::PatternTag> m_sheet {}; // banco planar de frames (Chip)
	eng::ChipView<eng::MaskTag> m_masks {};    // banco 1bpp de máscaras (Chip)
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
