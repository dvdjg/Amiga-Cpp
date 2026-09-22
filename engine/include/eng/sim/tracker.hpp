#pragma once

/// \file tracker.hpp
/// **Trackers** (`eng::sim`): la representación abstracta que una criatura mantiene de
/// otra entidad percibida. Cada tracker guarda a quién se refiere (`target`), de qué
/// tipo es la percepción (`TrackerKind`), la última posición conocida, su `confidence`
/// (0 = olvidado, 255 = certeza) y el tick del último avistamiento.
///
/// Este es el corazón del comportamiento emergente tipo *Rain World*: la criatura no
/// reacciona a lo que "hay", sino a **lo que cree que hay**. Un depredador visto hace un
/// segundo (confidence alta, posición fresca) pesa distinto que una presa solo oída
/// (confidence baja, posición antigua). `decay` modela el olvido gradual.
///
/// El sistema es agnóstico de la proyección: la posición es una posición **de mundo**
/// (`room` + `x,y`), no de pantalla. Un `Projection` decide cómo se traduce a 2D/iso/3D.
///
/// Verificación: HOST-152.

#include <eng/core/ptr.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/static_vector.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Qué representa el tracker.
enum class TrackerKind : eng::u8 {
	Prey = 0,   ///< posible comida
	Threat = 1, ///< peligro (depredador, rival fuerte)
	Friend = 2, ///< aliado/individuo del grupo
	Item = 3,   ///< objeto manipulable (comida, piedra, herramienta)
	Den = 4,    ///< refugio/hogar
	Injury = 5, ///< fuente de daño localizada (trampa, zona hostil)
	Noise = 6,  ///< ruido sin fuente identificada
	Kin = 7,    ///< cría/familiar al que atender
	Rival = 8,  ///< competidor percibido (territorio, comida, pareja)
	Mate = 9,   ///< pareja/candidato reproductivo percibido
	Count = 10,
};

/// Una entidad percibida y su recuerdo.
struct Tracker {
	EntityId target = no_entity;
	RoomId room = no_room;
	eng::s16 x = 0;
	eng::s16 y = 0;
	TrackerKind kind = TrackerKind::Noise;
	eng::u8 confidence = 0;
	eng::u16 last_seen = 0;
	eng::u8 modalities = 0; ///< bitmask de sentidos que lo han detectado (ver `senses.hpp`)
	eng::u8 salience = 0;   ///< relevancia para la atención (fuerza + novedad)
};

/// Lista de trackers de una criatura.
template <eng::usize N>
using TrackerList = eng::util::StaticVector<Tracker, N>;

/// Busca el tracker de `(target, kind)`: `Ref` a la entrada de la lista, o `Ref` no válida.
template <eng::usize N>
[[nodiscard]] constexpr eng::Ref<Tracker> find_tracker(TrackerList<N>& list, EntityId target,
						       TrackerKind kind) noexcept {
	for (eng::usize i = 0; i < list.size(); ++i) {
		if (list[i].target == target && list[i].kind == kind) {
			return eng::Ref<Tracker>(&list[i]);
		}
	}
	return eng::Ref<Tracker>();
}

template <eng::usize N>
[[nodiscard]] constexpr eng::Ref<const Tracker> find_tracker(const TrackerList<N>& list,
							     EntityId target,
							     TrackerKind kind) noexcept {
	for (eng::usize i = 0; i < list.size(); ++i) {
		if (list[i].target == target && list[i].kind == kind) {
			return eng::Ref<const Tracker>(&list[i]);
		}
	}
	return eng::Ref<const Tracker>();
}

/// Registra una percepción. Refresca el tracker existente (posición, `confidence` al
/// máximo de lo ya sabido y lo nuevo, `last_seen`) o crea uno; si está lleno, descarta
/// el de menor confianza (desempate: el primero).
template <eng::usize N>
constexpr void observe(TrackerList<N>& list, TrackerKind kind, EntityId target, RoomId room,
		       eng::s16 x, eng::s16 y, eng::u8 confidence,
		       eng::u16 tick) noexcept {
	if (target == no_entity) {
		return;
	}
	if (auto t = find_tracker(list, target, kind); t.valid()) {
		t->room = room;
		t->x = x;
		t->y = y;
		if (confidence > t->confidence) {
			t->confidence = confidence;
		}
		t->last_seen = tick;
		return;
	}
	const Tracker fresh {target, room, x, y, kind, confidence, tick, 0u, 0u};
	if (list.full()) {
		eng::usize weakest = 0u;
		eng::u8 weakest_conf = static_cast<eng::u8>(255u);
		for (eng::usize i = 0; i < list.size(); ++i) {
			if (list[i].confidence < weakest_conf) {
				weakest_conf = list[i].confidence;
				weakest = i;
			}
		}
		if (weakest_conf >= confidence) {
			return; // lo nuevo no mejora lo que ya hay
		}
		list[weakest] = fresh;
		return;
	}
	(void)list.push_back(fresh);
}

/// Envejece todos los trackers: baja `confidence` y elimina los olvidados (0).
template <eng::usize N>
constexpr void decay(TrackerList<N>& list, eng::u8 amount) noexcept {
	for (eng::usize i = 0; i < list.size();) {
		Tracker& t = list[i];
		t.confidence = u8_sat_sub(t.confidence, amount);
		if (t.confidence == 0u) {
			list.erase(i);
		} else {
			++i;
		}
	}
}

/// Tracker de mayor confianza de un tipo (`Ref` no válida si no hay ninguno).
template <eng::usize N>
[[nodiscard]] constexpr eng::Ref<const Tracker> best_tracker(const TrackerList<N>& list,
							     TrackerKind kind) noexcept {
	eng::Ref<const Tracker> best {};
	for (eng::usize i = 0; i < list.size(); ++i) {
		if (list[i].kind != kind) {
			continue;
		}
		if (!best.valid() || list[i].confidence > best->confidence) {
			best = eng::Ref<const Tracker>(&list[i]);
		}
	}
	return best;
}

/// Confianza actual hacia `(target, kind)` (0 si no hay tracker).
template <eng::usize N>
[[nodiscard]] constexpr eng::u8 confidence_of(const TrackerList<N>& list, EntityId target,
					      TrackerKind kind) noexcept {
	const eng::Ref<const Tracker> t = find_tracker(list, target, kind);
	return t.valid() ? t->confidence : 0u;
}

/// Olvida un tracker concreto.
template <eng::usize N>
constexpr void forget(TrackerList<N>& list, EntityId target, TrackerKind kind) noexcept {
	for (eng::usize i = 0; i < list.size(); ++i) {
		if (list[i].target == target && list[i].kind == kind) {
			list.erase(i);
			return;
		}
	}
}

} // namespace eng::sim
