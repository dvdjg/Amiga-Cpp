#pragma once

/// \file present_controller.hpp
/// **PresentController**: la pieza de presentación de la fase F2 — junta la **política de
/// paneo** (`pan_window.hpp`), el **pacing medido** (`render_pacing.hpp`) y la
/// **publicación diferida al inicio de campo**, y decide por frame qué se muestra (Full
/// publicado + desplazamiento de ventana) y qué se rendiza (pasada completa o nada).
///
/// La **publicación diferida** es la clave de la presentación limpia: el buffer de un Full
/// renderizado se muestra en el update SIGUIENTE (que corre tras el VBlank, antes de que el
/// haz llegue a los `MOVE` de punteros de la caja de copper), de modo que el frame
/// presentado es **entero** y desaparece el corte a mitad de campo que sale al parchear
/// nada más terminar un render que desborda el campo (DT-034). El offset de display del
/// propio Full es el paneo avanzado desde su render (0..margen), y los Fakes intermedios
/// desplazan la ventana sobre el **mismo buffer** del último Full publicado.
///
/// **Camino asíncrono (condicional al hardware)**: `async_ok` se activa solo si hay
/// **Fast RAM** (`eng::hw::has_fast_ram`): es el único escenario donde el solape de la cola
/// de blits por IRQ del Blitter (`AmigaBackend::enable_blitter_queue`, INTF_BLIT → nivel 3)
/// tiene CPU que solapar — veredicto medido en
/// `docs/debugging/investigaciones/218-campana-rendimiento.md` (268k vs 221k sin trabajo
/// solapable; el encolado en Slow costaba ~6k/job con el Blitter en marcha). En A500 sin
/// Fast RAM el controlador deja el camino síncrono.

#include <eng/core/types/types.hpp>
#include <eng/graphics/pan_window.hpp>
#include <eng/graphics/render_pacing.hpp>
#include <eng/hw/info.hpp>

namespace eng::graphics {

/// Decisión de un frame de presentación (lo que el llamador debe hacer).
struct PresentStep {
	bool full = false;    ///< renderizar una pasada completa este frame (y luego `commit`)
	bool publish = false; ///< publicar el Full pendiente (al INICIO del update, antes del patch)
	bool async_ok = false; ///< el camino asíncrono (cola IRQ) es aconsejable en esta máquina
	eng::s16 offset_x = 0; ///< ventana del frame PRESENTADO (px respecto al centro), clampeada
};

/// Configuración del controlador (política base y tope de fakes).
struct PresentControllerConfig {
	PanWindowPolicy policy {}; ///< márgenes y racha base (la racha se re-ajusta por coste)
	eng::u8 fake_cap = 3u;     ///< tope de fotogramas falsos seguidos (tolerancia visual)
};

/// Controlador de presentación (sin hardware: estado + decisión; el llamador aplica).
class PresentController {
public:
	/// Configura con la política de la escena y el **inventario de hardware** de la máquina
	/// (de él sale `async_ok`).
	constexpr void configure(const PresentControllerConfig& cfg, const eng::hw::HwInfo& hw) {
		m_cfg = cfg;
		m_policy = cfg.policy;
		m_async_ok = eng::hw::has_fast_ram(hw);
	}

	/// Ancla el estado al paneo inicial (una vez, tras configurar).
	constexpr void reset(eng::s32 pan) {
		m_pan_state = PanWindowState {};
		m_pending = false;
		m_pending_pan = pan;
		m_shown_pan = pan;
	}

	/// Decide el frame para el paneo `pan`: llamar AL INICIO del update. Si `publish`, el
	/// llamador muestra el buffer pendiente (cambio de punteros/ventana en ese orden);
	/// si `full`, renderizará la pasada completa y cerrará con `commit(pan)`.
	[[nodiscard]] constexpr PresentStep begin(eng::s32 pan) {
		PresentStep out {};
		if (m_pending) {
			m_shown_pan = m_pending_pan;
			m_pending = false;
			out.publish = true;
		}
		// Ventana del frame presentado: el paneo avanzado respecto al Full publicado,
		// clampeado al margen de la política (S en 1..31 == off −15..+15 con centro 16).
		eng::s32 off = pan - m_shown_pan;
		const eng::s16 margin = static_cast<eng::s16>(m_policy.margin_x);
		if (off > margin) {
			off = margin;
		}
		if (off < -margin) {
			off = -margin;
		}
		out.offset_x = static_cast<eng::s16>(off);
		out.async_ok = m_async_ok;
		const PanWindowStep st = pan_window_step(m_pan_state, pan, 0, m_policy);
		out.full = st.full;
		return out;
	}

	/// Cierra el render de un frame en el que `begin` pidió `full`: el buffer renderizado
	/// queda pendiente y se publicará en el `begin` del frame siguiente.
	constexpr void commit(eng::s32 pan) {
		m_pending = true;
		m_pending_pan = pan;
	}

	/// Coste medido de la pasada completa (ciclos): re-ajusta la racha adaptativa de fakes
	/// para no bajar de la cadencia base (`RenderPacing::affordable_fakes`).
	constexpr void observe_full(eng::u32 full_cycles) {
		m_pacing.observe(full_cycles);
		m_policy.max_consecutive_fakes = m_pacing.affordable_fakes(m_cfg.fake_cap);
	}

	[[nodiscard]] constexpr bool pending() const { return m_pending; }
	[[nodiscard]] constexpr eng::s32 shown_pan() const { return m_shown_pan; }
	[[nodiscard]] constexpr bool async_ok() const { return m_async_ok; }
	[[nodiscard]] constexpr eng::u8 max_consecutive_fakes() const {
		return m_policy.max_consecutive_fakes;
	}
	[[nodiscard]] constexpr const RenderPacing& pacing() const { return m_pacing; }

private:
	PresentControllerConfig m_cfg {};
	PanWindowState m_pan_state {};
	PanWindowPolicy m_policy {};
	RenderPacing m_pacing {};
	bool m_async_ok = false;
	bool m_pending = false;
	eng::s32 m_pending_pan = 0;
	eng::s32 m_shown_pan = 0;
};

} // namespace eng::graphics
