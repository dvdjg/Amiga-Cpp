#pragma once

/// \file actor.hpp
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
/// Verificación: lógica y emisión cubiertas por el test host `tests/host/072_actor`
/// (almacén generacional, políticas, geometría, jobs de fondo/dibujo y Copper anclado).
/// **NO VERIFICADA por demo**: todavía no hay una demo con gate visual que lo consuma.

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/graphics/animation.hpp>
#include <eng/graphics/bob.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/raster_intent.hpp>

#include <eng/scene/representation.hpp>

namespace eng::scene {

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
using eng::graphics::Visual;
using eng::graphics::VisualKind;

/// Buffers de display soportados por actor (paralelo a `MultiBuffered<Driver,N>`).
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
	}
	return {0xf0u, false};
}

/// Algoritmo de dibujo del Blitter coherente con la transparencia.
constexpr BobDraw bob_draw_for(TransparencyMode mode) {
	switch (mode) {
		case TransparencyMode::AdditiveOr:
			return BobDraw::Or;
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
	const Animation* animation = nullptr; ///< opcional; sin ella el frame es el Visual
	eng::s16 x = 0;                       ///< posición de mundo del ANCLA
	eng::s16 y = 0;
	eng::u8 z = 128;                      ///< orden global (mayor = delante)
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
	eng::Span<eng::u16> save[kActorBuffers] {};
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
	DirtyRect prev[kActorBuffers] {}; ///< rect previo POR BUFFER (save-under/borrado)
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
	if (a.desc.animation != nullptr && !a.desc.animation->frames.empty()) {
		return a.desc.animation->current(a.anim);
	}
	return Frame {0u, 0u, a.desc.visual.w, a.desc.visual.h, 1u, 0u};
}

/// Avanza la animación `game_ticks` ticks de juego aplicando la velocidad del actor.
/// Devuelve true si cambió el frame. Sin división: acumulador + resta.
inline bool actor_tick(Actor& a, eng::u16 game_ticks) {
	if (a.desc.animation == nullptr) {
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
	BobTarget target {};          ///< bitmap destino (el buffer trasero)
	DirtyRect clip {};            ///< ventana visible; si es inválida, no se recorta
	eng::s16 cam_x = 0;
	eng::s16 cam_y = 0;
	eng::u8 buffer = 0;           ///< buffer de display en uso (0..kActorBuffers-1)
	eng::u16 display_top = 0;     ///< línea raster del borde superior del display
};

namespace actor_detail {

inline bool emit_save(FramePlan& plan, const Actor& a, const DirtyRect& r, const ActorEmitContext& ctx) {
	const eng::Span<eng::u16> save = a.desc.save[ctx.buffer];
	const eng::u16 words = static_cast<eng::u16>((r.width() + 15u) / 16u);
	const eng::u16 h = r.height();
	if (save.empty() || words > a.desc.save_words_per_row || h > a.desc.save_height) {
		return false; // sin buffer o buffer insuficiente: rechazo controlado
	}
	const eng::u32 save_row_bytes = static_cast<eng::u32>(a.desc.save_words_per_row) * 2u;
	eng::graphics::BlitJob job {};
	job.destination = {save.data()};
	job.source = {reinterpret_cast<const eng::u16*>(
		ctx.target.base + static_cast<eng::u32>(r.top) * ctx.target.row_bytes +
		(static_cast<eng::u32>(r.left & ~15) >> 3u))};
	job.words_per_row = words;
	job.height = h;
	job.bitplane_count = a.bob.planes;
	job.source_modulo_bytes = static_cast<eng::s16>(ctx.target.row_bytes - static_cast<eng::u32>(words) * 2u);
	job.destination_modulo_bytes = static_cast<eng::s16>(save_row_bytes - static_cast<eng::u32>(words) * 2u);
	job.source_plane_stride_bytes = ctx.target.plane_bytes;
	job.destination_plane_stride_bytes = save_row_bytes * a.desc.save_height;
	return plan.add_copy_rect(job);
}

inline bool emit_restore(FramePlan& plan, const Actor& a, const DirtyRect& r,
			 const ActorEmitContext& ctx) {
	const eng::Span<eng::u16> save = a.desc.save[ctx.buffer];
	const eng::u16 words = static_cast<eng::u16>((r.width() + 15u) / 16u);
	const eng::u16 h = r.height();
	if (save.empty() || words > a.desc.save_words_per_row || h > a.desc.save_height) {
		return false;
	}
	const eng::u32 save_row_bytes = static_cast<eng::u32>(a.desc.save_words_per_row) * 2u;
	eng::graphics::BlitJob job {};
	job.source = {save.data()};
	job.destination = {reinterpret_cast<eng::u16*>(
		ctx.target.base + static_cast<eng::u32>(r.top) * ctx.target.row_bytes +
		(static_cast<eng::u32>(r.left & ~15) >> 3u))};
	job.words_per_row = words;
	job.height = h;
	job.bitplane_count = a.bob.planes;
	job.source_modulo_bytes = static_cast<eng::s16>(save_row_bytes - static_cast<eng::u32>(words) * 2u);
	job.destination_modulo_bytes = static_cast<eng::s16>(ctx.target.row_bytes - static_cast<eng::u32>(words) * 2u);
	job.source_plane_stride_bytes = save_row_bytes * a.desc.save_height;
	job.destination_plane_stride_bytes = ctx.target.plane_bytes;
	return plan.add_restore_rect(job);
}

/// Dibuja el frame `f` del actor como BOB, con el origen del frame dentro de la hoja.
inline bool emit_bob(FramePlan& plan, const Actor& a, const Frame& f, eng::s16 x, eng::s16 y,
		     const BobTarget& target) {
	using eng::graphics::bob_detail::sheet_row_of;
	Bob b = a.bob;
	const eng::u32 row = sheet_row_of(a.bob); // stride de la hoja (antes de cambiar w/h)
	b.sheet += static_cast<eng::u32>(f.y) * row + (static_cast<eng::u32>(f.x) >> 4u) * 2u;
	b.width = f.w;
	b.height = f.h;
	b.frame_count = 1u;
	b.frame_stride = 0u;
	return eng::graphics::bob_draw(plan, b, 0u, x, y, target);
}

} // namespace actor_detail

/// Emite el actor para el frame actual: fondo (borrado o save-under) y objeto. El rect
/// efectivo queda en `out_rect`. Las necesidades de Copper ancladas las escribe el
/// llamador con `actor_emit_copper` (usando el rect devuelto), para que quepan en su
/// propio buffer de intenciones.
inline ActorEmitStatus actor_emit(FramePlan& plan, Actor& a, const ActorEmitContext& ctx,
				  DirtyRect* out_rect = nullptr) {
	if (a.desc.visual.pixels.empty() || a.desc.visual.w == 0u || a.desc.visual.h == 0u) {
		return ActorEmitStatus::Nothing;
	}
	if (ctx.buffer >= kActorBuffers) {
		return ActorEmitStatus::Full;
	}
	const Frame f = actor_current_frame(a);
	const DirtyRect rect = actor_screen_rect(a, f, ctx.cam_x, ctx.cam_y);
	if (!rect.valid()) {
		return ActorEmitStatus::Nothing;
	}
	if (ctx.clip.valid() && !rect_contains(ctx.clip, rect)) {
		return ActorEmitStatus::Clipped; // recorte parcial: pendiente (ver OBJECT_SYSTEM.md)
	}

	const BackgroundPolicy policy = resolve_background(a.desc.background, f.w, f.h);
	const DirtyRect& prev = a.prev[ctx.buffer];

	switch (policy) {
		case BackgroundPolicy::ClearRect: {
			if (prev.valid() &&
			    !eng::graphics::bob_erase_box(plan, a.bob, prev.width(), prev.height(),
							  prev.left, prev.top, ctx.target)) {
				return ActorEmitStatus::Full;
			}
			break;
		}
		case BackgroundPolicy::SaveUnder: {
			// El save-under cuenta con planos contiguos en el destino.
			if (ctx.target.layout != BobLayout::Planar) {
				return ActorEmitStatus::Full;
			}
			if (prev.valid() && !actor_detail::emit_restore(plan, a, prev, ctx)) {
				return ActorEmitStatus::Full;
			}
			if (!actor_detail::emit_save(plan, a, rect, ctx)) {
				return ActorEmitStatus::Full;
			}
			break;
		}
		case BackgroundPolicy::None:
		case BackgroundPolicy::DirtyRect:
		case BackgroundPolicy::Auto:
			break;
	}

	if (!actor_detail::emit_bob(plan, a, f, rect.left, rect.top, ctx.target)) {
		return ActorEmitStatus::Full;
	}

	a.prev[ctx.buffer] = rect;
	plan.add_dirty_rect(rect);
	if (out_rect != nullptr) {
		*out_rect = rect;
	}
	return plan.ok() ? ActorEmitStatus::Ok : ActorEmitStatus::Full;
}

/// Escribe las necesidades de Copper del actor como líneas ABSOLUTAS, dado su borde
/// superior de pantalla y la primera línea del display. Devuelve cuántas escribió.
inline eng::u8 actor_emit_copper(const Actor& a, eng::s16 screen_y, eng::u16 display_top,
				 CopperIntent* out, eng::u8 capacity) {
	if (out == nullptr) {
		return 0u;
	}
	const eng::s32 base = static_cast<eng::s32>(display_top) + screen_y;
	eng::u8 n = 0;
	for (const CopperIntent& need : a.desc.copper) {
		if (n >= capacity) {
			break;
		}
		CopperIntent abs = need;
		abs.top = static_cast<eng::u16>(base + need.top);
		abs.bottom = static_cast<eng::u16>(base + need.bottom);
		out[n++] = abs;
	}
	return n;
}

/// Almacén de actores de capacidad fija con handles generacionales. Sin heap.
template <eng::u16 MaxActors>
class ActorStore {
public:
	static constexpr eng::u16 kMax = MaxActors;

	constexpr void reset() {
		for (eng::u16 i = 0; i < MaxActors; ++i) {
			m_next[i] = static_cast<eng::u16>(i + 1u);
			m_used[i] = false;
		}
		m_next[MaxActors - 1u] = 0xffffu;
		m_free_head = 0u;
		m_count = 0u;
	}

	constexpr eng::u16 count() const { return m_count; }
	constexpr bool full() const { return m_free_head == 0xffffu; }

	/// Alta de un actor. Consume presupuesto del allocator. Devuelve un id inválido si
	/// no queda capacidad o si la descripción no tiene contenido.
	ActorId add(const ActorDesc& desc, RepresentationAllocator& alloc) {
		if (m_free_head == 0xffffu || desc.visual.pixels.empty() || desc.visual.w == 0u ||
		    desc.visual.h == 0u) {
			return {};
		}
		const eng::u16 index = m_free_head;
		m_free_head = m_next[index];
		Actor& a = m_actors[index];
		a = Actor {};
		a.desc = desc;
		a.bob = bob_from_visual(desc.visual, desc.layout, desc.transparency);
		a.bob.sheet_row_bytes = desc.sheet_row_bytes;
		ActorTemplate tmpl {};
		tmpl.width = desc.visual.w;
		tmpl.height = desc.visual.h;
		tmpl.planes = desc.visual.bitplanes;
		tmpl.preferred = desc.preferred;
		tmpl.priority = desc.z;
		tmpl.scrolls = false;
		a.actual = alloc.allocate(tmpl);
		m_used[index] = true;
		++m_count;
		return ActorId {index, m_generation[index]};
	}

	constexpr bool remove(ActorId id) {
		if (!valid_id(id)) {
			return false;
		}
		m_used[id.index] = false;
		++m_generation[id.index];
		m_next[id.index] = m_free_head;
		m_free_head = id.index;
		--m_count;
		return true;
	}

	constexpr Actor* get(ActorId id) {
		return valid_id(id) ? &m_actors[id.index] : nullptr;
	}

	constexpr const Actor* get(ActorId id) const {
		return valid_id(id) ? &m_actors[id.index] : nullptr;
	}

	constexpr bool valid_id(ActorId id) const {
		return id.valid() && id.index < MaxActors && m_used[id.index] &&
		       m_generation[id.index] == id.generation;
	}

	/// Acceso por índice de slot (para iterar el parque); comprobar `used`.
	constexpr bool used(eng::u16 index) const { return index < MaxActors && m_used[index]; }
	constexpr Actor& at(eng::u16 index) { return m_actors[index]; }
	constexpr const Actor& at(eng::u16 index) const { return m_actors[index]; }

private:
	Actor m_actors[MaxActors] {};
	eng::u16 m_generation[MaxActors] {};
	eng::u16 m_next[MaxActors] {};
	bool m_used[MaxActors] {};
	eng::u16 m_free_head = 0u;
	eng::u16 m_count = 0u;
};

} // namespace eng::scene
