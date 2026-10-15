#pragma once

/// \file sprite_line_layer.hpp
/// **Capa/HUD de sprites por parcheo de `SPRxPOS` + `SPRxDATA`/`DATB` por línea**
/// (patrón Parasol Stars / Brian the Lion): un tramo vertical se **rearma en cada
/// scanline** para escribir la X y la **imagen de esa línea**.
///
/// Sirve para:
///
///   - **HUD de texto**: un sprite que muestra caracteres distintos en cada línea
///     (Parasol Stars usa uno solo para todo el marcador), sin gastar un canal por glifo.
///   - **Capa estrecha de 2-4 colores** repetida a lo ancho: pocos píxeles de alto,
///     muchas columnas, con una imagen propia por fila.
///   - **Fondos de pocas filas** cuyo DATA cambia de una línea a otra (Brian the Lion).
///
/// Es la variante *data-driven* sobre `copper::Scheduler::emit_sprite_horizontal_rearm(...)`
/// (`graphics::SpriteHorizontalRearm`): este header NO escribe registros; construye un
/// rearm por **(línea, canal)** y se lo entrega al scheduler, que los coloca en el
/// H-Blank y comprueba la carrera contra el haz. La separación mínima entre canales para
/// que el Copper llegue a tiempo está en `sprite_limits.hpp` (`kSpriteMinReusePx`).
///
/// **Orden de emisión**: línea a línea y, dentro de cada línea, canal a canal con `hpos`
/// estrictamente creciente (el Copper ejecuta los WAIT en orden; uno ya pasado esperaría
/// al frame siguiente). Por eso `hpos0 + ch * hpos_step - scroll` debe crecer con `ch`.
///
/// Lógica pura, host-testable, sin heap: el llamador aporta la tabla de DATA (Chip RAM).

#include <eng/core/types/types.hpp>
#include <eng/graphics/copper/copper.hpp>
#include <eng/graphics/raster_intent.hpp>
#include <eng/graphics/sprite_chain.hpp>
#include <eng/graphics/sprite_limits.hpp>
#include <eng/setup/coherence.hpp>

namespace eng::graphics {

/// Configuración de una `SpriteLineLayer` (todo cabe en el struct; sin heap).
struct SpriteLineLayerConfig {
	u16 first_line = 0;    ///< primera línea de la capa (inclusive)
	u16 lines = 0;         ///< nº de líneas que cubre la banda (1..512)
	u8  channel_first = 0; ///< primer canal de la corrida (0..7)
	u8  channels = 0;      ///< canales de la corrida (1..8)
	u16 hpos0 = 0;         ///< X del primer canal (px low-res; el WAIT debe caer tras el
			       ///< fetch de la línea, `DDFSTRT`/2 ≈ 28 px)
	u16 hpos_step = 16u;   ///< separación entre canales (debe ser > 0)
	u16 scroll = 0;        ///< desplazamiento horizontal (px); se resta a cada X
	/// Tabla de DATA por **canal** y línea: `[channel][line][2]` = `{DAT, DATB}` en Chip.
	/// Es la **estructura que recibe `SPRxPT`** (patrón `arm_object`/053): el DMA de un canal
	/// armado hace fetch de 2 palabras por línea y **avanza el walk** por ella — el registro
	/// `SPRxDATA` que escribe el Copper a media línea lo pisa el fetch, así que la DATA del
	/// HUD vive aquí, no en MOVEs (ver `winuae/sprite-dma.md` §Armado de un canal).
	eng::ChipView<eng::SpriteTag> line_data {};
	bool attach = false;   ///< par attached (15 colores)
	/// Planos del display donde se parchea. El parcheo de DATA a media línea necesita hueco
	/// de DMA en la línea: con más de `kSpriteLineDataMaxBitplanes` planos **no lo hay**
	/// (check_sprite_line_data; `custom.cpp:10111`/`10130-10131`) y `configure` lo rechaza.
	u8 display_planes = 4u;
};

/// **Capa de sprites rearmada por línea.** Ver la ficha del fichero.
class SpriteLineLayer {
public:
	constexpr SpriteLineLayer() = default;

	/// Valida y fija la configuración. Devuelve `false` (sin tocar el estado) si:
	/// `lines == 0`, el tramo se sale de 512 líneas, no hay canales, la corrida
	/// `[channel_first, channel_first+channels)` se sale de los 8, `hpos_step == 0`, la
	/// tabla `line_data` no cubre `channels × lines × 2` palabras, o `display_planes`
	/// supera `kSpriteLineDataMaxBitplanes` (sin hueco de DMA para el parcheo de DATA a
	/// media línea — `check_sprite_line_data`).
	[[nodiscard]] constexpr bool configure(const SpriteLineLayerConfig& cfg) noexcept {
		if (cfg.lines == 0u || cfg.channels == 0u || cfg.hpos_step == 0u) {
			return false;
		}
		if (cfg.first_line + cfg.lines > 512) {
			return false;
		}
		if (cfg.channel_first + cfg.channels > kSpriteChannels) {
			return false;
		}
		if (cfg.line_data.size() <
		    static_cast<eng::usize>(cfg.channels) * cfg.lines * 2u * sizeof(u16)) {
			return false;
		}
		if (!eng::setup::check_sprite_line_data(cfg.display_planes).ok()) {
			return false;
		}
		m_cfg = cfg;
		return true;
	}

	constexpr void set_scroll(u16 scroll) noexcept { m_cfg.scroll = scroll; }
	[[nodiscard]] constexpr const SpriteLineLayerConfig& config() const noexcept { return m_cfg; }

	/// Nº de **reposiciones** que emitirá (una por línea y canal).
	[[nodiscard]] constexpr u16 rearm_count() const noexcept {
		return static_cast<u16>(m_cfg.lines * m_cfg.channels);
	}

	/// Emite el armado (uno por canal: `POS`+`CTL`+`PT` → su tabla de DATA, patrón
	/// `arm_object`/053) y una **reposición** POS-only por (línea, canal) — el walk del DMA
	/// entrega la DATA de cada línea. Los canales cuya X cae fuera de pantalla
	/// (`< 0` o `> 0x1fe`) se omiten (no se arman ni se reposicionan).
	template <class Sched>
	void emit_into(Sched& sched) const {
		// 1) Armado único por canal, al inicio de la banda.
		for (u8 ch = 0u; ch < m_cfg.channels; ++ch) {
			const s32 x = static_cast<s32>(m_cfg.hpos0) + ch * m_cfg.hpos_step - m_cfg.scroll;
			if (x < 0 || x > 0x1fe) {
				continue;
			}
			const u8 channel = static_cast<u8>(m_cfg.channel_first + ch);
			const SpritePosCtl e = encode_sprite_pos_ctl(
				static_cast<u16>(x), m_cfg.first_line, m_cfg.lines, m_cfg.attach);
			sched.wait_line_safe(m_cfg.first_line);
			sched.move(static_cast<eng::copper::Register>(0x140u + channel * 8u), e.pos);
			sched.move(static_cast<eng::copper::Register>(0x142u + channel * 8u), e.ctl);
			// PT → la DATA del canal (`[channel][line][2]`), no a una cabecera: el fetch
			// entrega las filas y no hay nada que el registro pueda pisar (ver el config).
			const eng::uintptr addr =
				m_cfg.line_data.address(static_cast<eng::s32>(ch) *
							static_cast<eng::s32>(m_cfg.lines) * 4).value;
			sched.move(static_cast<eng::copper::Register>(0x120u + channel * 8u),
				   static_cast<u16>(addr >> 16u));
			sched.move(static_cast<eng::copper::Register>(0x122u + channel * 8u),
				   static_cast<u16>(addr & 0xffffu));
		}
		// 2) Reposición POS-only por (línea, canal): 1 MOVE por segmento (patrón Risky Woods).
		for (u16 v = m_cfg.first_line; v < m_cfg.first_line + m_cfg.lines; ++v) {
			for (u8 ch = 0u; ch < m_cfg.channels; ++ch) {
				const s32 x =
					static_cast<s32>(m_cfg.hpos0) + ch * m_cfg.hpos_step - m_cfg.scroll;
				if (x < 0 || x > 0x1fe) {
					continue;
				}
				sched.emit_sprite_horizontal_reposition(
					static_cast<u8>(m_cfg.channel_first + ch),
					static_cast<u8>(v), static_cast<u16>(x));
			}
		}
	}

private:
	SpriteLineLayerConfig m_cfg {};
};

} // namespace eng::graphics
