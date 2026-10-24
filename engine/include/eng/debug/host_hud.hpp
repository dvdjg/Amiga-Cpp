#pragma once

/// \file host_hud.hpp
/// **HUD de depuración del host** reutilizable: nombre + número de demo, versión,
/// contador de frames renderizados y fps. Lo compone WinUAE en el lado host
/// (`app.debug().text`) y no consume bitplanes ni DMA del Amiga.
///
/// Es la vía recomendada por `AGENTS.md` §0 y `API_ERGONOMICS.md` §6; las demos no
/// duplican el formato ni el cálculo de fps.

#include <eng/core/types/types.hpp>
#include <eng/core/util/text_line.hpp>
#include <eng/debug/prof.hpp>
#include <eng/debug/version.hpp>

namespace eng::debug {

/// Estado y formato del HUD de host. El llamador conserva una instancia y llama a
/// `draw` una vez por frame (o con el frame renderizado que quiera mostrar).
class HostHud {
public:
	/// Escribe el HUD en el overlay de depuración del host.
	/// \tparam DebugOverlay tipo con `clear()` y `text(x,y,texto,rgb)` (p. ej. el
	///         `DebugOverlay` del backend Amiga).
	/// \param dbg       overlay de destino; se limpia si `clear` es `true`.
	/// \param name      nombre corto de la demo/juego (incluye su número).
	/// \param version   versión `major.minor.build` (el build lo inyecta el build script).
	/// \param frame     contador de frames **renderizados** (no VBlanks del bucle).
	/// \param cycle_now reloj de ciclos PAL (`prof_clock()`); `0` desactiva el cálculo
	///                  de fps y deja el último valor.
	/// \param stats     línea opcional de telemetría propia; `nullptr`/vacío = sin línea.
	/// \param clear     `true` limpia el overlay antes de escribir; `false` respeta el
	///                  contenido previo (útil para añadir el HUD después de otra telemetría).
	template <class DebugOverlay>
	void draw(DebugOverlay& dbg, const char* name, const Version& version, eng::u32 frame,
		  eng::u32 cycle_now, const char* stats = nullptr, bool clear = true) noexcept {
		eng::u32 fps10 = 0u;
		if (m_fps.update(frame, cycle_now, fps10)) {
			m_fps10 = fps10;
		}
		char label[16] {};
		const eng::u8 len = version_label(version, label, static_cast<eng::u8>(sizeof(label)));
		if (clear) {
			dbg.clear();
		}
		// Una sola línea y una sola primitiva: el overlay de WinUAE recorta
		// peor las líneas apiladas al redimensionar. El separador `|` mantiene
		// toda la información útil (nombre, frame/fps, telemetría) visible.
		m_block.clear();
		m_block << (name != nullptr ? name : "") << ' '
			<< eng::util::StringView(label, len)
			<< " | F" << frame << ' ' << eng::util::scaled(m_fps10, 1u) << "FPS";
		if (stats != nullptr && *stats != '\0') {
			m_block << " | " << stats;
		}
		dbg.text(kMarginX, kMarginY, m_block.c_str(), 0xffffffu);
	}

	/// Última estimación de fps ×10 (p. ej. 275 = 27,5 fps); 0 hasta la primera ventana.
	[[nodiscard]] constexpr eng::u32 fps10() const noexcept { return m_fps10; }

	/// Margen seguro del HUD de una línea.
	static constexpr eng::s16 kMarginX = 8;
	static constexpr eng::s16 kMarginY = 8;

private:
	HudFpsWindow m_fps {};          ///< ventana de ~1 s para fps10
	eng::u32 m_fps10 = 0u;          ///< última estimación fps ×10
	eng::util::TextLine<192> m_block {}; ///< línea única enviada al overlay
};

} // namespace eng::debug
