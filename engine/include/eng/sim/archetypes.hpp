#pragma once

/// \file archetypes.hpp
/// **Catálogo de arquetipos** de personalidad (`eng::sim`, capa de persona). Un arquetipo
/// es una fila de la tabla `kArchetypes[]` que describe una personalidad con nombre a
/// partir de los rasgos base (`Personality`), los rasgos de psique (`PsycheTraits`), las
/// aptitudes de partida (`Skills`) y los defectos típicos (`Flaws`).
///
/// No es un `enum` cerrado: añadir una personalidad es añadir una fila. El arquetipo se
/// `materialize` en una `Persona` (ver `persona.hpp`) con jitter determinista, de modo que
/// dos individuos del mismo arquetipo difieren.
///
/// El catálogo cubre el listado de `docs/engine/architecture/NPC_PSYCHOLOGY.md` §4
/// (flemático, pardillo, embustero, experimentado, indeciso, listillo, bobo, engreído,
/// paternalista, condescendiente, idiota, sabio, ignorante, malo, envidioso, intolerante,
/// amigable, cenizo, tristón, alegre, chistoso, impertinente, despistado, esperanzador,
/// ilusionado, enamorado, irascible y variantes).
///
/// Verificación: HOST-199.

#include <eng/core/types/types.hpp>
#include <eng/sim/personality.hpp>
#include <eng/sim/psyche_traits.hpp>

namespace eng::sim {

/// Arquetipos del catálogo. El orden no implica fuerza.
enum class Archetype : eng::u8 {
	Flematico = 0,
	Pardillo,
	Embustero,
	Experimentado,
	Indeciso,
	Listillo,
	Bobo,
	Engreido,
	Paternalista,
	Condescendiente,
	Idiota,
	Sabio,
	Ignorante,
	Malo,
	Envidoso,
	Intolerante,
	Amigable,
	Cenizo,
	Triston,
	Alegre,
	Chistoso,
	Impertinente,
	Despistado,
	Esperanzador,
	Ilusionado,
	Enamorado,
	Irascible,
	Timorato,
	Temerario,
	Avaro,
	Generoso,
	Vengativo,
	Fatalista,
	Sanguineo,
	Metodico,
	Mistico,
	Nostalgico,
	Tramposo,
	Inocente,
	Count,
};

inline constexpr eng::usize archetype_count = static_cast<eng::usize>(Archetype::Count);

/// Definición de un arquetipo: rasgos base, de psique, aptitudes y defectos típicos.
struct ArchetypeDef {
	const char* name;
	Archetype id;
	Personality base;
	PsycheTraits psyche;
	Skills skills;
	Flaws flaws;
};

/// Constructor de conveniencia para una fila (campos por defecto neutros).
/// `b` fija un rasgo base, `p` uno de psique, `s` una aptitud y `f` añade un defecto.
struct ArchetypeBuilder {
	Personality base {};
	PsycheTraits psyche {};
	Skills skills {};
	Flaws flaws {};

	constexpr ArchetypeBuilder& b(eng::u8 Personality::*field, eng::u8 v) noexcept {
		base.*field = v;
		return *this;
	}
	constexpr ArchetypeBuilder& p(eng::u8 PsycheTraits::*field, eng::u8 v) noexcept {
		psyche.*field = v;
		return *this;
	}
	constexpr ArchetypeBuilder& s(eng::u8 Skills::*field, eng::u8 v) noexcept {
		skills.*field = v;
		return *this;
	}
	constexpr ArchetypeBuilder& f(Flaw flaw) noexcept {
		flaws.set(flaw);
		return *this;
	}
	[[nodiscard]] constexpr ArchetypeDef build(const char* name, Archetype id) const noexcept {
		return ArchetypeDef {name, id, base, psyche, skills, flaws};
	}
};

/// Tabla del catálogo. Cada fila declara los rasgos dominantes; el resto queda neutro.
inline constexpr ArchetypeDef kArchetypes[archetype_count] = {
	// Flemático: imperturbable, frío, sin tics.
	ArchetypeBuilder {}
		.p(&PsycheTraits::composure, 90).p(&PsycheTraits::temper, 15)
		.p(&PsycheTraits::patience, 80).p(&PsycheTraits::impulsivity, 20)
		.s(&Skills::odds_math, 60).s(&Skills::tell_control, 70)
		.build("Flemático", Archetype::Flematico),

	// Pardillo: se le nota todo, se lo cree todo, farolea mal.
	ArchetypeBuilder {}
		.p(&PsycheTraits::gullibility, 85).p(&PsycheTraits::composure, 20)
		.p(&PsycheTraits::attention, 25).p(&PsycheTraits::honesty, 80)
		.p(&PsycheTraits::deceit, 20)
		.s(&Skills::odds_math, 20).s(&Skills::bluffing, 10)
		.f(Flaw::Overconfidence).f(Flaw::Copycat)
		.build("Pardillo", Archetype::Pardillo),

	// Embustero: miente bien, sospecha, juega al despiste.
	ArchetypeBuilder {}
		.p(&PsycheTraits::deceit, 90).p(&PsycheTraits::composure, 80)
		.p(&PsycheTraits::suspicion, 75).p(&PsycheTraits::morality, 25)
		.s(&Skills::bluffing, 80).s(&Skills::trapping, 70).s(&Skills::tell_control, 75)
		.f(Flaw::Overconfidence)
		.build("Embustero", Archetype::Embustero),

	// Experimentado: lee, recuerda y ajusta; rutina explotable.
	ArchetypeBuilder {}
		.p(&PsycheTraits::composure, 85).p(&PsycheTraits::attention, 85)
		.p(&PsycheTraits::social_memory, 85)
		.s(&Skills::odds_math, 85).s(&Skills::opponent_reading, 85)
		.s(&Skills::history_memory, 80).s(&Skills::position_awareness, 80)
		.f(Flaw::Predictability)
		.build("Experimentado", Archetype::Experimentado),

	// Indeciso: duda de todo, tarda, teme perder.
	ArchetypeBuilder {}
		.p(&PsycheTraits::patience, 25).p(&PsycheTraits::impulsivity, 65)
		.p(&PsycheTraits::self_esteem, 25)
		.s(&Skills::odds_math, 40)
		.f(Flaw::AnalysisParalysis).f(Flaw::FearOfLoss)
		.build("Indeciso", Archetype::Indeciso),

	// Listillo: listo y vanidoso; contra-farol y comentarios.
	ArchetypeBuilder {}
		.p(&PsycheTraits::deceit, 85).p(&PsycheTraits::attention, 80)
		.p(&PsycheTraits::vanity, 80).p(&PsycheTraits::suspicion, 75)
		.s(&Skills::bluffing, 75).s(&Skills::opponent_reading, 70)
		.s(&Skills::deceit_detection, 70)
		.f(Flaw::Overconfidence)
		.build("Listillo", Archetype::Listillo),

	// Bobo: distraído e imprevisible sin querer.
	ArchetypeBuilder {}
		.p(&PsycheTraits::attention, 15).p(&PsycheTraits::gullibility, 80)
		.p(&PsycheTraits::concentration, 20)
		.s(&Skills::odds_math, 10)
		.f(Flaw::Copycat).f(Flaw::Boredom)
		.build("Bobo", Archetype::Bobo),

	// Engreído: prepotente, vanidoso, narcisista, flemático, poco empático.
	ArchetypeBuilder {}
		.b(&Personality::dominance, 85).b(&Personality::empathy, 15)
		.p(&PsycheTraits::vanity, 90).p(&PsycheTraits::self_esteem, 85)
		.p(&PsycheTraits::composure, 80).p(&PsycheTraits::temper, 25)
		.p(&PsycheTraits::morality, 25).p(&PsycheTraits::deceit, 70)
		.s(&Skills::table_presence, 75).s(&Skills::bluffing, 70)
		.f(Flaw::Overconfidence).f(Flaw::Predictability)
		.build("Engreído", Archetype::Engreido),

	// Paternalista: protector y generoso, se cree el mayor.
	ArchetypeBuilder {}
		.b(&Personality::empathy, 85).b(&Personality::dominance, 70)
		.p(&PsycheTraits::generosity, 80).p(&PsycheTraits::morality, 80)
		.s(&Skills::table_presence, 60)
		.build("Paternalista", Archetype::Paternalista),

	// Condescendiente: mira por encima del hombro.
	ArchetypeBuilder {}
		.p(&PsycheTraits::vanity, 85)
		.b(&Personality::empathy, 20).b(&Personality::dominance, 75)
		.p(&PsycheTraits::temper, 30)
		.f(Flaw::Overconfidence)
		.build("Condescendiente", Archetype::Condescendiente),

	// Idiota: sin atención ni cálculo; mina la mesa.
	ArchetypeBuilder {}
		.p(&PsycheTraits::attention, 10).p(&PsycheTraits::concentration, 15)
		.p(&PsycheTraits::gullibility, 85).p(&PsycheTraits::impulsivity, 80)
		.s(&Skills::odds_math, 5)
		.f(Flaw::ChasingLosses).f(Flaw::Denial)
		.build("Idiota", Archetype::Idiota),

	// Sabio: sereno, atento, moral; casi imposible de leer.
	ArchetypeBuilder {}
		.b(&Personality::autonomy, 70)
		.p(&PsycheTraits::morality, 90).p(&PsycheTraits::patience, 85)
		.p(&PsycheTraits::attention, 90).p(&PsycheTraits::composure, 90)
		.s(&Skills::odds_math, 85).s(&Skills::opponent_reading, 85)
		.s(&Skills::tell_control, 90)
		.build("Sabio", Archetype::Sabio),

	// Ignorante: no calcula ni atiende; fácil de explotar.
	ArchetypeBuilder {}
		.p(&PsycheTraits::attention, 20).p(&PsycheTraits::patience, 30)
		.s(&Skills::odds_math, 10)
		.f(Flaw::Boredom).f(Flaw::Copycat)
		.build("Ignorante", Archetype::Ignorante),

	// Malo: juega sucio y puede intimidar.
	ArchetypeBuilder {}
		.b(&Personality::aggression, 85).b(&Personality::empathy, 15)
		.p(&PsycheTraits::morality, 10).p(&PsycheTraits::deceit, 80)
		.s(&Skills::table_presence, 70).s(&Skills::bluffing, 70)
		.f(Flaw::Vengeance).f(Flaw::Greed)
		.build("Malo", Archetype::Malo),

	// Envidioso: persigue al que va ganando.
	ArchetypeBuilder {}
		.p(&PsycheTraits::vanity, 75).b(&Personality::empathy, 25)
		.f(Flaw::ChasingLosses).f(Flaw::Vengeance)
		.build("Envidioso", Archetype::Envidoso),

	// Intolerante: se descontrola con según quién.
	ArchetypeBuilder {}
		.p(&PsycheTraits::temper, 85).b(&Personality::empathy, 20)
		.p(&PsycheTraits::patience, 25)
		.f(Flaw::Paranoia).f(Flaw::Stubbornness)
		.build("Intolerante", Archetype::Intolerante),

	// Amigable: ambiente distendido, juega blando.
	ArchetypeBuilder {}
		.b(&Personality::sociability, 85).b(&Personality::empathy, 80)
		.p(&PsycheTraits::generosity, 80).p(&PsycheTraits::morality, 75)
		.p(&PsycheTraits::honesty, 80)
		.s(&Skills::table_presence, 40)
		.build("Amigable", Archetype::Amigable),

	// Cenizo: ve mala suerte en todo.
	ArchetypeBuilder {}
		.p(&PsycheTraits::optimism, 15).p(&PsycheTraits::superstition, 80)
		.b(&Personality::nervousness, 75)
		.f(Flaw::Denial).f(Flaw::FearOfLoss)
		.build("Cenizo", Archetype::Cenizo),

	// Tristón: pasivo y sin ganas.
	ArchetypeBuilder {}
		.p(&PsycheTraits::optimism, 20).b(&Personality::sociability, 25)
		.f(Flaw::Drowsiness).f(Flaw::FearOfLoss)
		.build("Tristón", Archetype::Triston),

	// Alegre: sube el ritmo, farolea por diversión.
	ArchetypeBuilder {}
		.p(&PsycheTraits::optimism, 90).b(&Personality::sociability, 80)
		.p(&PsycheTraits::humor, 80)
		.f(Flaw::Overconfidence).f(Flaw::Boredom)
		.build("Alegre", Archetype::Alegre),

	// Chistoso: distrae a la mesa con bromas.
	ArchetypeBuilder {}
		.p(&PsycheTraits::humor, 90).p(&PsycheTraits::talkativeness, 90)
		.b(&Personality::sociability, 75)
		.f(Flaw::Distraction).f(Flaw::Boredom)
		.build("Chistoso", Archetype::Chistoso),

	// Impertinente: busca el pique.
	ArchetypeBuilder {}
		.p(&PsycheTraits::temper, 80).p(&PsycheTraits::talkativeness, 80)
		.b(&Personality::empathy, 20)
		.f(Flaw::Stubbornness)
		.build("Impertinente", Archetype::Impertinente),

	// Despistado: pierde tells y pagos.
	ArchetypeBuilder {}
		.p(&PsycheTraits::attention, 15).p(&PsycheTraits::concentration, 20)
		.p(&PsycheTraits::social_memory, 20)
		.f(Flaw::Distraction).f(Flaw::Boredom)
		.build("Despistado", Archetype::Despistado),

	// Esperanzador: confía en mejorar.
	ArchetypeBuilder {}
		.p(&PsycheTraits::optimism, 85).p(&PsycheTraits::patience, 80)
		.p(&PsycheTraits::morality, 75)
		.build("Esperanzador", Archetype::Esperanzador),

	// Ilusionado: se emociona y apuesta de más.
	ArchetypeBuilder {}
		.p(&PsycheTraits::optimism, 85).p(&PsycheTraits::risk, 85)
		.b(&Personality::curiosity, 80)
		.f(Flaw::Overconfidence).f(Flaw::ChasingLosses)
		.build("Ilusionado", Archetype::Ilusionado),

	// Enamorado: distraído; regala fichas al "ser querido".
	ArchetypeBuilder {}
		.b(&Personality::empathy, 85).b(&Personality::sociability, 75)
		.p(&PsycheTraits::attention, 25)
		.f(Flaw::Distraction).f(Flaw::FearOfLoss)
		.build("Enamorado", Archetype::Enamorado),

	// Irascible: se le ve todo; entra en tilt fácil.
	ArchetypeBuilder {}
		.p(&PsycheTraits::temper, 90).b(&Personality::aggression, 80)
		.p(&PsycheTraits::composure, 20)
		.f(Flaw::Tilt).f(Flaw::Vengeance)
		.build("Irascible", Archetype::Irascible),

	// --- Variantes adicionales ---
	ArchetypeBuilder {}
		.b(&Personality::nervousness, 85).p(&PsycheTraits::risk, 20)
		.f(Flaw::FearOfLoss)
		.build("Timorato", Archetype::Timorato),
	ArchetypeBuilder {}
		.p(&PsycheTraits::risk, 90).p(&PsycheTraits::impulsivity, 85)
		.f(Flaw::ChasingLosses)
		.build("Temerario", Archetype::Temerario),
	ArchetypeBuilder {}
		.p(&PsycheTraits::generosity, 10)
		.f(Flaw::Greed)
		.build("Avaro", Archetype::Avaro),
	ArchetypeBuilder {}
		.p(&PsycheTraits::generosity, 90)
		.build("Generoso", Archetype::Generoso),
	ArchetypeBuilder {}
		.b(&Personality::empathy, 15).p(&PsycheTraits::morality, 25)
		.f(Flaw::Vengeance).f(Flaw::Stubbornness)
		.build("Vengativo", Archetype::Vengativo),
	ArchetypeBuilder {}
		.p(&PsycheTraits::optimism, 15).p(&PsycheTraits::superstition, 85)
		.build("Fatalista", Archetype::Fatalista),
	ArchetypeBuilder {}
		.b(&Personality::sociability, 85).p(&PsycheTraits::temper, 75)
		.p(&PsycheTraits::impulsivity, 75)
		.build("Sanguíneo", Archetype::Sanguineo),
	ArchetypeBuilder {}
		.p(&PsycheTraits::concentration, 85).p(&PsycheTraits::patience, 80)
		.b(&Personality::diligence, 85)
		.s(&Skills::odds_math, 75)
		.build("Metódico", Archetype::Metodico),
	ArchetypeBuilder {}
		.p(&PsycheTraits::superstition, 90)
		.f(Flaw::Predictability)
		.build("Místico", Archetype::Mistico),
	ArchetypeBuilder {}
		.p(&PsycheTraits::social_memory, 85).b(&Personality::sociability, 40)
		.build("Nostálgico", Archetype::Nostalgico),
	ArchetypeBuilder {}
		.p(&PsycheTraits::honesty, 10).p(&PsycheTraits::deceit, 90)
		.p(&PsycheTraits::composure, 80)
		.s(&Skills::bluffing, 85).s(&Skills::trapping, 80)
		.build("Tramposo", Archetype::Tramposo),
	ArchetypeBuilder {}
		.p(&PsycheTraits::gullibility, 90).p(&PsycheTraits::honesty, 85)
		.build("Inocente", Archetype::Inocente),
};

/// Definición del arquetipo pedido.
[[nodiscard]] constexpr const ArchetypeDef& archetype_def(Archetype a) noexcept {
	return kArchetypes[static_cast<eng::usize>(a)];
}

/// Nombre legible de un arquetipo.
[[nodiscard]] constexpr const char* archetype_name(Archetype a) noexcept {
	return archetype_def(a).name;
}

/// Arquetipo cuyo nombre coincide (comparación byte a byte). `Archetype::Count` si no hay.
[[nodiscard]] constexpr Archetype archetype_of(const char* name) noexcept {
	for (eng::usize i = 0u; i < archetype_count; ++i) {
		const char* a = kArchetypes[i].name;
		const char* b = name;
		bool same = true;
		while (*a != '\0' || *b != '\0') {
			if (*a != *b) {
				same = false;
				break;
			}
			++a;
			++b;
		}
		if (same) {
			return static_cast<Archetype>(i);
		}
	}
	return Archetype::Count;
}

} // namespace eng::sim
