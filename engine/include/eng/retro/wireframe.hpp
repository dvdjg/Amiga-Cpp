#pragma once

/// \file wireframe.hpp
/// **Trazado de alambre por Blitter** (`eng::retro`, Amiga): dibuja una lista de aristas
/// proyectadas como **líneas de un píxel en modo LINE** (`BLTCON0` = `BC0F_LINE_OR`), con
/// el preludio común fijado **una vez** (`blitter_lines_begin`) y una espera por línea.
///
/// Es el hermano **sin relleno** de `flat_shade_xor`: sirve a mallas arbitrarias (no exige
/// convexidad ni paridad de contorno) y es la técnica del original de `wireframe`
/// (`DrawObject` de `wireframe.c`: los mismos registros, con las horizontales dibujadas).
/// El color de cada arista es una **máscara de planos** (bit `p` = planos en los que se
/// traza), así que con un solo color de pantalla se pasa `1u << plano`.
///
/// Para juegos es el primitivo «dibuja estas aristas en alambre» sobre un lienzo planar:
/// el llamador proyecta sus vértices y entrega aristas ya en coordenadas de pantalla; el
/// engine no impone ni cámara ni formato de malla. Acepta un **clip opcional**
/// (`eng::retro::Rect`, píxeles inclusivos por lado mínimo y exclusivos por máximo, como
/// `lib2d`) para lienzos compartidos: un juego lo activa para no salirse del plano; una
/// demo con geometría garantizada dentro de la ventana lo deja a `nullptr` (coste cero,
/// como el original, que tampoco recortaba).
///
/// ```text
///   ┌─ trazado de alambre (malla arbitraria) ─────────────────────────────┐
///   │ 1. blitter_lines_begin          fija los comunes del modo línea      │
///   │ 2. por arista y plano del color: [clip_line si hay clip] +           │
///   │    blitter_line_prepare + draw  (modo LINE sin ONEDOT; horizontales  │
///   │                                  válidas)                            │
///   │ 3. wait_blitter                 sincroniza antes del swap            │
///   └──────────────────────────────────────────────────────────────────────┘
/// ```
///
/// `Backend` es la capa de plataforma (`AmigaBackend` en Amiga) que aporta las primitivas
/// de Blitter; esta cabecera no conoce registros (igual que `flat_shade_xor.hpp`).

#include <eng/core/types/domains.hpp>
#include <eng/core/types/ptr.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/retro/flat_shade_xor.hpp> // `OutlineEdge` (arista proyectada del layer retro)
#include <eng/retro/lib2d.hpp>          // `Rect` + `clip_line` (clip opcional)

namespace eng::retro {

/// Dibuja las aristas de `edges` en alambre sobre `planes` (bloque contiguo de
/// `plane_count` planos de `plane_bytes` cada uno; filas de `row_bytes`). Devuelve el número
/// de líneas-plano trazadas (0 si no hay aristas). `wait` decide si se espera al Blitter al
/// final (por defecto sí); con `false` el llamador sincroniza más tarde (solape con CPU,
/// patrón de la pipeline de 3 buffers de `flatshade-convex`).
///
/// El color de cada arista es la máscara de planos (`OutlineEdge::color`); los bits fuera de
/// `plane_count` se ignoran. Las aristas **horizontales se dibujan** (a diferencia de la
/// variante EOR de contorno): en un alambre son aristas legítimas.
///
/// `clip` (opcional): ventana de recorte en píxeles; cada arista se recorta con
/// `clip_line` (Liang-Barsky) antes de programar el Blitter y las que caen fuera no se
/// trazan. Con `nullptr` (por defecto) no hay recorte: coste cero (la geometría debe caber
/// en el lienzo, como en el original de `wireframe`).
template <class Backend>
inline eng::u32 wire_lines(Backend& backend, eng::PlaneBytes planes, eng::u16 row_bytes,
			   eng::u32 plane_bytes, eng::u8 plane_count,
			   eng::Span<const OutlineEdge> edges, bool wait = true,
			   eng::Ref<const eng::retro::Rect> clip = nullptr) {
	// Modo COPIA (sin ONEDOT/EOR): aquí NO hay truco de paridad — el canal D va a la
	// propia línea (`d_base = nullptr`) y `blitter_line_draw` lo reescribe POR LÍNEA
	// (en modo línea el Blitter muta el puntero D durante el blit; ver el CONTRATO DE
	// PARIDAD de `flat_shade_xor.hpp` y la regresión de rayas 2026-10-17).
	backend.blitter_lines_begin(row_bytes);
	eng::u8* base = planes.data();
	const eng::u8 color_mask =
		plane_count >= 8u ? 0xffu : static_cast<eng::u8>((1u << plane_count) - 1u);
	eng::u32 lines = 0u;
	for (eng::usize i = 0u; i < edges.size(); ++i) {
		const OutlineEdge& e = edges[i];
		const eng::u8 color = static_cast<eng::u8>(e.color & color_mask);
		if (color == 0u) {
			continue;
		}
		eng::s16 x0 = e.x0, y0 = e.y0, x1 = e.x1, y1 = e.y1;
		if (clip.valid()) {
			// Recorte de LIBRERÍA (opt-in): las aristas que quedan fuera del lienzo
			// compartido se descartan y las parciales entran recortadas (si no, el
			// Blitter en modo línea escribiría fuera del plano).
			eng::retro::Vec2 a = eng::retro::v2(x0, y0);
			eng::retro::Vec2 b = eng::retro::v2(x1, y1);
			if (!eng::retro::clip_line(*clip, a, b)) {
				continue;
			}
			x0 = a.x().v;
			y0 = a.y().v;
			x1 = b.x().v;
			y1 = b.y().v;
		}
		typename Backend::LineEorParams p {};
		if (!backend.blitter_line_prepare(p, row_bytes, x0, y0, x1, y1)) {
			continue;
		}
		for (eng::u8 pl = 0u; pl < plane_count; ++pl) {
			if ((color & (1u << pl)) != 0u) {
				// `d_base = nullptr`: el destino es la propia línea (como el original de
				// wireframe; el truco de la base del bitmap es de la paridad del EOR).
				backend.blitter_line_draw(
					p, base + static_cast<eng::u32>(pl) * plane_bytes, nullptr);
				++lines;
			}
		}
	}
	if (wait) {
		backend.wait_blitter();
	}
	return lines;
}

} // namespace eng::retro
