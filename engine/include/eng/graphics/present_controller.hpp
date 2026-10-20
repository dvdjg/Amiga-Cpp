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
	eng::s16 offset_y = 0; ///< paneo vertical de la ventana, en filas completas, clampeado
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

	/// Ancla el estado al paneo inicial (una vez, tras configurar); `pan_y` se expresa en filas.
	constexpr void reset(eng::s32 pan_x, eng::s32 pan_y = 0) {
		m_pan_state = PanWindowState {};
		m_pending = false;
		m_pending_pan_x = pan_x;
		m_pending_pan_y = pan_y;
		m_shown_pan_x = pan_x;
		m_shown_pan_y = pan_y;
	}

	/// Decide el frame para el paneo `(pan_x, pan_y)`: llamar AL INICIO del update. Si `publish`, el
	/// llamador muestra el buffer pendiente (cambio de punteros/ventana en ese orden); si `full`,
	/// renderiza la pasada completa y cierra con `commit(pan_x, pan_y)`. Omitir `pan_y` conserva el
	/// contrato horizontal existente.
	[[nodiscard]] constexpr PresentStep begin(eng::s32 pan_x, eng::s32 pan_y = 0) {
		PresentStep out {};
		if (m_pending) {
			m_shown_pan_x = m_pending_pan_x;
			m_shown_pan_y = m_pending_pan_y;
			m_pending = false;
			out.publish = true;
		}
		// La ventana mostrada es el delta respecto a ambos ejes del Full publicado; OCS desplaza
		// verticalmente solo en scanlines completas, mientras que X también admite fine-scroll.
		eng::s32 off_x = pan_x - m_shown_pan_x;
		eng::s32 off_y = pan_y - m_shown_pan_y;
		const eng::s16 margin_x = m_policy.margin_x;
		const eng::s16 margin_y = m_policy.margin_y;
		if (off_x > margin_x) {
			off_x = margin_x;
		}
		if (off_x < -margin_x) {
			off_x = -margin_x;
		}
		if (off_y > margin_y) {
			off_y = margin_y;
		}
		if (off_y < -margin_y) {
			off_y = -margin_y;
		}
		out.offset_x = static_cast<eng::s16>(off_x);
		out.offset_y = static_cast<eng::s16>(off_y);
		out.async_ok = m_async_ok;
		const PanWindowStep st = pan_window_step(m_pan_state, pan_x, pan_y, m_policy);
		out.full = st.full;
		return out;
	}

	/// Cierra el render Full: el buffer queda pendiente y se publica en el `begin` siguiente.
	constexpr void commit(eng::s32 pan_x, eng::s32 pan_y = 0) {
		m_pending = true;
		m_pending_pan_x = pan_x;
		m_pending_pan_y = pan_y;
	}

	/// Coste medido de la pasada completa (ciclos): re-ajusta la racha adaptativa de fakes
	/// para no bajar de la cadencia base (`RenderPacing::affordable_fakes`).
	constexpr void observe_full(eng::u32 full_cycles) {
		m_pacing.observe(full_cycles);
		m_policy.max_consecutive_fakes = m_pacing.affordable_fakes(m_cfg.fake_cap);
	}

	[[nodiscard]] constexpr bool pending() const { return m_pending; }
	/// Paneo horizontal del Full que actualmente lee el display.
	[[nodiscard]] constexpr eng::s32 shown_pan() const { return m_shown_pan_x; }
	/// Paneo horizontal del Full que actualmente lee el display.
	[[nodiscard]] constexpr eng::s32 shown_pan_x() const { return m_shown_pan_x; }
	/// Paneo vertical (filas) del Full que actualmente lee el display.
	[[nodiscard]] constexpr eng::s32 shown_pan_y() const { return m_shown_pan_y; }
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
	eng::s32 m_pending_pan_x = 0;
	eng::s32 m_pending_pan_y = 0;
	eng::s32 m_shown_pan_x = 0;
	eng::s32 m_shown_pan_y = 0;
};

} // namespace eng::graphics
