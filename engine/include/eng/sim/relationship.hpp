#pragma once

/// \file relationship.hpp
/// **Relaciones** entre criaturas (`eng::sim`): qué siente un individuo por otro y con
/// qué intensidad. Cada relación lleva:
///
/// - `kind`: el tipo de vínculo (come, teme, manada, ignora, rival, familia, pareja).
/// - `affinity` (`s8`, `[-100,100]`): el **vínculo social** (amistad/apego estable que
///   cambia poco: compartir, ayudar, convivir).
/// - `affect` (`s8`, `[-100,100]`): la **carga emocional dirigida** (amor vs odio, que
///   puede cambiar rápido: cortejo, traición, agravio). Es lo que permite **celos** y
///   **venganza selectiva**: no se odia "en general", se odia a alguien.
///
/// `bond_score` combina ambos y es la lectura que usan los comportamientos para elegir
/// **a quién** ayudar, cortejar o confrontar. La lista es de capacidad fija
/// (`eng::util::StaticVector`): al llenarse se descarta la relación de menor peso
/// (`|affinity| + |affect|`), de modo que no hay heap y el coste queda acotado en el 68000.
///
/// El afecto agregado (amor, odio, envidia...) vive en `Mind`; aquí está su proyección
/// **dirigida a un individuo**, que es la que da memoria social matizada.
///
/// Verificación: HOST-152 y HOST-154.

#include <eng/core/types/ptr.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/static_vector.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Tipo cualitativo de vínculo.
enum class RelationKind : eng::u8 {
	Eats = 0,    ///< el target es comida
	Afraid = 1,  ///< el target da miedo
	Pack = 2,    ///< aliado de manada
	Ignores = 3, ///< neutro
	Rival = 4,   ///< competidor (misma comida/territorio/pareja)
	Family = 5,  ///< cría/padre/hermano
	Mate = 6,    ///< pareja
	Count = 7,
};

/// Una relación activa con otra entidad.
struct Relationship {
	EntityId target = no_entity;
	RelationKind kind = RelationKind::Ignores;
	eng::s8 affinity = 0; ///< vínculo social estable
	eng::s8 affect = 0;   ///< carga emocional dirigida (amor/odio)
};

/// Lista de relaciones de una criatura.
template <eng::usize N>
using RelationshipList = eng::util::StaticVector<Relationship, N>;

namespace detail {
[[nodiscard]] constexpr eng::u8 abs_s8(eng::s8 v) noexcept {
	return v < 0 ? static_cast<eng::u8>(-static_cast<eng::s16>(v))
		     : static_cast<eng::u8>(v);
}
[[nodiscard]] constexpr eng::s8 clamp_rel(eng::s16 v) noexcept {
	if (v > 100) {
		return 100;
	}
	if (v < -100) {
		return -100;
	}
	return static_cast<eng::s8>(v);
}
[[nodiscard]] constexpr eng::s16 weight_of(const Relationship& r) noexcept {
	return static_cast<eng::s16>(abs_s8(r.affinity)) + abs_s8(r.affect);
}
} // namespace detail

/// Busca la relación con `target`: `Ref` a la entrada, o `Ref` no válida.
template <eng::usize N>
[[nodiscard]] constexpr eng::Ref<Relationship> find_rel(RelationshipList<N>& list,
							EntityId target) noexcept {
	for (eng::usize i = 0; i < list.size(); ++i) {
		if (list[i].target == target) {
			return &list[i];
		}
	}
	return eng::Ref<Relationship>();
}

template <eng::usize N>
[[nodiscard]] constexpr eng::Ref<const Relationship> find_rel(const RelationshipList<N>& list,
							      EntityId target) noexcept {
	for (eng::usize i = 0; i < list.size(); ++i) {
		if (list[i].target == target) {
			return &list[i];
		}
	}
	return eng::Ref<const Relationship>();
}

/// Crea o actualiza la relación (vínculo y afecto). Si está llena, descarta la de menor
/// peso.
template <eng::usize N>
constexpr void set_relationship(RelationshipList<N>& list, EntityId target, RelationKind kind,
				eng::s8 affinity, eng::s8 affect = 0) noexcept {
	if (target == no_entity) {
		return;
	}
	if (auto r = find_rel(list, target); r.valid()) {
		r->kind = kind;
		r->affinity = detail::clamp_rel(affinity);
		r->affect = detail::clamp_rel(affect);
		return;
	}
	const Relationship fresh {target, kind, detail::clamp_rel(affinity),
				  detail::clamp_rel(affect)};
	if (list.full()) {
		eng::usize weakest = 0u;
		eng::s16 weakest_w = 255;
		for (eng::usize i = 0; i < list.size(); ++i) {
			const eng::s16 w = detail::weight_of(list[i]);
			if (w < weakest_w) {
				weakest_w = w;
				weakest = i;
			}
		}
		list[weakest] = fresh;
		return;
	}
	(void)list.push_back(fresh);
}

/// Compatibilidad: fija vínculo y afecto a la vez.
template <eng::usize N>
constexpr void set_relation(RelationshipList<N>& list, EntityId target, RelationKind kind,
			    eng::s8 affinity) noexcept {
	set_relationship(list, target, kind, affinity, 0);
}

/// Ajusta el **vínculo** con `target` por `delta`, creando la relación si no existe.
template <eng::usize N>
constexpr void adjust_affinity(RelationshipList<N>& list, EntityId target,
			       RelationKind kind, eng::s16 delta) noexcept {
	if (auto r = find_rel(list, target); r.valid()) {
		r->affinity = detail::clamp_rel(static_cast<eng::s16>(r->affinity) + delta);
		return;
	}
	set_relationship(list, target, kind, detail::clamp_rel(delta), 0);
}

/// Ajusta la **carga emocional** hacia `target` por `delta` (positivo = acercamiento,
/// negativo = rencor). Es la vía del cortejo, la traición y la venganza selectiva.
template <eng::usize N>
constexpr void adjust_affect(RelationshipList<N>& list, EntityId target, RelationKind kind,
			     eng::s16 delta) noexcept {
	if (auto r = find_rel(list, target); r.valid()) {
		r->affect = detail::clamp_rel(static_cast<eng::s16>(r->affect) + delta);
		return;
	}
	set_relationship(list, target, kind, 0, detail::clamp_rel(delta));
}

/// Vínculo con `target` (0 si no hay relación).
template <eng::usize N>
[[nodiscard]] constexpr eng::s8 affinity_toward(const RelationshipList<N>& list,
						EntityId target) noexcept {
	const eng::Ref<const Relationship> r = find_rel(list, target);
	return r.valid() ? r->affinity : 0;
}

/// Carga emocional dirigida a `target` (0 si no hay relación).
template <eng::usize N>
[[nodiscard]] constexpr eng::s8 affect_toward(const RelationshipList<N>& list,
					      EntityId target) noexcept {
	const eng::Ref<const Relationship> r = find_rel(list, target);
	return r.valid() ? r->affect : 0;
}

/// Puntuación de vínculo total (`affinity + affect`), en `[-200, 200]`.
template <eng::usize N>
[[nodiscard]] constexpr eng::s16 bond_score(const RelationshipList<N>& list,
					    EntityId target) noexcept {
	const eng::Ref<const Relationship> r = find_rel(list, target);
	if (!r.valid()) {
		return 0;
	}
	return static_cast<eng::s16>(r->affinity) + r->affect;
}

/// Relación más intensa de un tipo (la de mayor valor absoluto de afinidad).
template <eng::usize N>
[[nodiscard]] constexpr eng::Ref<const Relationship> strongest_rel(const RelationshipList<N>& list,
								   RelationKind kind) noexcept {
	eng::Ref<const Relationship> best {};
	eng::u8 best_mag = 0u;
	for (eng::usize i = 0; i < list.size(); ++i) {
		if (list[i].kind != kind) {
			continue;
		}
		const eng::u8 mag = detail::abs_s8(list[i].affinity);
		if (!best.valid() || mag > best_mag) {
			best = &list[i];
			best_mag = mag;
		}
	}
	return best;
}

/// Relación con la carga emocional más **positiva** (aliado amado).
template <eng::usize N>
[[nodiscard]] constexpr eng::Ref<const Relationship> most_loved(
	const RelationshipList<N>& list) noexcept {
	eng::Ref<const Relationship> best {};
	for (eng::usize i = 0; i < list.size(); ++i) {
		if (!best.valid() || list[i].affect > best->affect) {
			best = &list[i];
		}
	}
	return (best.valid() && best->affect > 0) ? best : eng::Ref<const Relationship>();
}

/// Relación con la carga emocional más **negativa** (enemigo odiado), si la hay.
template <eng::usize N>
[[nodiscard]] constexpr eng::Ref<const Relationship> most_hated(
	const RelationshipList<N>& list) noexcept {
	eng::Ref<const Relationship> best {};
	for (eng::usize i = 0; i < list.size(); ++i) {
		if (!best.valid() || list[i].affect < best->affect) {
			best = &list[i];
		}
	}
	return (best.valid() && best->affect < 0) ? best : eng::Ref<const Relationship>();
}

} // namespace eng::sim
