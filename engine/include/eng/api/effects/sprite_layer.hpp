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

/// **Capa de fondo con canales de sprite rearmados horizontalmente** (sprite-as-playfield):
/// los canales se reposicionan y recargan su DATA a lo largo de cada línea para cubrir el
/// ancho de la pantalla, **sin coste de bitplanes**. Es una capa de **fondo**: los sprites
/// quedan detrás del playfield (prioridad `BPLCON2`). Ver
/// `docs/reference/amiga/techniques/sprite-horizontal-multiplex.md`.
class SpriteLayer {
public:
	struct Config {
		u16 first_line = 0; ///< primera línea del efecto
		u16 lines = 0;      ///< nº de líneas que cubre
		u8 channels = 8;    ///< canales usados (1..8)
		u16 hpos0 = 0;      ///< x del primer tramo (low-res px)
		u16 hpos_step = 0;  ///< separación entre tramos (>=16 px; columnas contiguas)
		u16 data_high = 0;  ///< SPRxDATA (primera palabra de la fila) — canales Copper
		u16 data_low = 0;   ///< SPRxDATB (segunda palabra) — canales Copper
		/// **Free form**: nº total de posiciones de 16 px a cubrir (columnas a lo ancho).
		/// `0` = `channels` (comportamiento clásico: una posición por canal). Si es `> channels`,
		/// las posiciones extra **reutilizan** los canales ciclando `k % channels` (≥24 px entre
		/// reusos; ver `sprite-horizontal-multiplex.md`, variante *Free Form*).
		u16 columns = 0;
		/// **Free form**: imagen por (columna, línea) = `columns * lines * 2` words (`DAT`,
		/// `DATB`). Si **no** está vacía, cada posición Copper recibe su DATA distinta (fondo
		/// **no repetitivo**); si está vacía se usa `data_high`/`data_low` en todas (patrón
		/// repetido). La DATA debe estar en Chip RAM (el Copper la escribe; el DMA la lee).
		eng::Span<const eng::u16> image {};
		/// **Ventana**: primera columna del **mundo** que muestra la 1.ª posición. Permite un mundo
		/// más ancho que la vista y scrollear sobre él sin recomponer la DATA (`image` es el mundo).
		u16 window_col = 0u;
		u16 bplcon2 = 0;    ///< prioridad (`BPLCON2`); sprites detrás del playfield = fondo
		u16 arm_hpos = 0x40; ///< posición H del `WAIT` de rearmado: debe caer **después** del
		                     ///< fetch DMA de sprites (`DDFSTRT`) y **antes** de la primera
		                     ///< columna, para que la DATA del Copper gane a la del DMA.
		/// **Canales DMA** (los `dma_channels` primeros): sprite **alto** (toda la banda) cuya
		/// estructura en Chip RAM lleva **cabecera `POS`+`CTL`** (`[POS, CTL, DAT0, DATB0, …,
		/// 0, 0]`); `SPRxPT` apunta al **inicio** de la estructura y Agnus recarga POS/CTL de
		/// ahí. `SpriteLayer` parchea el POS de la cabecera (scroll). El resto de canales
		/// (Copper) rearman POS/DATA por línea, pero **también necesitan** su estructura:
		/// sin cabecera/terminador válidos el DMA del canal avanza por memoria y lee una
		/// cabecera basura (columna fantasma fuera de la banda). Ver
		/// `spr_layer/Sprite_Layer/` (Jeroen Knoester).
		u8 dma_channels = 0;
		eng::Span<eng::u16> dma_data {}; ///< estructura DMA por canal (`channels` estructuras de `dma_stride` words)
		u16 dma_stride = 0;              ///< words por estructura (`2 + dma_height*2 + 2`)
	};

	/// Configura la capa. `false` si `lines == 0`, `channels` fuera de 1..8, `hpos_step < 16`,
	/// `dma_channels > channels` o hay canales DMA sin `dma_data`/`dma_stride`. Si se da
	/// `dma_data`, debe haber **una estructura por canal** (`dma_stride` words cada una).
	[[nodiscard]] bool attach(Config cfg) {
		if (cfg.lines == 0u || cfg.channels == 0u || cfg.channels > 8u ||
		    cfg.hpos_step < 16u || cfg.dma_channels > cfg.channels ||
		    (cfg.columns != 0u &&
		     (cfg.columns < cfg.channels ||
		      (!cfg.image.empty() &&
		       cfg.image.size() < static_cast<eng::usize>(cfg.columns) * cfg.lines * 2u))) ||
		    (cfg.dma_channels > 0u && (cfg.dma_data.empty() || cfg.dma_stride == 0u))) {
			return false;
		}
		m_cfg = cfg;
		return true;
	}

	/// Desplaza la capa horizontalmente (px low-res; el paso entre columnas no cambia).
	void set_scroll(u16 x) noexcept { m_scroll = x; }

	/// Fija la **ventana** sobre el mundo (primera columna de `image` que se muestra). El llamador
	/// debe rellenar las estructuras DMA de los canales con la DATA de esa ventana y re-emitir.
	void set_window_col(u16 c) noexcept { m_cfg.window_col = c; }

	/// **Layout de parcheo por Blitter** (válido tras `bind`): índice (en words) del 1.er
	/// `SPRxPOS` de las columnas Copper; stride entre líneas del bloque; y base del bloque de
	/// reposición de fin de línea. Permite a la capa/demo parchear con `blitter_fill_words_strided`.
	[[nodiscard]] u16 pos_word_base() const noexcept { return m_pos_base; }
	[[nodiscard]] u16 pos_line_stride_words() const noexcept {
		const u16 col_end = (m_cfg.columns != 0u) ? m_cfg.columns : m_cfg.channels;
		return static_cast<u16>((col_end - m_cfg.dma_channels) * 6u +
					m_cfg.dma_channels * 2u + 2u);
	}
	/// `SPRxPOS` de la columna `k` con el scroll actual (valor a escribir en la copperlist).
	[[nodiscard]] u16 column_pos(u16 k) const noexcept { return pos_for(0u, k); }

	/// Emite la capa completa. **Todos los canales** reciben `SPRxPT` apuntando a su
	/// estructura (`dma_data`), con `POS`+`CTL`: es lo que impide que el DMA de un canal
	/// Copper siga avanzando por memoria tras el terminador. **Canales DMA** (los primeros):
	/// solo la estructura (columna alta, posición parcheada con el scroll). **Canales
	/// Copper**: además, por cada línea, un `WAIT` en `arm_hpos` (después del fetch DMA) y
	/// una **ráfaga** de `SPRxCTL`+`SPRxPOS`+`SPRxDATB`+`SPRxDATA` que reescribe posición y
	/// data de la línea; `DATA` arma el sprite.
	template <class Sched>
	void emit_into(Sched& sched) const {
		sched.move(copper::Register::BPLCON2, m_cfg.bplcon2); // prioridad de fondo
		const u16 vstop = static_cast<u16>(m_cfg.first_line + m_cfg.lines);

		// --- Estructuras DMA: SPRxPT + cabecera POS+CTL para TODOS los canales ---
		// Cada canal (DMA y Copper) apunta a una estructura válida con cabecera y
		// terminador. Si un canal Copper quedara apuntando a una estructura nula corta,
		// el DMA del canal seguiría avanzando por memoria y leería basura como cabecera
		// (columna fantasma fuera de la banda). El Copper reescribe POS/DATA por línea
		// (más abajo), pero el DMA necesita una estructura consistente.
		if (!m_cfg.dma_data.empty() && m_cfg.dma_stride != 0u) {
			const eng::uintptr base = reinterpret_cast<eng::uintptr>(m_cfg.dma_data.data());
			for (u8 ch = 0u; ch < m_cfg.channels; ++ch) {
				const u16 hpos = static_cast<u16>(m_cfg.hpos0 +
								  static_cast<u16>(ch) * m_cfg.hpos_step + m_scroll);
				const u16 pos = static_cast<u16>(((m_cfg.first_line & 0xffu) << 8u) |
								 ((hpos >> 1u) & 0xffu));
				// Parchear el POS de la cabecera y apuntar PT a la estructura del canal.
				m_cfg.dma_data[static_cast<eng::u32>(ch) * m_cfg.dma_stride] = pos;
				const eng::uintptr addr =
					base + static_cast<eng::uintptr>(ch) * m_cfg.dma_stride * 2u;
				sched.move(static_cast<copper::Register>(0x120u + ch * 4u),
					   static_cast<u16>(addr >> 16));                 // SPRxPTH
				sched.move(static_cast<copper::Register>(0x122u + ch * 4u),
					   static_cast<u16>(addr & 0xffffu));             // SPRxPTL
			}
		}

		// --- Canales Copper: rearm por línea (WAIT + CTL/POS/DATB/DATA) ---
		// Nº de posiciones a cubrir (free form: `columns`; clásico: una por canal).
		const u16 col_end = (m_cfg.columns != 0u) ? m_cfg.columns : m_cfg.channels;
		for (u16 line = m_cfg.first_line; line < vstop; ++line) {
			sched.wait_position_safe(line, static_cast<u8>(m_cfg.arm_hpos & 0xfeu));
			for (u16 k = m_cfg.dma_channels; k < col_end; ++k) {
				// Canal reutilizado: las posiciones extra ciclan los canales (`k % channels`).
				const u8 ch = static_cast<u8>(k % m_cfg.channels);
				const u16 hpos = static_cast<u16>(m_cfg.hpos0 + k * m_cfg.hpos_step + m_scroll);
				const u16 pos = static_cast<u16>(((m_cfg.first_line & 0xffu) << 8u) |
								 ((hpos >> 1u) & 0xffu));
				if (m_binding && line == m_cfg.first_line && k == m_cfg.dma_channels) {
					// Palabra de DATO del primer `SPRxPOS` (la 2.ª de las 4 words del canal).
					// `if constexpr` para no exigir `words_used()` a schedulers de prueba.
					if constexpr (requires { sched.words_used(); }) {
						m_pos_base = static_cast<u16>(sched.words_used() + 1u);
						m_pos_valid = true;
					}
				}
				u16 dat = m_cfg.data_high;
				u16 datb = m_cfg.data_low;
				if (!m_cfg.image.empty()) { // Free form (ventana). `image` va (DATB, DATA).
					const eng::usize t = (static_cast<eng::usize>(k + m_cfg.window_col) *
							      m_cfg.lines +
							      (line - m_cfg.first_line)) * 2u;
					datb = m_cfg.image[t];     // SPRxDATB = bit 1
					dat = m_cfg.image[t + 1u]; // SPRxDATA = bit 0
				}
				// Rearmado por Copper: **solo** `POS`+`DATB`+`DATA` (NO se escribe `SPRxCTL`;
				// `SPRxDATA` arma el sprite). Ver `spr_layer/Data/copperlists.asm`.
				sched.move(static_cast<copper::Register>(0x140u + ch * 8u), pos);    // SPRxPOS
				sched.move(static_cast<copper::Register>(0x146u + ch * 8u), datb);   // SPRxDATB
				sched.move(static_cast<copper::Register>(0x144u + ch * 8u), dat);    // SPRxDATA (arma)
			}
			// Fin de línea: reposiciona los canales DMA a la izquierda **en orden inverso** para
			// que el próximo renglón los vuelva a dibujar en su sitio (fuente:
			// `spr_layer/Data/copperlists.asm`, "End of line repositioning").
			for (u16 i = m_cfg.dma_channels; i-- > 0u; ) {
				sched.move(static_cast<copper::Register>(0x140u + i * 8u),
					   pos_for(line, i));
			}
		}
	}

	/// Emite la capa al plan de la escena (azúcar de `emit_into(scene.scheduler())`).
	void frame(graphics::composition::Scene& scene) { emit_into(scene.scheduler()); }

	/// **Prepara el parcheo por frame**: emite la capa (como `emit_into`) **registrando** dónde
	/// caen las palabras `SPRxPOS`. Llamar **una vez** (setup). Después, `patch(words)` reescribe
	/// solo esas palabras con el scroll actual, sin re-emitir (coste ~0 por frame).
	template <class Sched>
	void bind(Sched& sched) {
		m_binding = true;
		emit_into(sched);
		m_binding = false;
	}

	/// Reescribe las palabras `SPRxPOS` (canales Copper, por línea) y el `POS` de cabecera de
	/// los canales DMA con el `m_scroll` actual. `words` = la copperlist ya materializada
	/// (p. ej. `scene.plan().active_words()`). Requiere haber llamado `bind` en el setup.
	void patch(u16* words) const noexcept {
		if (!m_pos_valid || words == nullptr) {
			return;
		}
		const u16 col_end = (m_cfg.columns != 0u) ? m_cfg.columns : m_cfg.channels;
		// Precomputa el HSTART por columna (una vez por frame): evita el producto `k*step` por
		// línea (era una libcall `__mulsi3` por palabra -> el patch se comía el frame).
		u16 hstart[32] {};
		for (u16 k = 0u; k < col_end && k < 32u; ++k) {
			hstart[k] = static_cast<u16>((m_cfg.hpos0 + k * m_cfg.hpos_step + m_scroll) >> 1u) &
				    0xffu;
		}
		u16 idx = m_pos_base;
		const u16 vstop = static_cast<u16>(m_cfg.first_line + m_cfg.lines);
		const u16 v = static_cast<u16>((m_cfg.first_line & 0xffu) << 8u); // VSTART fijo
		for (u16 line = m_cfg.first_line; line < vstop; ++line) {
			// Columnas Copper: `POS`+`DATB`+`DATA` (3 MOVEs = 6 words); el `POS` va primero.
			for (u16 k = m_cfg.dma_channels; k < col_end; ++k) {
				words[idx] = static_cast<u16>(v | hstart[k]);
				idx = static_cast<u16>(idx + 6u);
			}
			// Fin de línea: reposición de los canales DMA (1 MOVE = 2 words cada uno).
			for (u16 i = m_cfg.dma_channels; i-- > 0u; ) {
				words[idx] = static_cast<u16>(v | hstart[i]);
				idx = static_cast<u16>(idx + 2u);
			}
			idx = static_cast<u16>(idx + 2u); // WAIT de la línea siguiente
		}
		if (!m_cfg.dma_data.empty() && m_cfg.dma_stride != 0u) {
			for (u8 ch = 0u; ch < m_cfg.dma_channels; ++ch) {
				m_cfg.dma_data[static_cast<eng::u32>(ch) * m_cfg.dma_stride] =
					pos_for(m_cfg.first_line, ch);
			}
		}
	}

	/// Contrato `Effect`: avanza el estado temporal. La capa no anima por sí sola (el
	/// llamador fija el scroll con `set_scroll`), así que aquí no hace nada.
	void update(eng::u16) noexcept {}

	/// Tramo de raster que reclama la capa (`reserve_band`): la banda que cubre y la máscara
	/// de **sus canales**. Así `reserve_band` solo ve conflicto con otro efecto que use el
	/// mismo canal en líneas solapadas: dos fondos por sprites en canales distintos comparten
	/// scanlines (el límite es por canal, no por región de pantalla).
	[[nodiscard]] copper::BandScope band_scope() const noexcept {
		const u16 last = static_cast<u16>(m_cfg.first_line + m_cfg.lines - 1u);
		return copper::BandScope {m_cfg.first_line, last,
					  graphics::sprite_channel_register_mask(0u, m_cfg.channels)};
	}

	/// **Coste declarado** del efecto (huella estimada): nº de "aportaciones" (1 `BPLCON2`
	/// + un rearm por línea y canal Copper) y palabras de Copper (`words_estimate`).
	[[nodiscard]] copper::EffectCost effect_cost() const noexcept {
		const u32 copper_ch = static_cast<u32>(m_cfg.channels - m_cfg.dma_channels);
		const u32 intents = 1u + static_cast<u32>(m_cfg.lines) * copper_ch;
		return copper::EffectCost {
			static_cast<u16>(intents), static_cast<u16>(words_estimate())};
	}

	/// **Contrato `Effect`** sobre el plan de la escena: reserva la banda (`reserve_band`),
	/// anota el coste (`note_effect_cost`) y emite la capa. Devuelve `false` si la banda
	/// **solapa** con otro efecto o si el coste no cabe en el presupuesto; la lista se
	/// emite igualmente (el llamador decide abortar). Registrar como:
	/// `scene.add_effect([&layer](Scene& s) { layer.update(s.frame()); layer.apply_into(s.plan()); });`
	[[nodiscard]] bool apply_into(copper::Plan& plan) const {
		const copper::BandScope band = band_scope();
		const bool free = plan.reserve_band(band.first_line, band.last_line, band.register_mask);
		const bool fits = plan.note_effect_cost(effect_cost());
		emit_into(plan.scheduler());
		return free && fits;
	}

	[[nodiscard]] const Config& config() const noexcept { return m_cfg; }
	/// Huella estimada en palabras de Copper (para `EffectCost`): `BPLCON2` + 2 por canal
	/// con estructura (`SPRxPT` H/L) + por línea [`WAIT` (2) + 3 MOVEs por canal Copper].
	[[nodiscard]] u16 words_estimate() const noexcept {
		const u32 cop = static_cast<u32>(m_cfg.channels - m_cfg.dma_channels);
		const u32 struct_ch = (!m_cfg.dma_data.empty() && m_cfg.dma_stride != 0u)
					      ? static_cast<u32>(m_cfg.channels)
					      : static_cast<u32>(m_cfg.dma_channels);
		return static_cast<u16>(1u + struct_ch * 2u +
					static_cast<u32>(m_cfg.lines) * (2u + cop * 6u));
	}

private:
	/// `SPRxPOS` de la línea `line` y canal `ch` con el `m_scroll` actual.
	[[nodiscard]] u16 pos_for(u16 /*line*/, u16 k) const noexcept {
		const u16 hpos = static_cast<u16>(m_cfg.hpos0 + k * m_cfg.hpos_step + m_scroll);
		return static_cast<u16>(((m_cfg.first_line & 0xffu) << 8u) | ((hpos >> 1u) & 0xffu));
	}

	Config m_cfg {};
	u16 m_scroll = 0;
	/// Palabra de DATO del primer `SPRxPOS` (canal Copper). La registra `bind` durante la
	/// emisión; `patch` reescribe desde ahí con strides regulares.
	mutable u16 m_pos_base = 0;
	mutable bool m_pos_valid = false; ///< `bind` registró la posición de parcheo
	bool m_binding = false;           ///< `emit_into` está registrando (lo activa `bind`)
};

} // namespace eng::effects
