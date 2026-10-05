#pragma once

#include <eng/graphics/blit_job.hpp>
#include <eng/graphics/playfield_scroll.hpp>
#include <eng/graphics/composition/compose.hpp>
#include <eng/graphics/composition/copper_chunky.hpp>
#include <eng/graphics/effects/raster_gradient.hpp>
#include <eng/graphics/effects/rotozoom.hpp>
#include <eng/graphics/sprite_channel_window.hpp>
#include <eng/core/types/memory_kind.hpp>

namespace eng::effects {

/// **Scroll horizontal fino de una capa planar** (`visible_words` words = 320 px). Mantiene el
/// estado (**fine 0..15** + **columna absoluta**) y produce la `BPLCON1` del frame y, al cruzar
/// el word, los `graphics::BlitJob` de **desplazamiento** + **columna nueva**. Se apoya en
/// `DDFSTRT = $30` (fetch de 1 word extra) y un buffer de `visible_words + 1` words/fila
/// (guarda + columna entrante): es el patrón del driver `graphics/drivers/tile_scroll.hpp`,
/// extraído como helper reutilizable. El contenido lo genera el llamador (procedural o tilemap).
///
/// ```cpp
/// eng::effects::FineScroll scroll;
/// scroll.attach({.plane = plane, .rows = 256});
/// // por frame (tras el arranque, con la lista montada con `scroll.ddfstrt()`/`bplcon1()`):
/// if (scroll.step()) {                        // fine cruzó 16: shift + columna
///     fill_column(scroll.column(), col);      // el llamador llena `col`
///     backend.blitter_submit(scroll.shift_job(), true);
///     backend.blitter_submit(scroll.column_job(col.data()), true);
/// }
/// bplcon1 = scroll.bplcon1();                 // p. ej. move(BPLCON1, bplcon1)
/// ```
class FineScroll {
public:
	struct Config {
		u16* plane = nullptr;    ///< base del plano (Chip RAM)
		u16 rows = 0;            ///< filas (256)
		u16 visible_words = 20u; ///< words visibles (320 px)
	};

	/// Configura la capa. `false` si `plane == nullptr`, `rows == 0` o `visible_words == 0`.
	/// \param cfg  `{plane (base Chip), rows, visible_words}`.
	/// \return `true` si la config es válida.
	[[nodiscard]] bool attach(Config cfg) {
		if (cfg.plane == nullptr || cfg.rows == 0u || cfg.visible_words == 0u) {
			return false;
		}
		m_cfg = cfg;
		m_fine = 0;
		m_column = cfg.visible_words;
		return true;
	}

	/// Bytes por fila del buffer (`visible_words + 1` words: guarda/entrante).
	[[nodiscard]] u16 row_bytes() const noexcept {
		return static_cast<u16>((static_cast<u32>(m_cfg.visible_words) + 1u) * 2u);
	}
	/// `DDFSTRT` del display con **1 word extra** a la izquierda (fine scroll).
	[[nodiscard]] static constexpr u16 ddfstrt() noexcept { return graphics::fine_scroll_ddfstrt; }

	/// Valor de `BPLCON1` del frame (`delay`, `(16 − fine) & 15`).
	[[nodiscard]] u16 bplcon1() const noexcept { return graphics::fine_delay(m_fine); }

	/// Avanza **1 px** el scroll. `true` si toca el **shift de columna** (fine cruzó 16).
	bool step() noexcept {
		if (++m_fine < 16u) {
			return false;
		}
		m_fine = 0;
		++m_column;
		return true;
	}

	/// Columna **absoluta** que entra (para generar su contenido).
	[[nodiscard]] u16 column() const noexcept { return m_column; }
	[[nodiscard]] const Config& config() const noexcept { return m_cfg; }

	/// `BlitJob` del **desplazamiento** de una columna a la izquierda (words 0..v−1 = 1..v).
	[[nodiscard]] graphics::BlitJob shift_job() const noexcept {
		graphics::BlitJob j {};
		j.kind = graphics::BlitJobKind::CopyRect;
		j.source = graphics::BlitPtr::from_storage(m_cfg.plane + 1u);
		j.destination = graphics::BlitPtr::from_storage(m_cfg.plane);
		j.words_per_row = m_cfg.visible_words;
		j.height = m_cfg.rows;
		j.source_modulo_bytes = 2;
		j.destination_modulo_bytes = 2;
		j.bitplane_count = 1u;
		return j;
	}

	/// `BlitJob` de la **columna nueva** (word `visible_words`), con `col` de `rows` words.
	[[nodiscard]] graphics::BlitJob column_job(const u16* col) const noexcept {
		graphics::BlitJob j {};
		j.kind = graphics::BlitJobKind::CopyRect;
		j.source = graphics::BlitPtr::from_storage(col);
		j.destination = graphics::BlitPtr::from_storage(m_cfg.plane + m_cfg.visible_words);
		j.words_per_row = 1u;
		j.height = m_cfg.rows;
		j.source_modulo_bytes = 0;
		j.destination_modulo_bytes = static_cast<s16>(row_bytes() - 2u);
		j.bitplane_count = 1u;
		return j;
	}

private:
	Config m_cfg {};
	u16 m_fine = 0;
	u16 m_column = 0;
};

} // namespace eng::effects
