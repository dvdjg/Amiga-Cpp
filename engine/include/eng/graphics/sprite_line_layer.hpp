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
#include <eng/graphics/raster_intent.hpp>
#include <eng/graphics/sprite_limits.hpp>

namespace eng::graphics {

/// Configuración de una `SpriteLineLayer` (todo cabe en el struct; sin heap).
struct SpriteLineLayerConfig {
	u16 first_line = 0;    ///< primera línea de la capa (inclusive)
	u16 lines = 0;         ///< nº de líneas que rearma (1..512)
	u8  channel_first = 0; ///< primer canal de la corrida (0..7)
	u8  channels = 0;      ///< canales que se rearman por línea (1..8)
	u16 hpos0 = 0;         ///< X del primer canal (px low-res)
	u16 hpos_step = 16u;   ///< separación entre canales (debe ser > 0)
	u16 scroll = 0;        ///< desplazamiento horizontal (px); se resta a cada X
	/// Tabla de DATA por línea: `[lines][channels][2]` = `{DAT, DATB}` (Chip RAM).
	/// `nullptr` = se usa el par constante `data_high`/`data_low` en todas las líneas.
	const u16* line_data = nullptr;
	u16 data_high = 0;     ///< `SPRxDATA` constante (si `line_data == nullptr`)
	u16 data_low = 0;      ///< `SPRxDATB` constante (si `line_data == nullptr`)
	bool attach = false;   ///< par attached (15 colores)
};

/// **Capa de sprites rearmada por línea.** Ver la ficha del fichero.
class SpriteLineLayer {
public:
	constexpr SpriteLineLayer() = default;

	/// Valida y fija la configuración. Devuelve `false` (sin tocar el estado) si:
	/// `lines == 0`, el tramo se sale de 512 líneas, no hay canales, la corrida
	/// `[channel_first, channel_first+channels)` se sale de los 8, o `hpos_step == 0`.
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
		m_cfg = cfg;
		return true;
	}

	constexpr void set_scroll(u16 scroll) noexcept { m_cfg.scroll = scroll; }
	[[nodiscard]] constexpr const SpriteLineLayerConfig& config() const noexcept { return m_cfg; }

	/// Nº de rearmes que emitirá (uno por línea y canal).
	[[nodiscard]] constexpr u16 rearm_count() const noexcept {
		return static_cast<u16>(m_cfg.lines * m_cfg.channels);
	}

	/// Emite un `SpriteHorizontalRearm` por (línea, canal) en el scheduler. Los canales
	/// cuya X cae fuera de pantalla (`< 0` o `> 0x1fe`) se omiten esa línea.
	template <class Sched>
	void emit_into(Sched& sched) const {
		for (u16 v = m_cfg.first_line; v < m_cfg.first_line + m_cfg.lines; ++v) {
			const int line = v - m_cfg.first_line;
			for (u8 ch = 0; ch < m_cfg.channels; ++ch) {
				// La aritmética se hace en `int` (los operandos promocionan solos) y la
				// geometría se reempaqueta a los campos del registro al final.
				const s32 x = m_cfg.hpos0 + ch * m_cfg.hpos_step - m_cfg.scroll;
				if (x < 0 || x > 0x1fe) {
					continue;
				}
				SpriteHorizontalRearm r {};
				r.channel = static_cast<u8>(m_cfg.channel_first + ch);
				r.hpos = static_cast<u16>(x & 0xfffe);
				r.vstart = v;
				r.vstop = static_cast<u16>(v + 1u);
				if (m_cfg.line_data != nullptr) {
					const int idx = (line * m_cfg.channels + ch) * 2;
					r.data_high = m_cfg.line_data[idx];
					r.data_low = m_cfg.line_data[idx + 1];
				} else {
					r.data_high = m_cfg.data_high;
					r.data_low = m_cfg.data_low;
				}
				r.attach = m_cfg.attach;
				sched.emit_sprite_horizontal_rearm(r);
			}
		}
	}

private:
	SpriteLineLayerConfig m_cfg {};
};

} // namespace eng::graphics
