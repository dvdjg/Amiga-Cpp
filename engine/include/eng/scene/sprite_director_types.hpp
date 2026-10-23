#pragma once

/// \file sprite_director_types.hpp
/// Tipos públicos de la fachada SpriteDirector: SpriteId, SpriteDesc y las
/// conversiones de reporte/banda. Se separan del director para mantener la cabecera del
/// algoritmo por debajo del umbral de tamaño y permitir reutilizarlos sin arrastrar la
/// implementación del pool.

#include <eng/api/objects.hpp>
#include <eng/api/sprites.hpp>
#include <eng/graphics/copper/plan.hpp>
#include <eng/graphics/sprite_line_layer.hpp>
#include <eng/scene/sprite_visual.hpp>

namespace eng::scene {

/// Identificador generacional de una entidad del director.
struct SpriteId {
	/// Índice de slot en el pool interno; `0xffff` = id inválido (nunca asignado).
	eng::u16 index = 0xffffu;
	/// Generación del slot: sube al reciclar; un id con generación vieja deja de ser
	/// válido aunque el índice se reutilice.
	eng::u16 generation = 0u;
	/// `true` si fue devuelto por `SpriteDirector::add` y no se ha invalidado por índice.
	[[nodiscard]] constexpr bool valid() const noexcept { return index != 0xffffu; }
	[[nodiscard]] constexpr bool operator==(const SpriteId& o) const noexcept {
		return index == o.index && generation == o.generation;
	}
	[[nodiscard]] constexpr bool operator!=(const SpriteId& o) const noexcept {
		return !(*this == o);
	}
};

/// Descripción de una entidad para el director: el contenido viaja por handles/vistas y
/// el llamador mantiene vivos los buffers referenciados.
struct SpriteDesc {
	/// Contenido de la entidad: `simple(VisualId)` o `composite_visual(CompositeId)`.
	/// El id debe seguir vivo en `SceneAssets` durante toda la vida de la entidad; si no,
	/// `add` devuelve un `SpriteId` inválido.
	SpriteVisualRef content {};
	/// Intención de reparto/emisión (política, attached, gap, canal, rank,
	/// tira horizontal). Se copia en la entidad; los cambios posteriores
	/// en el `SpriteDesc` original no la actualizan.
	SpriteHint hint {};
	/// X de **pantalla** en píxeles low-res (0..319 típico). Para sprite HW el director
	/// suma `kSpriteScreenXOrigin` al construir el `ActorDesc`; para BOB/`force_bob` se
	/// usa tal cual. Puede ser negativa (recorte del lado izquierdo).
	eng::s16 x = 0;
	/// Y de **pantalla** en líneas raster (0..255 típico); es también el `vstart` del
	/// sprite. Puede ser negativa (recorte superior); el compositor recorta si procede.
	eng::s16 y = 0;
	/// Superficie de la composición (índice en `ActorEmitContext::targets`); 0 es la
	/// primera. Los compuestos usan el target 0 en esta fachada.
	eng::u8 surface = 0u;
	/// Orden de superposición dentro de la superficie (mayor = delante); no es la
	/// prioridad de sprite frente a playfields (esa es `hint.sprite_priority`).
	eng::u8 z = 128u;
	/// Paleta opcional del actor, indexada por registro `COLORxx` (32 entradas para
	/// cubrir `COLOR16..31`). Vacía = sin declaración; una vista corta se ignora en la
	/// composición. La vista debe sobrevivir a la entidad.
	eng::PaletteWords sprite_palette {};
	/// Necesidades de Copper ancladas al borde superior del actor, en líneas
	/// **relativas** (el compositor las convierte a absolutas con `display_top + y`).
	/// La vista debe sobrevivir a la entidad; vacía = sin Copper propio.
	eng::Span<const eng::graphics::CopperIntent> copper {};
	/// Plantilla de franjas opcional (`Reuse::Chain`/`PerLine`): bitmap Chip + segmentos
	/// + switches. Vacía = sprite normal. La vista debe sobrevivir a la entidad.
	eng::graphics::HwSpriteTemplateView sprite_template {};
	/// Animación simple opcional (frames de la hoja); vacía = frame estático. El estado
	/// de animación vive en el `Actor` interno y avanza con `tick`.
	eng::Ref<const eng::graphics::Animation> animation {};
	/// Capa avanzada opcional para `SpriteReuse::PerLine` (el llamador la mantiene viva).
	eng::Ref<const eng::graphics::SpriteLineLayer> line_layer {};
	/// Política de transparencia del BOB derivado/forzado. Para sprite HW el color 0 es
	/// transparente por hardware; para BOB `Mask1Bit`/`ColorKey0` exigen máscara.
	TransparencyMode transparency = TransparencyMode::ColorKey0;
	/// Política de borrado/restauración del BOB (save-under, clear rect o ninguna).
	BackgroundPolicy background = BackgroundPolicy::ClearRect;
	/// Layout de la hoja para el camino BOB (`Planar` o `Interleaved`). El camino sprite
	/// siempre espera DAT/DATB intercalado por línea.
	BobLayout layout = BobLayout::Planar;
	/// Bytes por fila de la hoja BOB si no es el contrato compacto (0 = derivarlo del
	/// ancho/planos). Para una hoja planar compacta de 16 px suele ser 2.
	eng::u32 sheet_row_bytes = 0u;
	/// Ancla dentro del frame en píxeles con signo (0,0 = esquina superior izquierda).
	Anchor anchor {};
	/// Desplazamiento adicional sobre el ancla (sacudidas, retroceso...), en píxeles.
	Offset offset {};
	/// Ticks de animación por cada `anim_rate_den` ticks de juego (1/1 = misma velocidad).
	eng::u16 anim_rate_num = 1u;
	/// Denominador de la velocidad de animación; 0 se trata como 1.
	eng::u16 anim_rate_den = 1u;
	/// Secuencia inicial del compuesto (índice en `CompositeVisual::sequences`); 0 si no
	/// hay secuencias. Para contenido simple se ignora.
	eng::u8 sequence = 0u;
	/// Espejo horizontal inicial del compuesto; para contenido simple se ignora.
	bool facing_left = false;
};

/// Informe de paletas por banda → intents/eventos. `playfield` va a `Plan`; `sprites` se
/// devuelve como eventos para fusionarlos con los rearmes de sprites.
/// \param bands         Bandas a emitir. `top`/`scroll_y` se suman; `height` es el tramo
///                      visible en líneas. Las bandas se procesan en el orden del span y
///                      se avisa de solapes entre ellas.
/// \param plan          Plan de Copper destino para `playfield` (puede ser nulo; entonces
///                      esas bandas cuentan como fuera de rango y `ok == false`).
/// \param sprite_out    Buffer de salida para los eventos de paleta de sprites; los
///                      sobrantes se omiten y marcan `ok == false`.
/// \param display_top   Primera línea visible del display (para validar el rango).
/// \param display_height Alto visible en líneas; `top + height` debe caber en
///                      `[display_top, display_top + display_height)`.
/// \return Contadores de bandas/cues/eventos/solapes/fuera de rango y `ok`; no toca
///         hardware directamente, solo añade intents al `Plan`.
[[nodiscard]] inline BandPaletteReport emit_band_palettes(
	eng::Span<const BandPalette> bands, eng::Ref<eng::copper::Plan> plan,
	eng::Span<eng::graphics::SpritePaletteEvent> sprite_out,
	eng::u16 display_top = 0u, eng::u16 display_height = 512u) noexcept {
	BandPaletteReport r {};
	r.bands = static_cast<eng::u16>(bands.size());
	for (eng::usize i = 0u; i < bands.size(); ++i) {
		const BandPalette& b = bands[i];
		const eng::s32 signed_line = band_palette_line_signed(b);
		const eng::u16 line = band_palette_line(b);
		const eng::u16 height = b.height;
		if (signed_line < 0 || signed_line > 511 ||
		    static_cast<eng::u32>(signed_line) + height > 512u ||
		    line < display_top ||
		    static_cast<eng::u32>(line) + height > static_cast<eng::u32>(display_top) + display_height) {
			++r.out_of_range;
			r.ok = false;
		}
		// Solape con una banda anterior (cualquier registro): se avisa, no se silencia.
		for (eng::usize j = 0u; j < i; ++j) {
			const eng::s32 a0 = band_palette_line_signed(bands[j]);
			const eng::s32 a1 = a0 + static_cast<eng::s32>(bands[j].height);
			const eng::s32 b0 = signed_line;
			const eng::s32 b1 = signed_line + static_cast<eng::s32>(height);
			if (a0 < b1 && b0 < a1) {
				++r.overlaps;
				r.ok = false;
				break;
			}
		}
		if (!b.playfield.empty()) {
			const eng::usize count = b.playfield.size() > 32u ? 32u : b.playfield.size();
			if (plan.valid()) {
				eng::graphics::CopperIntent ci {};
				ci.kind = eng::graphics::CopperIntentKind::PaletteLine;
				ci.top = line;
				ci.bottom = static_cast<eng::u16>(line + height);
				ci.colors = b.playfield.first(count);
				ci.first = 0u;
				ci.count = static_cast<eng::u8>(count);
				plan->add(ci);
			} else {
				r.ok = false;
			}
			++r.playfield_events;
			++r.cues;
		}
		if (!b.sprites.empty()) {
			const eng::usize count = b.sprites.size() > 16u ? 16u : b.sprites.size();
			if (r.sprite_events < sprite_out.size()) {
				eng::graphics::SpritePaletteEvent ev {};
				ev.line = line;
				ev.colors = b.sprites.first(count);
				ev.first = 16u;
				sprite_out[r.sprite_events] = ev;
				++r.sprite_events;
				++r.cues;
			} else {
				r.ok = false;
			}
		}
		r.lines = static_cast<eng::u16>(r.lines + height);
	}
	return r;
}

} // namespace eng::scene
