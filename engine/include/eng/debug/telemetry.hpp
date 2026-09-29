#pragma once

/// \file telemetry.hpp
/// **Panel de telemetría** dibujado en el overlay del depurador (WinUAE-DBG).
///
/// El overlay (`debug_text`/`debug_filled_rect`) escribe en una capa de depuración que **no**
/// toca la escena del Amiga y no aparece en las capturas de gameplay: es el sitio para ver fps,
/// número de frame y uso de memoria sin ensuciar la demo. `draw_telemetry` recibe cualquier
/// "sink" con `text(s16 x, s16 y, const char* s, u32 rgb)` —p. ej. `AmigaBackend::DebugOverlay`—
/// y compone las líneas con `eng::util::StaticString`/`to_chars_u32` (decimal sin división).
///
/// ```cpp
/// eng::debug::Telemetry t {};
/// t.fps_x100 = 5960u; t.frames = 1234u; t.chip_used = 20480u; t.chip_capacity = 524288u;
/// eng::debug::draw_telemetry(backend.debug(), t, 4, 4, 8, 0xffffffu);
/// ```

#include <eng/core/types/types.hpp>
#include <eng/core/util/static_string.hpp>
#include <eng/core/util/text.hpp>

namespace eng::debug {

/// Capacidad de una línea del panel (etiqueta + `u32/u32` + margen). No cuenta el `\0`.
inline constexpr eng::usize telemetry_line_capacity = 28u;

/// Línea del panel (terminada en `\0` para el sink de texto).
using TelemetryLine = eng::util::StaticString<telemetry_line_capacity + 1u>;

/// Datos del panel. Todo entero (sin coma flotante): `fps_x100` = fps * 100.
struct Telemetry {
	eng::u32 fps_x100 = 0u; ///< fps * 100 (5960 = 59.60)
	eng::u32 frames = 0u;   ///< frames desde el arranque
	eng::u32 chip_used = 0u;
	eng::u32 chip_capacity = 0u;
	eng::u32 slow_used = 0u;
	eng::u32 slow_capacity = 0u;
	eng::u32 fast_used = 0u;
	eng::u32 fast_capacity = 0u;
};

namespace detail {

/// Añade `v` como `int.frac` (2 decimales) a `out`.
inline void put_fixed2(TelemetryLine& out, eng::u32 v) noexcept {
	(void)eng::util::to_chars_u32(out, v / 100u);
	if (!out.append('.')) {
		return;
	}
	const eng::u32 frac = v % 100u;
	(void)out.append(static_cast<char>('0' + (frac / 10u)));
	(void)out.append(static_cast<char>('0' + (frac % 10u)));
}

/// Añade `used/cap` decimal a `out`.
inline void put_used_cap(TelemetryLine& out, eng::u32 used, eng::u32 cap) noexcept {
	(void)eng::util::to_chars_u32(out, used);
	if (!out.append('/')) {
		return;
	}
	(void)eng::util::to_chars_u32(out, cap);
}
} // namespace detail

/// Dibuja el panel en `overlay` (cualquier sink con `text(x, y, cstr, rgb)`).
/// `line` es el alto de línea en píxeles del overlay; `x`/`y` la esquina superior.
template <class Overlay>
void draw_telemetry(Overlay& overlay, const Telemetry& t, eng::s16 x, eng::s16 y,
		    eng::s16 line, eng::u32 rgb) {
	eng::s16 row = y;
	{
		TelemetryLine l {};
		(void)l.append("FPS ");
		detail::put_fixed2(l, t.fps_x100);
		overlay.text(x, row, l.c_str(), rgb);
	}
	row = static_cast<eng::s16>(row + line);
	{
		TelemetryLine l {};
		(void)l.append("FRAME ");
		(void)eng::util::to_chars_u32(l, t.frames);
		overlay.text(x, row, l.c_str(), rgb);
	}
	row = static_cast<eng::s16>(row + line);
	{
		TelemetryLine l {};
		(void)l.append("CHIP ");
		detail::put_used_cap(l, t.chip_used, t.chip_capacity);
		overlay.text(x, row, l.c_str(), rgb);
	}
	row = static_cast<eng::s16>(row + line);
	{
		TelemetryLine l {};
		(void)l.append("SLOW ");
		detail::put_used_cap(l, t.slow_used, t.slow_capacity);
		overlay.text(x, row, l.c_str(), rgb);
	}
	row = static_cast<eng::s16>(row + line);
	{
		TelemetryLine l {};
		(void)l.append("FAST ");
		detail::put_used_cap(l, t.fast_used, t.fast_capacity);
		overlay.text(x, row, l.c_str(), rgb);
	}
}

} // namespace eng::debug
