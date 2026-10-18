#pragma once

/// \file strip.hpp
/// **Presentador de franja del HUD** (`eng::hud`): materializa la lista de `HudDrawOp` sobre un
/// `CanvasPlayfield` (Chip) con `Surface`, con recorte a la banda. Es la **ruta CPU de referencia**
/// de H1 (`ROADMAP_HUD.md`): el compositor de escena publica la banda (split de `ModeSwitchZone`);
/// este presentador no toca registros ni conoce el chipset. Diseño: `HUD_SYSTEM.md` §6.3.
///
/// Verificación: **HOST-468**. La demo `302_hud_band` ejercita la banda en hardware (franja de
/// 2 planos con paleta propia, deltas y presupuesto de 1 campo).

#include <eng/core/types/box.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/field/canvas_playfield.hpp>
#include <eng/field/surface.hpp>
#include <eng/hud/model.hpp>

namespace eng::hud {

/// Configuración de la franja: geometría del lienzo y color de fondo lógico.
struct HudStripConfig {
	eng::u16 width = 320u;  ///< ancho de la banda en píxeles
	eng::u16 height = 48u;  ///< alto de la banda en líneas (canónico inferior: 48)
	eng::u8 planes = 2u;    ///< planos del lienzo (1..6; 0 = inválido)
	eng::u8 background = 0u; ///< índice de color del fondo (del tema)
};

/// Materializa operaciones del modelo sobre una `Surface` recortada a la región (ruta CPU de
/// referencia). Los textos se copian a un buffer local NUL-terminado; `Blit`/`Sprite`/`Palette`
/// se omiten en H1 (el atlas llega en H6 y los sprites en H4). Devuelve cuántas se materializaron.
[[nodiscard]] inline eng::u16 apply_hud_ops(eng::playfield::Surface& surface,
					    eng::Span<const HudDrawOp> ops) noexcept {
	eng::u16 applied = 0u;
	for (eng::usize i = 0u; i < ops.size(); ++i) {
		const HudDrawOp& op = ops[i];
		if (op.box.empty()) {
			continue;
		}
		switch (op.kind) {
		case HudOpKind::Fill:
			if (surface.fill_rect(op.box.x, op.box.y, op.box.w, op.box.h, op.color)) {
				++applied;
			}
			break;
		case HudOpKind::Frame:
			// Marco de 1 px: cuatro rellenos; con menos de 2 px en un eje, un solo relleno.
			if (op.box.w < 2u || op.box.h < 2u) {
				(void)surface.fill_rect(op.box.x, op.box.y, op.box.w, op.box.h, op.color);
			} else {
				(void)surface.fill_rect(op.box.x, op.box.y, op.box.w, 1u, op.color);
				(void)surface.fill_rect(op.box.x, op.box.bottom(), op.box.w, 1u, op.color);
				(void)surface.fill_rect(op.box.x, static_cast<eng::s16>(op.box.y + 1), 1u,
							static_cast<eng::u16>(op.box.h - 2u), op.color);
				(void)surface.fill_rect(op.box.right(), static_cast<eng::s16>(op.box.y + 1), 1u,
							static_cast<eng::u16>(op.box.h - 2u), op.color);
			}
			++applied;
			break;
		case HudOpKind::Text:
		case HudOpKind::Digits: {
			// El texto del op va por valor (buffer fijo); se copia a un buffer local
			// NUL-terminado para `draw_text`/`draw_text_words`.
			char buf[kHudCounterChars] {};
			const eng::usize limit = static_cast<eng::usize>(kHudCounterChars - 1u);
			const eng::usize n = op.text_len < limit ? op.text_len : limit;
			for (eng::usize c = 0u; c < n; ++c) {
				buf[c] = op.text[c];
			}
			// Texto por **palabras** cuando la celda esta alineada a 16 px (2 glifos de
			// Font8 por word): una escritura por fila y plano en vez de un tramo por
			// glifo. Es la ruta barata para contadores sobre fondo ya limpio.
			const bool word_path = (op.box.x & 15) == 0;
			const bool ok = word_path ? surface.draw_text_words(op.box.x, op.box.y, buf, op.color)
						  : surface.draw_text(op.box.x, op.box.y, buf, op.color);
			if (ok) {
				++applied;
			}
			break;
		}
		case HudOpKind::Blit:
		case HudOpKind::Sprite:
		case HudOpKind::Palette:
			break; // H1: sin atlas ni sprites (H4/H6)
		}
	}
	return applied;
}

/// Presentador de franja: **posee el lienzo** (`CanvasPlayfield`), no la memoria. El bloque Chip
/// lo reserva el llamador y debe vivir más que la franja (el compositor lee su vista).
class HudStrip {
public:
	/// Liga el lienzo al bloque Chip ya reservado y valida la configuración. El bloque debe
	/// cubrir `width/8 * planes * height` bytes (lo comprueba `CanvasPlayfield::bind`).
	[[nodiscard]] bool bind(const eng::Block<eng::PlaneTag>& planes,
				const HudStripConfig& cfg) noexcept {
		if (cfg.width == 0u || cfg.height == 0u || cfg.planes == 0u || cfg.planes > 6u) {
			return false;
		}
		playfield::CanvasPlayfield::Config c {};
		c.width = cfg.width;
		c.height = cfg.height;
		c.planes = cfg.planes;
		if (!m_canvas.bind(planes, c)) {
			return false;
		}
		// **Rellenos del HUD por CPU** (la franja es la ruta CPU de referencia): las
		// operaciones del modelo son rectangulos pequenos/medianos (pips 8x8, deltas de
		// medidor de 2x6, marcos de 1 px) y en un lienzo de 2 planos el arranque del Blitter
		// (~10-14k ciclos por rect, `ATRIBUCION_DE_CICLOS.md` antipatron 3) domina al coste
		// CPU (~100-1000). Con `Auto` el umbral de 64 px manda al Blitter los pips/marcos;
		// con `Cpu` el relleno de la franja completa (clear inicial) tambien va por CPU, que
		// cabe de sobra en el presupuesto del setup. El Blitter queda para las hojas/atlas
		// (H4/H6), que no pasan por `fill_rect`.
		m_canvas.set_raster_policy(playfield::RasterPolicy {
			playfield::AccelMode::Cpu, 0u, true });
		m_cfg = cfg;
		m_ok = true;
		return true;
	}

	/// ¿La franja quedó ligada a un lienzo válido?
	[[nodiscard]] bool ok() const noexcept { return m_ok; }

	/// Configuración vigente.
	[[nodiscard]] const HudStripConfig& config() const noexcept { return m_cfg; }

	/// Rectángulo de la franja en coordenadas de banda (origen 0,0).
	[[nodiscard]] eng::Box bounds() const noexcept {
		return eng::Box {0, 0, m_cfg.width, m_cfg.height};
	}

	/// Superficie recortada a la franja (coordenadas relativas a la banda).
	[[nodiscard]] playfield::Surface surface() noexcept {
		return playfield::Surface {m_canvas,
					   playfield::SurfaceRect {0, 0, m_cfg.width, m_cfg.height}};
	}

	/// Limpia la franja con el color de fondo del tema.
	void clear() noexcept {
		playfield::Surface s = surface();
		(void)s.fill_rect(0, 0, m_cfg.width, m_cfg.height, m_cfg.background);
	}

	/// Aplica las operaciones del modelo (ruta CPU) y devuelve cuántas se materializaron.
	[[nodiscard]] eng::u16 apply(eng::Span<const HudDrawOp> ops) noexcept {
		playfield::Surface s = surface();
		return apply_hud_ops(s, ops);
	}

	/// Lienzo de la franja (el compositor lee su `hardware_view`).
	[[nodiscard]] const playfield::CanvasPlayfield& canvas() const noexcept { return m_canvas; }

private:
	playfield::CanvasPlayfield m_canvas {};
	HudStripConfig m_cfg {};
	bool m_ok = false;
};

} // namespace eng::hud
