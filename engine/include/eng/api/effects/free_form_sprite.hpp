#pragma once

#include <eng/graphics/blit_job.hpp>
#include <eng/graphics/playfield_scroll.hpp>
#include <eng/graphics/composition/compose.hpp>
#include <eng/graphics/composition/copper_chunky.hpp>
#include <eng/graphics/effects/raster_gradient.hpp>
#include <eng/graphics/effects/rotozoom.hpp>
#include <eng/graphics/sprite_channel_window.hpp>
#include <eng/core/types/memory_kind.hpp>
#include <eng/api/effects/sprite_layer.hpp>

namespace eng::effects {

/// **Fondo de sprites NO repetitivo con scroll** (`FreeFormSpriteLayer`): un fondo de `columns`
/// columnas de 16 px donde **cada columna es distinta** (no un patrón que se repite), con scroll
/// por Copper y **CPU libre**. Los `dma_channels` primeros canales dibujan sus columnas por **DMA**;
/// el **Copper reutiliza** esos canales para el resto reescribiendo `SPRxPOS`+`SPRxDATB`+`SPRxDATA`
/// (sin `SPRxCTL`; reposición de fin de línea). Es la técnica *Free Form Sprite Layer* (Jeroen
/// Knoester); apoya en `SpriteLayer` en modo libre y añade el `bind`/`patch` del scroll (~0 CPU).
///
/// Reparto de ladrillos: `RiskyWoodsLayer` = patrón que **se repite** (solo reposiciona `POS`,
/// barato); `SpriteLayer` = capa genérica; **`FreeFormSpriteLayer` = no repetitivo**. Ver
/// `docs/reference/amiga/techniques/sprite-horizontal-multiplex.md`.
class FreeFormSpriteLayer {
public:
	struct Config {
		u16 first_line = 0u;   ///< primera línea de la banda (inclusive)
		u16 lines = 0u;        ///< líneas que cubre
		u16 hpos0 = 128u;      ///< X de la 1.ª columna (lo-res)
		u16 hpos_step = 16u;   ///< paso entre columnas (px)
		u16 columns = 0u;      ///< nº de posiciones (columnas) a cubrir
		u16 bplcon2 = 0u;      ///< prioridad (`BPLCON2`); fondo = detrás del playfield
		u8  arm_hpos = 0x40u;  ///< `WAIT` del rearmado (hpos = px/2)
		u8  channels = 8u;     ///< canales usados (1..8)
		u8  dma_channels = 8u; ///< canales que dibujan por DMA (las primeras columnas)
		/// Imagen por (columna, línea): `columns * lines * 2` words (`DAT`,`DATB`).
		eng::Span<const eng::u16> image {};
		/// **Estructuras DMA** (Chip): `channels * dma_stride` words; el llamador las rellena con
		/// la DATA de las primeras `dma_channels` columnas.
		eng::Span<eng::u16> dma_data {};
		u16 dma_stride = 0u;   ///< words por estructura (`2 + lines*2 + 2`)
	};

	/// Instala el efecto con `cfg` (geometría, imagen y estructuras DMA) delegando en el
	/// `SpriteLayer` interno; devuelve si la capa aceptó la configuración. Llamar antes de `bind`.
	[[nodiscard]] bool attach(Config cfg) {
		m_cfg = cfg;
		SpriteLayer::Config sc {};
		sc.first_line = cfg.first_line;
		sc.lines = cfg.lines;
		sc.hpos0 = cfg.hpos0;
		sc.hpos_step = cfg.hpos_step;
		sc.columns = cfg.columns;
		sc.bplcon2 = cfg.bplcon2;
		sc.arm_hpos = cfg.arm_hpos;
		sc.channels = cfg.channels;
		sc.dma_channels = cfg.dma_channels;
		sc.image = cfg.image;
		sc.dma_data = cfg.dma_data;
		sc.dma_stride = cfg.dma_stride;
		return m_layer.attach(sc);
	}

	/// Monta la copperlist (con las `POS` fijas por línea) y **registra** las palabras `SPRxPOS`
	/// para el scroll. Llamar **una vez** (setup).
	template <class Sched>
	void bind(Sched& sched) {
		m_layer.bind(sched);
	}

	/// Emite sin registrar (para re-emitir la lista entera si hiciera falta).
	template <class Sched>
	void emit_into(Sched& sched) const {
		m_layer.emit_into(sched);
	}

	/// Desplaza el fondo horizontalmente (px lo-res). Coste ~0: el llamador aplica con `patch`.
	void set_scroll(u16 x) noexcept { m_layer.set_scroll(x); }

	/// Fija la **ventana** sobre el mundo (primera columna de `image` que se muestra); el llamador
	/// rellena las estructuras DMA de la ventana y re-emite la copperlist.
	void set_window_col(u16 c) noexcept { m_layer.set_window_col(c); }

	/// Layout de parcheo por Blitter (ver `SpriteLayer`).
	[[nodiscard]] u16 pos_word_base() const noexcept { return m_layer.pos_word_base(); }
	[[nodiscard]] u16 pos_line_stride_words() const noexcept { return m_layer.pos_line_stride_words(); }
	[[nodiscard]] u16 column_pos(u16 k) const noexcept { return m_layer.column_pos(k); }

	/// Reescribe las palabras `SPRxPOS` con el scroll actual (~0 CPU), sobre la lista ya montada.
	void patch(u16* words) const noexcept { m_layer.patch(words); }

	/// Emite al plan de la escena (`emit_into(scene.scheduler())`).
	void frame(graphics::composition::Scene& scene) { m_layer.frame(scene); }

	/// Contrato `Effect`: la capa no anima por sí sola (el llamador fija el scroll).
	void update(eng::u16 f) noexcept { m_layer.update(f); }

	/// Tramo de raster + canales que reclama (para combinarse con otros fondos).
	[[nodiscard]] copper::BandScope band_scope() const noexcept { return m_layer.band_scope(); }

	/// **Contrato `Effect`** sobre el plan: reserva la banda, anota el coste y emite.
	[[nodiscard]] bool apply_into(copper::Plan& plan) const { return m_layer.apply_into(plan); }

	/// Coste declarado (delegado en `SpriteLayer`).
	[[nodiscard]] copper::EffectCost effect_cost() const noexcept { return m_layer.effect_cost(); }

	[[nodiscard]] const Config& config() const noexcept { return m_cfg; }

private:
	Config m_cfg {};
	mutable SpriteLayer m_layer {};
};

} // namespace eng::effects
