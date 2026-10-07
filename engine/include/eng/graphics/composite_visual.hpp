#pragma once

/// \file composite_visual.hpp
/// **Contenido de un sprite compuesto**: una entidad gráfica de alto nivel (personaje,
/// vehículo, jefe) formada por **varias partes** con su propio `Visual`, offsets respecto
/// al ancla, frames por parte y **secuencias** con eventos, más las **hitboxes** del frame.
///
/// Es contenido puro (sin flags de representación ni de hardware): cada parte se
/// materializará como Sprite HW, BOB o CPU según decida el planner (ver
/// `docs/guides/roadmap/ROADMAP_JUEGO_SPRITES_BOBS.md` §5 F1 y
/// `docs/engine/architecture/OBJECT_SYSTEM.md`). El **estado de runtime** y la
/// materialización viven en `scene/composite_actor.hpp`.
///
/// Todo el contenido es **cocinable** (spans a tablas estáticas): partes ordenadas por `z`,
/// frames por parte y hitboxes por frame. El runtime solo lee spans y avanza enteros.
///
/// Lógica pura (sin hardware, sin heap, sin STL): host-testable.

#include <eng/core/types/box.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/graphics/raster_intent.hpp>

namespace eng::graphics {

/// **Caja de golpe** de un frame del compuesto, relativa al ancla (sin espejar).
struct CompositeHitBox {
	eng::Box box {};  ///< x, y relativos al ancla; w/h en píxeles
	u8 group = 0;     ///< capa de colisión (jugador, enemigo, proyectil…), de la app
	bool solid = true; ///< ¿bloquea el movimiento? (a criterio de la app)
};

/// **Parte** de un compuesto: contenido + posición respecto al ancla + orden de dibujo.
struct CompositePart {
	Visual visual {};   ///< contenido puro (píxeles/máscara/geometría/frames)
	s16 offset_x = 0;   ///< desplazamiento respecto al ancla (píxeles)
	s16 offset_y = 0;
	u8 z = 0;           ///< orden dentro del compuesto (menor primero; el mayor delante)
};

/// **Frame** del compuesto: qué frame usa cada parte + hitboxes + duración y evento.
struct CompositeFrame {
	/// Frame de cada parte (mismo orden que `CompositeVisual::parts`); si es más corto o
	/// vacío, las partes sin entrada usan el frame 0.
	Span<const u8> part_frames {};
	/// Hitboxes de este frame; vacío = usar `CompositeVisual::bounds`.
	Span<const CompositeHitBox> hits {};
	u16 ticks = 1;  ///< duración en ticks de juego (0 se trata como 1)
	u16 event = 0;  ///< evento opcional (sonido, golpe, fin); 0 = ninguno
};

/// **Secuencia** de frames (idle, andar, atacar, morir…).
struct CompositeSequence {
	Span<const CompositeFrame> frames {};
	bool loop = true;
	/// Secuencia a la que saltar al terminar (si `loop == false`); `0xff` = quedarse en el
	/// último frame (`finished`).
	u8 next = 0xff;
};

/// **Descripción completa** de un sprite compuesto (contenido cocinado).
struct CompositeVisual {
	Span<const CompositePart> parts {};
	Span<const CompositeSequence> sequences {};
	s16 anchor_x = 0;  ///< ancla (pies, centro…) dentro del espacio de las partes
	s16 anchor_y = 0;
	eng::Box bounds {}; ///< hitbox por defecto si el frame no trae `hits`
};

} // namespace eng::graphics
