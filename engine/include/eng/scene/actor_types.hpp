#pragma once

/// \file actor_types.hpp
/// Estado retenido de un actor y su emisión a `FramePlan` / Copper.
///
/// Un actor describe **qué** se ve (contenido portable, posición, prioridad y
/// políticas); el engine decide **cómo** se materializa (`Representation`), y puede
/// reasignarlo sin que la aplicación cambie su descripción.
///
/// Este header cubre el modelo de estado y las políticas; la emisión de hardware
/// pasa por las piezas existentes:
///
///   - `RepresentationAllocator` (`representation.hpp`): elige sprite/BOB/CPU/capa.
///   - `Bob` / `bob_draw` / `bob_erase_box` (`bob.hpp`): dibujo y borrado por Blitter.
///   - `FramePlan` (`frame_plan.hpp`): cola de blits, presupuesto y dirty rects.
///   - `CopperIntent` (`raster_intent.hpp`): necesidades de Copper, que el actor
///     declara RELATIVAS a su borde superior y el compositor convierte a absolutas.
///
/// Contrato y diseño: `docs/engine/architecture/OBJECT_SYSTEM.md`.
///
/// Verificación: lógica y emisión cubiertas por el test host `tests/host/scene/072_actor`
/// (almacén generacional, políticas, geometría, jobs de fondo/dibujo y Copper anclado).
/// **NO VERIFICADA por demo**: todavía no hay una demo con gate visual que lo consuma.

#include <eng/core/types/ptr.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/array.hpp>
#include <eng/core/util/bitset.hpp>
#include <eng/core/util/pool.hpp>
#include <eng/core/util/static_vector.hpp>
#include <eng/graphics/animation.hpp>
#include <eng/graphics/bob.hpp>
#include <eng/graphics/copper/plan.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/raster_intent.hpp>
#include <eng/graphics/sprite.hpp>
#include <eng/graphics/sprite_allocator.hpp>

#include <eng/scene/representation.hpp>

namespace eng::scene {

using eng::copper::Plan;
using eng::graphics::Animation;
using eng::graphics::Bob;
using eng::graphics::BobDraw;
using eng::graphics::BobErase;
using eng::graphics::BobLayout;
using eng::graphics::BobTarget;
using eng::graphics::CopperIntent;
using eng::graphics::DirtyRect;
using eng::graphics::Frame;
using eng::graphics::FramePlan;
using eng::graphics::SpriteAllocator;
using eng::graphics::SpriteIntent;
using eng::graphics::SpritePlacement;
using eng::graphics::SpriteSlot;
using eng::graphics::Visual;
using eng::graphics::VisualKind;

/// Buffers de display soportados por actor (paralelo a los `buffers` de `scene::compose`).
inline constexpr eng::u8 kActorBuffers = 3u;

/// Identificador estable de actor: índice de slot más generación. Un id con la
/// generación vieja deja de ser válido al reciclarse el slot.
struct ActorId {
	eng::u16 index = 0xffffu;
	eng::u16 generation = 0u;

	constexpr bool valid() const { return index != 0xffffu; }
};

/// Modo de transparencia. Es una **política**, no un tipo de objeto: se traduce a un
/// minterm de Blitter y a la necesidad (o no) de plano de máscara.
enum class TransparencyMode : eng::u8 {
	Opaque,     ///< `D = A` (`$F0`): sobrescribe el fondo.
	ColorKey0,  ///< el índice 0 es transparente (requiere máscara derivada).
	Mask1Bit,   ///< cookie-cut `$CA` con plano de máscara.
	AdditiveOr, ///< `D = A | D` (`$FC`): aditivo/glow, sin máscara.
	SubtractiveAnd, ///< `D = A & D` (`$C0`): sombra/sustractivo, sin máscara.
};

/// Política de gestión del fondo bajo el objeto.
enum class BackgroundPolicy : eng::u8 {
	None,      ///< la escena se repinta entera.
	ClearRect, ///< borrar la caja previa (`D = 0`); válido con fondo liso.
	SaveUnder, ///< guardar el fondo y restaurarlo (doble blit); exige buffer.
	DirtyRect, ///< registrar el rectángulo y repintar solo esa región.
	Auto,      ///< el engine elige por área (Small => SaveUnder, grande => ClearRect).
};

/// Punto de anclaje del objeto dentro de su frame (en píxeles con signo). La posición
/// de mundo sitúa el ancla: `esquina = posición − ancla`.
struct Anchor {
	eng::s16 x = 0;
	eng::s16 y = 0;
};

/// Desplazamiento adicional sobre el ancla (sacudidas, retroceso de disparo...).
struct Offset {
	eng::s16 x = 0;
	eng::s16 y = 0;
};

/// Traducción de la política de transparencia al Blitter.
struct TransparencyPlan {
	eng::u8 minterm = 0xcau;
	bool needs_mask = true;
};

constexpr TransparencyPlan transparency_plan(TransparencyMode mode) {
	switch (mode) {
		case TransparencyMode::Opaque:
			return {0xf0u, false};
		case TransparencyMode::ColorKey0:
			return {0xcau, true};
		case TransparencyMode::Mask1Bit:
			return {0xcau, true};
		case TransparencyMode::AdditiveOr:
			return {0xfcu, false};
		case TransparencyMode::SubtractiveAnd:
			return {0xc0u, false};
	}
	return {0xf0u, false};
}

/// Algoritmo de dibujo del Blitter coherente con la transparencia.
constexpr BobDraw bob_draw_for(TransparencyMode mode) {
	switch (mode) {
		case TransparencyMode::AdditiveOr:
			return BobDraw::Or;
		case TransparencyMode::SubtractiveAnd:
			return BobDraw::And;
		case TransparencyMode::Opaque:
			return BobDraw::Opaque;
		case TransparencyMode::ColorKey0:
		case TransparencyMode::Mask1Bit:
			return BobDraw::CookieCut;
	}
	return BobDraw::CookieCut;
}

/// Área (en píxeles) por debajo de la cual `Auto` prefiere `SaveUnder`.
inline constexpr eng::u32 kAutoSaveUnderMaxArea = 48u * 48u;

/// Resuelve `Auto` en una política concreta. Determinista y sin división.
constexpr BackgroundPolicy resolve_background(BackgroundPolicy policy, eng::u16 w, eng::u16 h) {
	if (policy != BackgroundPolicy::Auto) {
		return policy;
	}
	return (static_cast<eng::u32>(w) * static_cast<eng::u32>(h) <= kAutoSaveUnderMaxArea)
		       ? BackgroundPolicy::SaveUnder
		       : BackgroundPolicy::ClearRect;
}

/// Descripción que aporta la aplicación (sin mecanismo ni registros).
struct ActorDesc {
	Visual visual {};
	eng::Ref<const Animation> animation {}; ///< opcional; sin ella el frame es el Visual
	eng::s16 x = 0;                       ///< posición de mundo del ANCLA
	eng::s16 y = 0;
	/// Superficie de la composición donde se dibuja (índice en `ActorEmitContext::targets`):
	/// 0 = playfield de fondo (PF1 en un DPF), 1 = segundo playfield (PF2), etc. Un BOB o
	/// un objeto CPU se puede dibujar en cualquier combinación de playfields.
	eng::u8 surface = 0;
	/// Orden de superposición DENTRO de la superficie: solo compara objetos que se
	/// dibujan sobre el mismo playfield (mayor = delante).
	eng::u8 z = 128;
	/// Prioridad del sprite hardware FRENTE A LOS PLAYFIELDS (0..3, `BPLCON2`); no es
	/// el `z` de los BOBs. Solo aplica si el actor se materializa como sprite.
	eng::u8 sprite_priority = 0;
	Representation preferred = Representation::Sprite;
	TransparencyMode transparency = TransparencyMode::ColorKey0;
	BackgroundPolicy background = BackgroundPolicy::ClearRect;
	Anchor anchor {};
	Offset offset {};
	BobLayout layout = BobLayout::Planar; ///< layout de los planos del contenido
	/// Bytes por fila de la hoja del contenido (0 = contrato compacto). Necesario
	/// cuando los frames son sub-rectángulos de una hoja más ancha.
	eng::u32 sheet_row_bytes = 0u;
	/// Velocidad de animación: `anim_rate_num` ticks de animación por cada
	/// `anim_rate_den` ticks de juego (1/1 = a la misma velocidad).
	eng::u16 anim_rate_num = 1u;
	eng::u16 anim_rate_den = 1u;
	/// Necesidades de Copper ancladas al borde superior del actor (líneas RELATIVAS).
	eng::Span<const CopperIntent> copper {};
	/// Save-under (solo `SaveUnder`): un buffer por buffer de display.
	eng::util::Array<eng::Span<eng::u16>, kActorBuffers> save {};
	eng::u16 save_words_per_row = 0; ///< capacidad de la rejilla de guardado
	eng::u16 save_height = 0;
};

/// Estado de un actor: descripción más lo que cambia con el tiempo.
struct Actor {
	ActorDesc desc {};
	Bob bob {};                  ///< materialización como BOB (misma identidad)
	Animation::State anim {};    ///< frame actual de la animación
	eng::u32 anim_accum = 0;     ///< acumulador de velocidad (aritmética entera)
	Representation actual = Representation::Cpu;
	eng::util::Array<DirtyRect, kActorBuffers> prev {}; ///< rect previo POR BUFFER (save-under/borrado)
};

/// Construye la descripción de BOB de un `Visual`: misma identidad, otra
/// representación (transición sprite -> BOB). El layout de la hoja es el del
/// contenido; si la hoja tiene un stride de fila mayor que el frame, el llamador debe
/// fijar `bob.sheet_row_bytes` después.
inline Bob bob_from_visual(const Visual& v, BobLayout layout, TransparencyMode transparency) {
	Bob b {};
	b.sheet = reinterpret_cast<const eng::u8*>(v.pixels.data());
	b.mask = v.mask.empty() ? nullptr : reinterpret_cast<const eng::u8*>(v.mask.data());
	b.width = v.w;
	b.height = v.h;
	b.planes = v.bitplanes;
	b.frame_count = 1u;
	b.frame_stride = 0u;
	b.layout = layout;
	b.draw = bob_draw_for(transparency);
	b.erase = BobErase::None; // el fondo lo decide la política, no el BOB
	return b;
}

/// Frame vigente: el de la animación, o el Visual completo si no hay animación.
constexpr Frame actor_current_frame(const Actor& a) {
	if (a.desc.animation.valid() && !a.desc.animation->frames.empty()) {
		return a.desc.animation->current(a.anim);
	}
	return Frame {0u, 0u, a.desc.visual.w, a.desc.visual.h, 1u, 0u};
}

/// Avanza la animación `game_ticks` ticks de juego aplicando la velocidad del actor.
/// Devuelve true si cambió el frame. Sin división: acumulador + resta.
inline bool actor_tick(Actor& a, eng::u16 game_ticks) {
	if (!a.desc.animation.valid()) {
		return false;
	}
	const eng::u16 num = a.desc.anim_rate_num != 0u ? a.desc.anim_rate_num : 1u;
	const eng::u16 den = a.desc.anim_rate_den != 0u ? a.desc.anim_rate_den : 1u;
	a.anim_accum += static_cast<eng::u32>(game_ticks) * num;
	// Tope de pasos por llamada: si sobra acumulador se drena en el siguiente tick
	// (evita un bucle largo con velocidades extremas sin recortar el resultado).
	constexpr eng::u32 kMaxSteps = 64u;
	eng::u32 steps = 0;
	while (a.anim_accum >= den && steps < kMaxSteps) {
		a.anim_accum -= den;
		++steps;
	}
	return a.desc.animation->advance(a.anim, static_cast<eng::u16>(steps));
}

/// Rectángulo efectivo en pantalla del frame `f`: posición − ancla + offset − cámara.
constexpr DirtyRect actor_screen_rect(const Actor& a, const Frame& f, eng::s16 cam_x,
				      eng::s16 cam_y) {
	const eng::s16 left = static_cast<eng::s16>(a.desc.x - a.desc.anchor.x + a.desc.offset.x - cam_x);
	const eng::s16 top = static_cast<eng::s16>(a.desc.y - a.desc.anchor.y + a.desc.offset.y - cam_y);
	DirtyRect r {};
	r.left = left;
	r.top = top;
	r.right = static_cast<eng::s16>(left + f.w);
	r.bottom = static_cast<eng::s16>(top + f.h);
	return r;
}

/// Traslación al bitmap anillo: coordenada física de una posición lógica. Si `ring_w`
/// es potencia de 2 se resuelve con una máscara (sin división); con otro tamaño
/// (p. ej. 320) usa el módulo, que es correcto pero paga `__divsi3`: en rutas calientes
/// conviene un anillo potencia de 2.
constexpr eng::s16 ring_physical(eng::s32 logical, eng::u16 ring_w) {
	if (ring_w == 0u) {
		return 0;
	}
	if ((ring_w & (ring_w - 1u)) == 0u) { // potencia de 2: máscara, sin división
		return static_cast<eng::s16>(logical & (static_cast<eng::s32>(ring_w) - 1));
	}
	const eng::s32 size = static_cast<eng::s32>(ring_w);
	eng::s32 v = logical % size;
	if (v < 0) {
		v += size;
	}
	return static_cast<eng::s16>(v);
}

/// ¿`inner` cabe entero en `outer`? (bordes exclusivos).
constexpr bool rect_contains(const DirtyRect& outer, const DirtyRect& inner) {
	return inner.left >= outer.left && inner.top >= outer.top && inner.right <= outer.right &&
	       inner.bottom <= outer.bottom;
}

/// Resultado de emitir un actor.
enum class ActorEmitStatus : eng::u8 {
	Ok,       ///< emitido (0 o más jobs) y dentro del presupuesto.
	Nothing,  ///< no hay nada que dibujar (sin contenido o rect vacío).
	Clipped,  ///< el rect no cabe entero en la ventana: no se emite (recorte parcial pendiente).
	Full,     ///< el plan o el buffer de guardado rechazaron un job (rechazo controlado).
};

/// Contexto de emisión de un frame.
struct ActorEmitContext {
	/// Superficies de dibujo de la composición, indexadas por `ActorDesc::surface`
	/// (p. ej. [0] = PF1/BG, [1] = PF2/FG en un DPF; o un único bitmap suelto).
	eng::Span<const BobTarget> targets {};
	DirtyRect clip {};            ///< ventana visible; si es inválida, no se recorta
	eng::s16 cam_x = 0;
	eng::s16 cam_y = 0;
	eng::u8 buffer = 0;           ///< buffer de display en uso (0..kActorBuffers-1)
	eng::u16 display_top = 0;     ///< línea raster del borde superior del display
};

/// Proyecta el actor sobre el camino de SPRITE hardware (cuando el allocator lo
/// materializa así): franja vertical, X propuesta, ancho (16 px, o 32 con `attach`) y
/// prioridad frente a los playfields. El canal definitivo lo decide `SpriteAllocator`.
inline SpriteIntent actor_to_sprite_intent(const Actor& a, const Frame& f, const DirtyRect& rect,
					   eng::u8 channel = 0u) {
	SpriteIntent it {};
	it.channel = channel;
	it.top = static_cast<eng::u16>(rect.top);
	it.bottom = static_cast<eng::u16>(rect.bottom);
	it.hpos = static_cast<eng::u16>(rect.left);
	it.width_words = static_cast<eng::u8>((f.w + 15u) / 16u);
	it.attach = it.width_words > 1u;
	it.priority = a.desc.sprite_priority;
	return it;
}


} // namespace eng::scene
