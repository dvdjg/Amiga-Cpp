#pragma once

/// \file world.hpp
/// `eng::sim::SimWorld`: el **contenedor del ecosistema** y su LOD de simulación. Añade al
/// núcleo (`SimWorldCore`, `world_core.hpp`) la planificación, la reproducción, los
/// objetos/economía, la percepción/memoria, el lenguaje y el mapa mental. Ver
/// `docs/engine/architecture/SIM_ECOSYSTEM.md`.

#include <eng/sim/world_core.hpp>

namespace eng::sim {

template <class Traits = SimTraits, eng::u16 MaxCreatures = 64u,
	  eng::u8 MaxTrackers = kDefaultMaxTrackers, eng::u8 MaxRelations = kDefaultMaxRelations,
	  eng::u8 MaxRooms = 64u, eng::u8 MaxPlans = 8u, eng::u16 PlannerNodes = 64u,
	  class AiT = SimGoap>
class SimWorld : public SimWorldCore<Traits, MaxCreatures, MaxTrackers, MaxRelations, MaxRooms, MaxPlans, PlannerNodes, AiT> {
public:
	using Creature = AbstractCreature<MaxTrackers, MaxRelations>;
	using Ai = AiT;
	using Plan = detail::ActivePlan<kMaxPlanSteps>;
	static constexpr eng::u8 max_plan_steps = kMaxPlanSteps;

	// --- Planificacion (solo si Traits::planning) ---

	/// Planifica para `id` desde `start` hacia `goal` sobre el dominio `actions`. Devuelve
	/// `true` si hay plan (o el objetivo ya se cumple). El plan queda asociado a la
	/// criatura y se consume con `current_action`/`advance_plan`.
	template <class Actions>
	[[nodiscard]] constexpr bool replan(EntityId id, const typename Ai::State& start,
					    const typename Ai::Goal& goal,
					    Actions actions) noexcept {
		if constexpr (!Traits::planning) {
			(void)id;
			(void)start;
			(void)goal;
			(void)actions;
			return false;
		} else {
			if (!this->find(id).valid()) {
				return false;
			}
			const eng::Span<const typename Ai::Action> span {
				actions.data(), actions.size()};
			if (!this->m_planner.driver.replan(start, goal, span)) {
				abort_plan(id);
				return false;
			}
			auto p = this->get_or_make_plan(id);
			if (!p.valid()) {
				return false;
			}
			p->runner = this->m_planner.driver.runner();
			return true;
		}
	}

	/// ¿Toca (re)planificar para `id` en `frame_now`? Aplica la **histéresis** sobre el
	/// `Mind` de la criatura (`should_replan`) y **actualiza su estado**. El juego llama a
	/// esto en su bucle; si devuelve `true`, aplica el presupuesto (`apply_budget`) y llama a
	/// `replan`. Ver HOST-155.
	[[nodiscard]] constexpr bool decide_plan(EntityId id, eng::u16 frame_now,
						 const PlanParams& params = PlanParams {}) noexcept {
		if constexpr (!Traits::planning) {
			(void)id;
			(void)frame_now;
			(void)params;
			return false;
		} else {
			auto c = this->find(id);
			if (!c.valid()) {
				return false;
			}
			return should_replan(c->personality, c->mind.plan, frame_now, params);
		}
	}

	/// Bucle de planificación **completo en un tick**: si toca (`decide_plan`), aplica el
	/// presupuesto (`apply_budget`) y replanifica; si no, deja el plan que hubiera. El juego
	/// solo aporta el **dominio** (estado, objetivo y acciones); no repite el bucle.
	/// Devuelve si queda un plan activo. Ver HOST-313.
	template <class Actions>
	[[nodiscard]] constexpr bool plan_tick(EntityId id, const typename Ai::State& start,
					       const typename Ai::Goal& goal, Actions actions,
					       eng::u16 frame_now,
					       const PlanParams& params = PlanParams {}) noexcept {
		if constexpr (!Traits::planning) {
			(void)id;
			(void)start;
			(void)goal;
			(void)actions;
			(void)frame_now;
			(void)params;
			return false;
		} else {
			if (!decide_plan(id, frame_now, params)) {
				return has_plan(id);
			}
			apply_budget(this->m_planner.driver, params);
			return replan(id, start, goal, actions);
		}
	}

	/// Asocia a `id` el plan ya calculado por un conductor externo (p. ej. un `HtnDriver`),
	/// para consumirlo con `current_action`/`advance_plan`/`abort_plan` igual que uno del
	/// GOAP. Ver HOST-313.
	[[nodiscard]] constexpr bool store_plan(EntityId id,
						const PlanRunner<kMaxPlanSteps>& runner) noexcept {
		if constexpr (!Traits::planning) {
			(void)id;
			(void)runner;
			return false;
		} else {
			if (!this->find(id).valid()) {
				return false;
			}
			auto p = this->get_or_make_plan(id);
			if (!p.valid()) {
				return false;
			}
			p->runner = runner;
			return true;
		}
	}

	/// Nodos expandidos por la **última** búsqueda del planificador compartido (diagnóstico y
	/// benchmark; 0 si la planificación está desactivada o no hubo búsqueda).
	[[nodiscard]] constexpr eng::usize planner_expansions() const noexcept {
		if constexpr (!Traits::planning) {
			return 0u;
		} else {
			return this->m_planner.driver.expansions();
		}
	}

	[[nodiscard]] constexpr bool has_plan(EntityId id) const noexcept {
		auto p = this->find_plan(id);
		return p.valid() && p->runner.active;
	}

	[[nodiscard]] constexpr eng::u16 current_action(EntityId id) const noexcept {
		auto p = this->find_plan(id);
		return p.valid() ? p->runner.current() : static_cast<eng::u16>(0xffffu);
	}

	constexpr void advance_plan(EntityId id) noexcept {
		if (auto p = this->find_plan(id); p.valid()) {
			p->runner.advance();
		}
	}

	constexpr void abort_plan(EntityId id) noexcept {
		if (auto p = this->find_plan(id); p.valid()) {
			p->runner.abort();
		}
	}

	[[nodiscard]] constexpr eng::u8 planning_count() const noexcept {
		eng::u8 n = 0u;
		for (eng::usize i = 0; i < this->m_plans.size(); ++i) {
			if (this->m_plans[i].runner.active) {
				++n;
			}
		}
		return n;
	}

	// --- Reproduccion ---

	/// Una criatura realizada, madura y bien alimentada con una pareja de vínculo
	/// suficiente engendra una cría (genoma heredado). Devuelve el id de la cría.
	template <class Rng>
	[[nodiscard]] constexpr EntityId try_reproduce(Rng& rng) noexcept {
		(void)rng; // la gestación no necesita azar hasta el parto
		for (eng::usize i = 0; i < this->m_creatures.size(); ++i) {
			Creature& c = this->m_creatures[i];
			if (!c.alive() || !c.realized() || !c.repro.ready() ||
			    !this->region_has_space(c.room) ||
			    !can_reproduce(c.age, c.health, c.needs.hunger, c.needs.fatigue, this->m_life)) {
				continue;
			}
			for (eng::usize r = 0; r < c.relationships.size(); ++r) {
				const Relationship& rel = c.relationships[r];
				if (rel.kind != RelationKind::Mate) {
					continue;
				}
				const eng::s16 mag = rel.affect < 0 ? static_cast<eng::s16>(-rel.affect)
								    : static_cast<eng::s16>(rel.affect);
				if (mag < this->m_life.repro_min_bond) {
					continue;
				}
				auto mate = this->find(rel.target);
				if (!mate.valid() || !mate->alive()) {
					continue;
				}
				// Inicia la gestación (el nacimiento ocurre al avanzar el ciclo).
				c.repro.gestation = this->m_life.gestation_ticks;
				c.repro.partner = mate->id;
				c.repro.cooldown = this->m_life.repro_cooldown;
				mate->repro.cooldown = this->m_life.repro_cooldown;
				rise(c.needs, Need::Hunger, this->m_life.parental_cost);
				rise(mate->needs, Need::Hunger, this->m_life.parental_cost);
				return c.id;
			}
		}
		return no_entity;
	}

	/// Avanza cooldowns y gestaciones; cuando una termina, nace la cría (genoma heredado de
	/// la pareja). Devuelve cuántos nacimientos se produjeron. El juego la llama cada frame.
	template <class Rng>
	constexpr eng::u8 advance_reproduction(Rng& rng) noexcept {
		eng::u8 births = 0u;
		for (eng::usize i = 0; i < this->m_creatures.size(); ++i) {
			Creature& c = this->m_creatures[i];
			if (!c.alive()) {
				continue;
			}
			if (c.repro.cooldown > 0u) {
				--c.repro.cooldown;
			}
			if (c.repro.gestation > 0u) {
				--c.repro.gestation;
				if (c.repro.gestation == 0u) {
					auto mate = this->find(c.repro.partner);
					const Genome child_g = newborn_genome(
						c.genome, mate.valid() ? mate->genome : c.genome, rng, this->m_gene);
					if (this->spawn_with_genome(c.species, c.faction, c.room, c.x, c.y,
							      child_g) != no_entity) {
						++births;
					}
					c.repro.partner = no_entity;
				}
			}
		}
		return births;
	}

	/// Una reina de enjambre pone un huevo de la casta más necesaria (según el censo) y lo
	/// sesga genéticamente hacia esa casta. Devuelve el id de la cría.
	template <class Rng>
	[[nodiscard]] constexpr EntityId lay_brood(Rng& rng,
						   const CasteParams& cp = CasteParams {}) noexcept {
		if constexpr (!Traits::society) {
			(void)rng;
			(void)cp;
			return no_entity;
		} else {
			for (eng::usize i = 0; i < this->m_creatures.size(); ++i) {
				Creature& c = this->m_creatures[i];
				if (!c.alive() || !c.realized() || !this->region_has_space(c.room) ||
				    caste_of(c.genome, cp) != Caste::Queen) {
					continue;
				}
				const Caste needed = this->m_colony.needed_caste(this->m_colony_params);
				Genome g = bias_for_caste(Genome::random(rng), needed, cp);
				const EntityId child = this->spawn_with_genome(c.species, c.faction, c.room, c.x,
									 c.y, g);
				if (child == no_entity) {
					return no_entity;
				}
				this->m_colony.note_birth(needed);
				return child;
			}
			return no_entity;
		}
	}

	// --- Objetos y economia ---

	/// Ejecuta materialmente un paso del dominio sobre la criatura (inventario) y el mundo.
	constexpr ActionResult execute_action(EntityId id, SimActionKind a) noexcept {
		auto c = this->find(id);
		if (!c.valid() || !c->alive()) {
			return ActionResult::Unknown;
		}
		return execute_domain_action(c->carrying, a, &this->m_items, c->room, c->x, c->y);
	}

	[[nodiscard]] constexpr ItemStore& items() noexcept { return this->m_items; }
	[[nodiscard]] constexpr const ItemStore& items() const noexcept { return this->m_items; }
	[[nodiscard]] constexpr Economy& economy() noexcept { return this->m_economy; }
	[[nodiscard]] constexpr const Economy& economy() const noexcept { return this->m_economy; }

	constexpr void set_economy_params(const EconomyParams& p) noexcept {
		this->m_econ_params = p;
		this->m_economy.reset(p);
	}
	constexpr void set_learning(const LearningParams& p) noexcept { this->m_learning = p; }

	/// Regalo de una facción a otra: sube la reputación del receptor y la demanda del
	/// objeto. Devuelve la reputación ganada.
	constexpr eng::s16 offer_gift(FactionId from, FactionId to, ItemKind k,
				      eng::u8 amount) noexcept {
		(void)from;
		return give_gift(this->m_society, this->m_economy, to, k, amount, this->m_econ_params);
	}

	/// **Trueque** entre dos criaturas: intercambia objetos a precio y sube su reputación.
	/// Devuelve `true` si el trato se cerró.
	constexpr bool offer_trade(EntityId a, EntityId b, const TradeOffer& offer,
				   const TradeParams& p = TradeParams {}) noexcept {
		auto ca = this->find(a);
		auto cb = this->find(b);
		if (!ca.valid() || !cb.valid()) {
			return false;
		}
		if (!execute_trade(ca->carrying, cb->carrying, this->m_economy, offer, p)) {
			return false;
		}
		this->m_society.adjust(ca->faction, p.rep_gain);
		this->m_society.adjust(cb->faction, p.rep_gain);
		return true;
	}


	// --- Percepcion y memoria ---

	constexpr void set_sense_params(const SenseParams& p) noexcept { this->m_sense_params = p; }
	constexpr void set_memory_params(const MemoryParams& p) noexcept { this->m_memory_params = p; }

	/// Percibe el entorno: rellena `out` con las observaciones del observador (según sus
	/// sentidos y su orientación). La novedad se decide con su memoria de corto y largo
	/// plazo. Devuelve cuántas observaciones produjo.
	[[nodiscard]] constexpr eng::u8 sense(EntityId observer,
					      eng::Span<const SenseTarget> targets,
					      eng::Span<Observation> out, eng::s16 face_x = 1,
					      eng::s16 face_y = 0) const noexcept {
		auto c = this->find(observer);
		if (!c.valid()) {
			return 0u;
		}
		Observer o {};
		o.room = c->room;
		o.x = c->x;
		o.y = c->y;
		o.face_x = face_x;
		o.face_y = face_y;
		const auto known = [&](EntityId id) noexcept {
			if (id == observer) {
				return true;
			}
			if (working_strength(c->trackers, id) > 0u) {
				return true;
			}
			return confidence_for(c->knowledge, KnowledgeKind::Enemy, id) > 0u ||
			       confidence_for(c->knowledge, KnowledgeKind::Ally, id) > 0u;
		};
		// El estado interno (miedo/ira) estrecha o agudiza los sentidos.
		const Senses eff = focused(c->senses, c->mind.emotions.fear, c->mind.emotions.anger,
					   this->m_attention_params);
		return perceive(eff, o, targets, out, known, this->m_sense_params);
	}

	/// Integra las observaciones en la memoria de corto plazo del observador.
	constexpr void integrate_senses(EntityId id,
					eng::Span<const Observation> observations) noexcept {
		auto c = this->find(id);
		if (!c.valid()) {
			return;
		}
		integrate_observations(c->trackers, observations, this->m_frame);
	}

	/// Consolida la memoria de corto plazo en largo plazo (una criatura).
	constexpr eng::u8 consolidate_memory(EntityId id) noexcept {
		auto c = this->find(id);
		if (!c.valid()) {
			return 0u;
		}
		return consolidate(c->trackers, c->knowledge, this->m_memory_params);
	}

	/// Tick de memoria: olvido del corto plazo y consolidación periódica de lo realizado.
	constexpr eng::u8 tick_memory() noexcept {
		eng::u8 n = 0u;
		const bool due = this->m_memory_params.consolidation_period == 0u ||
				 (this->m_frame % this->m_memory_params.consolidation_period) == 0u;
		for (eng::usize i = 0; i < this->m_creatures.size(); ++i) {
			Creature& c = this->m_creatures[i];
			if (!c.alive() || !c.realized()) {
				continue;
			}
			forget_working(c.trackers, this->m_memory_params);
			if (due) {
				n = u8_sat_add(n, consolidate(c.trackers, c.knowledge, this->m_memory_params));
				decay_places(c.knowledge, this->m_memory_params.place_decay);
			}
		}
		return n;
	}

	/// Deriva los sentidos del genoma de una criatura (al nacer o al cambiar de morfo).
	constexpr void derive_senses(EntityId id) noexcept {
		auto c = this->find(id);
		if (c.valid()) {
			c->senses = senses_from_genome(c->genome, this->m_sense_genome_params);
		}
	}

	constexpr void set_attention_params(const AttentionParams& p) noexcept {
		this->m_attention_params = p;
	}
	constexpr void set_sense_genome_params(const SenseGenomeParams& p) noexcept {
		this->m_sense_genome_params = p;
	}

	// --- Mapa mental aplicado al mundo (ruta fina y flujo) ---

	constexpr void set_mental_map_params(const MentalMapParams& p) noexcept {
		this->m_mental_params = p;
	}
	[[nodiscard]] constexpr const MentalMapParams& mental_map_params() const noexcept {
		return this->m_mental_params;
	}

	/// Sesgo de una región según lo que recuerda la criatura (macro).
	[[nodiscard]] constexpr eng::s16 mental_bias(EntityId id, RoomId room) const noexcept {
		auto c = this->find(id);
		return c.valid() ? place_bias(c->knowledge, room, this->m_mental_params) : 0;
	}

	/// Construye la capa de coste del mapa mental de una criatura para `eng::util::astar`.
	/// `room_at(idx)` mapea cada celda a su región (lo aporta el juego).
	template <eng::u16 W, eng::u16 H, class RoomAt>
	constexpr void stamp_mental_overlay(EntityId id, MentalOverlay<W, H>& overlay,
					    RoomAt room_at) const noexcept {
		auto c = this->find(id);
		if (c.valid()) {
			overlay.stamp(c->knowledge, room_at, this->m_mental_params);
		}
	}

	/// Deposita el peligro recordado por una criatura en un mapa de influencia.
	template <class Map, class RoomAt>
	constexpr void stamp_mental_danger(EntityId id, Map& influence,
					   RoomAt room_at) const noexcept {
		auto c = this->find(id);
		if (c.valid()) {
			deposit_mental_danger(c->knowledge, influence, room_at, this->m_mental_params);
		}
	}

	// --- Lenguaje y gestos ---

	constexpr void set_signal_params(const SignalParams& p) noexcept { this->m_signal_params = p; }

	/// Emite y entrega señales entre las criaturas realizadas: cada una comunica su
	/// conducta/emoción y las que la oyen (misma región, dentro de alcance) actualizan su
	/// memoria de corto plazo y su estado afectivo. Devuelve cuántas señales se oyeron.
	constexpr eng::u8 broadcast_signals() noexcept {
		eng::u8 heard = 0u;
		for (eng::usize i = 0; i < this->m_creatures.size(); ++i) {
			Creature& c = this->m_creatures[i];
			if (!c.alive() || !c.realized()) {
				continue;
			}
			const Signal s = make_signal(c.id, c.faction, c.room, c.x, c.y, c.behavior,
						     c.mind, c.senses, this->m_signal_params);
			if (s.intensity < this->m_signal_params.intensity_min) {
				continue;
			}
			heard = u8_sat_add(heard, this->deliver_signal(s, i));
		}
		return heard;
	}

	/// Actúa un **ritual** (cultura): efecto emocional propio y lo expresa con una señal a
	/// los de su región. Devuelve cuántos lo percibieron.
	constexpr eng::u8 enact_ritual(EntityId id, RitualKind r) noexcept {
		auto c = this->find(id);
		if (!c.valid()) {
			return 0u;
		}
		perform_ritual(c->mind, r, this->m_culture_params);
		Signal s {};
		s.sender = c->id;
		s.faction = c->faction;
		s.room = c->room;
		s.x = c->x;
		s.y = c->y;
		s.kind = signal_for_ritual(r);
		s.intensity = this->m_pack_params.call_intensity;
		s.range = u8_sat_add(this->m_pack_params.call_range,
				     static_cast<eng::u8>(c->senses.hearing / 8u));
		eng::usize self = 0u;
		for (eng::usize k = 0; k < this->m_creatures.size(); ++k) {
			if (this->m_creatures[k].id == c->id) {
				self = k;
				break;
			}
		}
		return this->deliver_signal(s, self);
	}

	/// **Coordina las manadas**: cada líder con una presa percibida envía a sus miembros
	/// (relaciones `Pack` hacia él) a posiciones de flanqueo alrededor del objetivo. Los
	/// miembros pasan a cazar y orientan su tracker de presa al punto que les toca.
	constexpr eng::u8 coordinate_packs() noexcept {
		eng::u8 coordinated = 0u;
		for (eng::usize i = 0; i < this->m_creatures.size(); ++i) {
			Creature& leader = this->m_creatures[i];
			if (!leader.alive() || !leader.realized()) {
				continue;
			}
			auto prey = best_attention_tracker(leader.trackers, TrackerKind::Prey);
			if (!prey.valid() || prey->room != leader.room) {
				continue;
			}
			eng::u8 idx = 0u;
			for (eng::usize j = 0; j < this->m_creatures.size(); ++j) {
				if (i == j) {
					continue;
				}
				Creature& m = this->m_creatures[j];
				if (!m.alive() || !m.realized() || m.room != leader.room) {
					continue;
				}
				auto rel = find_rel(m.relationships, leader.id);
				if (!rel.valid() || rel->kind != RelationKind::Pack) {
					continue;
				}
				const PackRole role = pack_role_for(false, idx);
				const eng::Point2s goal = flank_goal(eng::Point2s {prey->x, prey->y}, role,
								     idx, this->m_pack_params.flank_distance);
				observe(m.trackers, TrackerKind::Prey, prey->target, prey->room, goal.x,
					goal.y, prey->confidence, this->m_frame);
				m.behavior = Behavior::Hunt;
				++idx;
				++coordinated;
			}
		}
		return coordinated;
	}

	constexpr void set_culture_params(const CultureParams& p) noexcept { this->m_culture_params = p; }
	constexpr void set_pack_params(const PackParams& p) noexcept { this->m_pack_params = p; }

	// --- Mapa mental y rutas macro ---

	/// Ruta por el grafo de regiones (BFS). Escribe las regiones de `from` a `to` en `out`

	/// Difunde conocimiento: cada criatura realizada aporta lo suyo a la memoria de su
	/// facción y comparte con los correligionarios de su misma región; después la memoria
	/// colectiva se traduce en reputación y demanda (rumores). Devuelve cuántos eventos
	/// de difusión ocurrieron.
	constexpr eng::u8 diffuse_knowledge() noexcept {
		if constexpr (!Traits::knowledge) {
			return 0u;
		} else {
			eng::u8 events = 0u;
			for (eng::usize i = 0; i < this->m_creatures.size(); ++i) {
				Creature& c = this->m_creatures[i];
				if (!c.alive() || !c.realized()) {
					continue;
				}
				events = u8_sat_add(
					events, contribute(this->m_group_memory, c.faction, c.knowledge,
							   this->m_rumor_params, this->m_learning));
				for (eng::usize j = 0; j < this->m_creatures.size(); ++j) {
					Creature& d = this->m_creatures[j];
					if (j == i || !d.alive() || !d.realized() ||
					    d.faction != c.faction || d.room != c.room) {
						continue;
					}
					events = u8_sat_add(events,
							    share(c.knowledge, d.knowledge, this->m_learning));
				}
			}
			for (eng::u8 f = 0; f < kMaxFactions; ++f) {
				(void)apply_group_knowledge(this->m_society, this->m_economy, f, this->m_group_memory,
							    this->m_rumor_params);
			}
			return events;
		}
	}

};

} // namespace eng::sim
