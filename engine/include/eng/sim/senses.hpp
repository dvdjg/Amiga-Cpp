#pragma once

/// \file senses.hpp
/// **Percepción multimodal** (`eng::sim`): cada criatura "mira, oye, huele, toca, saborea y
/// nota la temperatura". Cada sentido tiene su alcance y su fiabilidad, y todos producen
/// **observaciones** que alimentan la memoria de corto plazo (los `Tracker`).
///
/// El mundo no expone sensores concretos: rellena una lista de `SenseTarget` (lo que un
/// candidato *emite*: tamaño, ruido, olor, salto térmico, cobertura, visibilidad) —normal-
/// mente a partir de un `SpatialHash`— y `perceive` decide qué percibe el observador. Así el
/// sistema es agnóstico de la proyección (2D/iso/3D): solo usa posición de mundo y
/// capacidades sensoriales.
///
/// Reglas (todas enteras, paramétricas en `SenseParams`):
/// - **Vista**: cono frontal (`vision_arc`), alcance, línea de visión aproximada por
///   cobertura; el tamaño del objetivo ayuda.
/// - **Oído**: omnidireccional, atraviesa muros con atenuación; depende del ruido emitido.
/// - **Olfato**: alcance corto, lo bloquea la cobertura.
/// - **Tacto/Gusto**: solo a distancia de contacto.
/// - **Temperatura**: omnidireccional, según el salto térmico.
///
/// La **novedad** (¿está ya en la memoria de largo plazo?) sube la `salience`, que es la que
/// gobierna la atención y la consolidación de recuerdos (`memory.hpp`).
///
/// Verificación: HOST-160.

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/sim/genetics.hpp>
#include <eng/sim/species.hpp>
#include <eng/sim/tracker.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Modalidad sensorial.
enum class SenseKind : eng::u8 {
	Sight = 0,
	Hearing = 1,
	Smell = 2,
	Touch = 3,
	Taste = 4,
	Temperature = 5,
	Count = 6,
};

/// Bit de cada modalidad (para la máscara `modalities` del `Tracker`).
namespace sense_bit {
inline constexpr eng::u8 sight = 0x01u;
inline constexpr eng::u8 hearing = 0x02u;
inline constexpr eng::u8 smell = 0x04u;
inline constexpr eng::u8 touch = 0x08u;
inline constexpr eng::u8 taste = 0x10u;
inline constexpr eng::u8 temperature = 0x20u;
} // namespace sense_bit

[[nodiscard]] constexpr eng::u8 sense_bit_of(SenseKind k) noexcept {
	switch (k) {
		case SenseKind::Sight: return sense_bit::sight;
		case SenseKind::Hearing: return sense_bit::hearing;
		case SenseKind::Smell: return sense_bit::smell;
		case SenseKind::Touch: return sense_bit::touch;
		case SenseKind::Taste: return sense_bit::taste;
		case SenseKind::Temperature: return sense_bit::temperature;
		default: return 0u;
	}
}

[[nodiscard]] constexpr const char* sense_name(SenseKind k) noexcept {
	switch (k) {
		case SenseKind::Sight: return "sight";
		case SenseKind::Hearing: return "hearing";
		case SenseKind::Smell: return "smell";
		case SenseKind::Touch: return "touch";
		case SenseKind::Taste: return "taste";
		case SenseKind::Temperature: return "temperature";
		default: return "?";
	}
}

/// Capacidades sensoriales de una criatura: sensibilidad (0..100) y alcance por modalidad.
struct Senses {
	eng::u8 vision = 50;
	eng::u8 hearing = 50;
	eng::u8 smell = 50;
	eng::u8 touch = 50;
	eng::u8 taste = 50;
	eng::u8 thermosense = 50;
	eng::u8 vision_arc = 100; ///< apertura del cono en grados (0..180)
	eng::u8 vision_range = 20;
	eng::u8 hearing_range = 25;
	eng::u8 smell_range = 12;
	eng::u8 temperature_range = 10;
	eng::u8 acuity = 50; ///< filtro de atención global (0..100)
	eng::u8 echolocation = 0; ///< 0..100: el oído atraviesa muros casi sin penalización
};

/// Lo que un candidato *emite* y que el observador puede percibir.
struct SenseTarget {
	EntityId id = no_entity;
	RoomId room = no_room;
	eng::s16 x = 0;
	eng::s16 y = 0;
	eng::u8 size = 50;      ///< tamaño aparente (ayuda a la vista)
	eng::u8 sound = 0;      ///< ruido emitido (0..255)
	eng::u8 odor = 0;       ///< olor emitido (0..255)
	eng::u8 temp_delta = 0; ///< salto térmico respecto al ambiente (0..255)
	eng::u8 cover = 0;      ///< cobertura del objetivo (0..100): camuflaje/bloqueo
	bool visible = true;    ///< ¿es visible (no camuflado/oculto)?
	TrackerKind kind = TrackerKind::Noise; ///< categoría que el juego le asigna
};

/// Quién observa y hacia dónde mira.
struct Observer {
	RoomId room = no_room;
	eng::s16 x = 0;
	eng::s16 y = 0;
	eng::s16 face_x = 1; ///< orientación (si es (0,0), visión de 360°)
	eng::s16 face_y = 0;
};

/// Resultado de percibir un objetivo.
struct Observation {
	EntityId target = no_entity;
	RoomId room = no_room;
	eng::s16 x = 0;
	eng::s16 y = 0;
	SenseKind sense = SenseKind::Sight; ///< modalidad dominante
	TrackerKind kind = TrackerKind::Noise; ///< categoría del objetivo
	eng::u8 modalities = 0;             ///< todas las que dispararon
	eng::u8 strength = 0;               ///< intensidad de la dominante
	eng::u8 novelty = 0;                ///< novedad percibida (0..255)
	eng::u8 salience = 0;               ///< fuerza + novedad (atención)
};

/// Parámetros globales de percepción.
struct SenseParams {
	eng::u8 novelty_bonus = 90u;       ///< salience extra por algo desconocido
	eng::u8 room_hearing_penalty = 60u;///< atenuación del oído entre regiones contiguas
	eng::u8 touch_range = 1;           ///< distancia de contacto
	eng::u8 taste_range = 1;
};

/// Sensores materiales de una especie (visión/oído del `Species`; el resto, por tamaño).
[[nodiscard]] constexpr Senses senses_from_species(const Species& s) noexcept {
	Senses sn {};
	sn.vision = s.vision;
	sn.hearing = s.hearing;
	sn.smell = static_cast<eng::u8>(50u + s.size / 4u);
	sn.touch = 60u;
	sn.taste = 50u;
	sn.thermosense = 50u;
	sn.acuity = static_cast<eng::u8>(50u + s.vision / 4u);
	return sn;
}

/// Parámetros de **atención**: el estado interno estrecha o agudiza los sentidos.
struct AttentionParams {
	eng::u8 fear_narrow_arc = 60u;   ///< % en que el miedo estrecha el cono visual
	eng::u8 fear_narrow_range = 40u; ///< % en que el miedo acorta la vista
	eng::u8 fear_narrow_smell = 30u;
	eng::u8 fear_boost_hearing = 30u;///< el miedo agudiza el oído (hipervigilancia)
	eng::u8 anger_narrow_arc = 25u;  ///< la ira enfoca aún más la vista
	eng::u8 anger_boost_acuity = 20u;
	eng::u8 min_arc = 30u;           ///< suelo del cono (no se cierra del todo)
	eng::u8 min_range = 4u;
};

/// Aplica el estado interno a los sentidos: el **miedo** estrecha y acorta la vista pero
/// agudiza el oído; la **ira** enfoca. Cierra el ciclo percepción↔conducta.
[[nodiscard]] constexpr Senses focused(const Senses& base, eng::u8 fear, eng::u8 anger,
				       const AttentionParams& p = AttentionParams {}) noexcept {
	Senses s = base;
	const eng::u8 arc_keep = static_cast<eng::u8>(
		100u - u8_min(100u, static_cast<eng::u8>(u8_scale(fear, p.fear_narrow_arc) +
							 u8_scale(anger, p.anger_narrow_arc))));
	s.vision_arc = u8_scale(base.vision_arc, arc_keep);
	if (s.vision_arc < p.min_arc) {
		s.vision_arc = p.min_arc;
	}
	const eng::u8 range_keep =
		static_cast<eng::u8>(100u - u8_min(100u, u8_scale(fear, p.fear_narrow_range)));
	s.vision_range = u8_scale(base.vision_range, range_keep);
	if (s.vision_range < p.min_range) {
		s.vision_range = p.min_range;
	}
	s.smell_range = u8_scale(
		base.smell_range,
		static_cast<eng::u8>(100u - u8_min(100u, u8_scale(fear, p.fear_narrow_smell))));
	s.hearing_range = u8_sat_add(base.hearing_range, u8_scale(fear, p.fear_boost_hearing));
	s.acuity = u8_sat_add(base.acuity, u8_scale(anger, p.anger_boost_acuity));
	return s;
}

/// Parámetros de expresión del genoma en los sentidos.
struct SenseGenomeParams {
	eng::u8 echo_threshold = 70u; ///< velocidad mínima para desarrollar ecolocalización
	eng::u8 range_base = 8u;      ///< alcance base de visión/oído
	eng::u8 smell_range_base = 6u;
};

/// Expresa los sentidos del genoma: la **velocidad** da vista fina y, por encima del
/// umbral, **ecolocalización**; la **sociabilidad**, oído; el **tamaño**, olfato.
[[nodiscard]] constexpr Senses senses_from_genome(const Genome& g,
						  const SenseGenomeParams& p = SenseGenomeParams {}) noexcept {
	Senses s {};
	const eng::u8 speed = g.gene(Gene::Speed);
	const eng::u8 social = g.gene(Gene::Sociability);
	const eng::u8 size = g.gene(Gene::Size);
	s.vision = clamp100(50 + (static_cast<eng::s32>(speed) - 50) / 2);
	s.hearing = clamp100(50 + (static_cast<eng::s32>(social) - 50) / 2);
	s.smell = clamp100(50 + (static_cast<eng::s32>(size) - 50) / 2);
	s.touch = 60u;
	s.taste = 50u;
	s.thermosense = clamp100(50 + (static_cast<eng::s32>(g.gene(Gene::Lifespan)) - 50) / 2);
	s.acuity = clamp100(50 + (static_cast<eng::s32>(g.gene(Gene::Aggression)) - 50) / 2);
	s.echolocation = speed >= p.echo_threshold ? speed : 0u;
	s.vision_range = u8_sat_add(p.range_base, static_cast<eng::u8>(speed / 16u));
	s.hearing_range = u8_sat_add(p.range_base, static_cast<eng::u8>(social / 16u));
	s.smell_range = u8_sat_add(p.smell_range_base, static_cast<eng::u8>(size / 20u));
	s.temperature_range = u8_sat_add(p.smell_range_base,
					 static_cast<eng::u8>(g.gene(Gene::Lifespan) / 24u));
	return s;
}

/// Sector (0..7) de una dirección, en brújula: 0=E, 2=N, 4=W, 6=S. Evita trigonometría
/// para el cono de visión.
[[nodiscard]] constexpr eng::u8 sector_of(eng::s16 x, eng::s16 y) noexcept {
	if (x == 0 && y == 0) {
		return 0u;
	}
	if (y == 0) {
		return x > 0 ? 0u : 4u;
	}
	if (x == 0) {
		return y > 0 ? 2u : 6u;
	}
	if (x > 0 && y > 0) {
		return x >= y ? 0u : 1u;
	}
	if (x < 0 && y > 0) {
		return -x >= y ? 4u : 3u;
	}
	if (x < 0 && y < 0) {
		return -x >= -y ? 4u : 5u;
	}
	return x >= -y ? 0u : 7u;
}

[[nodiscard]] constexpr eng::u8 sector_distance(eng::u8 a, eng::u8 b) noexcept {
	const eng::u8 d = static_cast<eng::u8>(a > b ? a - b : b - a);
	return d > 4u ? static_cast<eng::u8>(8u - d) : d;
}

/// ¿El objetivo cae dentro del cono frontal de `arc` grados? (si no hay orientación, 360°)
[[nodiscard]] constexpr bool in_cone(const Observer& o, eng::s16 dx, eng::s16 dy,
				     eng::u8 arc) noexcept {
	if (o.face_x == 0 && o.face_y == 0) {
		return true;
	}
	if (arc >= 180u) {
		return true;
	}
	// Un sector cubre 45°; el cono admite `arc/45` sectores a cada lado.
	const eng::u8 reach = static_cast<eng::u8>(arc / 90u + (arc % 90u != 0u ? 1u : 0u));
	return sector_distance(sector_of(o.face_x, o.face_y), sector_of(dx, dy)) <= reach;
}

/// Atenuación lineal por distancia en `[0,255]`.
[[nodiscard]] constexpr eng::u8 attenuation(eng::u16 dist, eng::u8 range) noexcept {
	if (range == 0u || dist > range) {
		return 0u;
	}
	return static_cast<eng::u8>(
		div_u16(static_cast<eng::u16>(static_cast<eng::u16>(range) - dist) * 255u, range));
}

/// Fuerza de la **vista** (0 si fuera de cono/alcance o invisible).
[[nodiscard]] constexpr eng::u8 sight_strength(const Senses& s, const Observer& o,
					       const SenseTarget& t, eng::u16 dist) noexcept {
	if (!t.visible || dist > s.vision_range) {
		return 0u;
	}
	if (!in_cone(o, static_cast<eng::s16>(t.x - o.x), static_cast<eng::s16>(t.y - o.y),
		     s.vision_arc)) {
		return 0u;
	}
	const eng::u8 att = attenuation(dist, s.vision_range);
	const eng::u8 cover = t.cover > 100u ? static_cast<eng::u8>(100u) : t.cover;
	return u8_scale(u8_scale(att, s.vision), static_cast<eng::u8>(100u - cover));
}

/// Fuerza del **oído** (omnidireccional; atraviesa muros con atenuación).
[[nodiscard]] constexpr eng::u8 hearing_strength(const Senses& s, bool same_room,
						 const SenseTarget& t, eng::u16 dist,
						 const SenseParams& p) noexcept {
	if (t.sound == 0u) {
		return 0u;
	}
	eng::u8 att = 0u;
	if (same_room) {
		if (dist > s.hearing_range) {
			return 0u;
		}
		att = attenuation(dist, s.hearing_range);
	} else {
		// Entre regiones no hay distancia fiable: se oye débil, atenuado por el muro.
		// La ecolocalización reduce esa penalización (casi "ve" con el sonido).
		const eng::u8 penalty = u8_scale(p.room_hearing_penalty,
						 static_cast<eng::u8>(100u - s.echolocation));
		att = static_cast<eng::u8>(100u - penalty);
	}
	return u8_scale(u8_scale(att, s.hearing), t.sound);
}

/// Fuerza del **olfato** (lo bloquea la cobertura).
[[nodiscard]] constexpr eng::u8 smell_strength(const Senses& s, bool same_room,
					       const SenseTarget& t, eng::u16 dist) noexcept {
	if (!same_room || t.odor == 0u || dist > s.smell_range) {
		return 0u;
	}
	const eng::u8 att = attenuation(dist, s.smell_range);
	const eng::u8 cover = t.cover > 100u ? static_cast<eng::u8>(100u) : t.cover;
	return u8_scale(u8_scale(u8_scale(att, s.smell), t.odor),
			static_cast<eng::u8>(100u - cover));
}

/// Fuerza del **tacto** (solo a distancia de contacto).
[[nodiscard]] constexpr eng::u8 touch_strength(const Senses& s, bool same_room,
					       eng::u16 dist,
					       const SenseParams& p) noexcept {
	return (same_room && dist <= p.touch_range) ? s.touch : 0u;
}

/// Fuerza del **gusto** (contacto con algo que emite olor, p. ej. comida).
[[nodiscard]] constexpr eng::u8 taste_strength(const Senses& s, bool same_room, bool detectable,
					       eng::u16 dist,
					       const SenseParams& p) noexcept {
	return (same_room && detectable && dist <= p.taste_range) ? s.taste : 0u;
}

/// Fuerza de la **temperatura**.
[[nodiscard]] constexpr eng::u8 temperature_strength(const Senses& s, bool same_room,
						     const SenseTarget& t,
						     eng::u16 dist) noexcept {
	if (!same_room || t.temp_delta == 0u || dist > s.temperature_range) {
		return 0u;
	}
	return u8_scale(u8_scale(attenuation(dist, s.temperature_range), s.thermosense),
			t.temp_delta);
}

/// Percibe `targets` y escribe hasta `out.size()` observaciones (las de mayor `salience`).
/// `known(target)` indica si la criatura ya conoce a ese objetivo (memoria de largo plazo),
/// lo que determina la **novedad**. Devuelve cuántas observaciones escribió.
template <class KnownFn>
constexpr eng::u8 perceive(const Senses& s, const Observer& o,
			   eng::Span<const SenseTarget> targets,
			   eng::Span<Observation> out, KnownFn known,
			   const SenseParams& p = SenseParams {}) noexcept {
	eng::u8 n = 0u;
	for (eng::usize i = 0; i < targets.size() && n < out.size(); ++i) {
		const SenseTarget& t = targets[i];
		const bool same_room = t.room == o.room && t.room != no_room;
		const eng::u16 dist = same_room
					      ? manhattan(o.x, o.y, t.x, t.y)
					      : static_cast<eng::u16>(255u);
		const eng::u8 sight = sight_strength(s, o, t, dist);
		const eng::u8 hear = hearing_strength(s, same_room, t, dist, p);
		const eng::u8 smell = smell_strength(s, same_room, t, dist);
		const eng::u8 touch = touch_strength(s, same_room, dist, p);
		const eng::u8 taste = taste_strength(s, same_room, t.odor != 0u, dist, p);
		const eng::u8 temp = temperature_strength(s, same_room, t, dist);

		eng::u8 modalities = 0u;
		eng::u8 best = 0u;
		SenseKind best_kind = SenseKind::Sight;
		const eng::u8 vals[6] = {sight, hear, smell, touch, taste, temp};
		for (eng::u8 m = 0; m < 6u; ++m) {
			if (vals[m] == 0u) {
				continue;
			}
			modalities = static_cast<eng::u8>(modalities |
							  sense_bit_of(static_cast<SenseKind>(m)));
			if (vals[m] > best) {
				best = vals[m];
				best_kind = static_cast<SenseKind>(m);
			}
		}
		if (modalities == 0u) {
			continue; // no percibido
		}
		const eng::u8 novelty = known(t.id) ? 0u : p.novelty_bonus;
		Observation obs {};
		obs.target = t.id;
		obs.room = t.room;
		obs.x = t.x;
		obs.y = t.y;
		obs.sense = best_kind;
		obs.kind = t.kind;
		obs.modalities = modalities;
		obs.strength = best;
		obs.novelty = novelty;
		const eng::u16 sal = static_cast<eng::u16>(best) + novelty;
		obs.salience = sal > 255u ? static_cast<eng::u8>(255u) : static_cast<eng::u8>(sal);
		out[n++] = obs;
	}
	return n;
}

} // namespace eng::sim
