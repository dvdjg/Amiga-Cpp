#pragma once

/// \file entity_pool.hpp
/// **Pool de entidades ligeras** (F2 de `ROADMAP_JUEGO_SPRITES_BOBS.md` §5): proyectiles,
/// enemigos ligeros y escombros con trayectoria y sprite compuesto. Capacidad fija (sin
/// heap), sin virtuales y con actualización determinista por ticks de juego.
///
/// Las entidades con `vertical_stream` son candidatas a compartir **un solo canal de
/// Sprite HW** por reuso vertical: el juego las recolecta y les asigna el mismo
/// `SpriteIntent::group_id` (el `SpriteAllocator` ya lo soporta; ver
/// `sprite-multiplexer-bob-fallback.md`).
///
/// Lógica pura (sin hardware, sin heap, sin STL): host-testable.

#include <eng/core/math/linalg.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/graphics/composite_visual.hpp>
#include <eng/scene/composite_actor.hpp>
#include <eng/scene/trajectory.hpp>

namespace eng::scene {

/// Tipo de entidad del pool (la app puede ampliarlo con `faction`/`hp`).
enum class EntityKind : eng::u8 {
	None = 0,
	Projectile,
	Enemy,
	Debris,
};

/// Pool de entidades de capacidad fija. El llamador guarda la tabla de trayectorias del
/// nivel (`Span<const Trajectory<s16>>`) y llama `update` una vez por tick de juego.
template <eng::u16 MaxEntities>
struct EntityPool {
	/// Ranura de entidad. `prev_x/prev_y` permiten el borrado del rastro (BOB) o el
	/// save-under sin que la app lleve estado paralelo.
	struct Slot {
		EntityKind kind = EntityKind::None;
		TrajectoryFollower<s16> traj {};
		eng::u8 trajectory_id = 0;
		const eng::graphics::CompositeVisual* visual = nullptr;
		CompositeState anim {};
		eng::s16 x = 0;
		eng::s16 y = 0;
		eng::s16 prev_x = 0;
		eng::s16 prev_y = 0;
		eng::u8 hp = 1;
		eng::u8 damage = 1;
		eng::u8 faction = 0;
		bool active = false;
		bool vertical_stream = false;  ///< candidata a grupo de 1 canal (F2)
	};

	Slot slots[MaxEntities] {};

	/// Da de alta una entidad en el primer hueco libre. Devuelve el índice o `0xffff`.
	/// `traj` puede ser `nullptr` (la entidad se queda en `(x, y)`).
	eng::u16 spawn(EntityKind kind, const eng::graphics::CompositeVisual* visual,
		       const Trajectory<s16>* traj, eng::u8 trajectory_id, eng::s16 x, eng::s16 y,
		       eng::u8 sequence = 0u, bool vertical = false) noexcept {
		for (eng::u16 i = 0; i < MaxEntities; ++i) {
			if (slots[i].active) {
				continue;
			}
			Slot& e = slots[i];
			e = Slot {};
			e.kind = kind;
			e.active = true;
			e.visual = visual;
			e.trajectory_id = (traj != nullptr) ? trajectory_id : 0xffu; // 0xff = sin trayectoria
			e.vertical_stream = vertical;
			e.traj.origin.x() = x;
			e.traj.origin.y() = y;
			e.anim.sequence = sequence;
			eng::math::Vec<2, eng::s16> pos {};
			if (traj != nullptr) {
				trajectory_advance(*traj, e.traj, 0u, pos); // posición del spawn
			} else {
				pos.x() = x;
				pos.y() = y;
			}
			e.x = e.anim.x = pos.x();
			e.y = e.anim.y = pos.y();
			e.prev_x = e.x;
			e.prev_y = e.y;
			return i;
		}
		return 0xffffu; // pool lleno
	}

	/// Apaga una entidad (el hueco se reutiliza en el siguiente `spawn`).
	void kill(eng::u16 index) noexcept {
		if (index < MaxEntities) {
			slots[index].active = false;
			slots[index].kind = EntityKind::None;
		}
	}

	/// Avanza todas las entidades: trayectoria, animación del compuesto y culling por
	/// rectángulo (`cull_min_*`/`cull_max_*`, por defecto el área visible 320x256 con
	/// margen). Los proyectiles que terminan su trayectoria se apagan solos.
	void update(eng::Span<const Trajectory<eng::s16>> trajectories, eng::u16 ticks,
		    eng::s16 cull_min_x = -32, eng::s16 cull_min_y = -32,
		    eng::s16 cull_max_x = 352, eng::s16 cull_max_y = 288) noexcept {
		for (eng::u16 i = 0; i < MaxEntities; ++i) {
			Slot& e = slots[i];
			if (!e.active) {
				continue;
			}
			e.prev_x = e.x;
			e.prev_y = e.y;
			if (e.trajectory_id < trajectories.size()) {
				eng::math::Vec<2, eng::s16> pos {};
				trajectory_advance(trajectories[e.trajectory_id], e.traj, ticks, pos);
				e.x = e.anim.x = pos.x();
				e.y = e.anim.y = pos.y();
				if (e.traj.finished && e.kind == EntityKind::Projectile) {
					kill(i);
					continue;
				}
			}
			if (e.visual != nullptr) {
				(void)composite_advance(*e.visual, e.anim, ticks);
			}
			if (e.x < cull_min_x || e.x > cull_max_x || e.y < cull_min_y ||
			    e.y > cull_max_y) {
				kill(i);
			}
		}
	}

	/// Cuántas entidades activas hay (telemetría / gates).
	[[nodiscard]] eng::u16 live_count() const noexcept {
		eng::u16 n = 0;
		for (eng::u16 i = 0; i < MaxEntities; ++i) {
			if (slots[i].active) {
				++n;
			}
		}
		return n;
	}
};

} // namespace eng::scene
