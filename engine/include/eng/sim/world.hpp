#pragma once

/// \file world.hpp
/// `eng::sim::SimWorld`: el **contenedor del ecosistema** y su LOD de simulación. Reúne
/// un array fijo de criaturas abstractas, el grafo de habitaciones y la sociedad, y
/// decide qué se simula con detalle:
///
/// - **Plano realizado** (`tick_realized`): las criaturas cercanas a la cámara se marcan
///   con `flags::realized`; cada frame se actualizan sus necesidades, se envejecen sus
///   trackers, se actualiza su mente y se elige comportamiento. Aquí se ejecuta la IA
///   completa y (fuera del engine) el pathfinding y la física.
/// - **Plano abstracto** (`tick_abstract`): el resto del mundo sigue vivo de forma
///   barata y **escalonada** (`stagger_period`): las necesidades avanzan, los trackers se
///   olvidan más rápido y las criaturas migran entre habitaciones adyacentes hacia su
///   refugio cuando llueve. Sin pathfinding ni percepción.
///
/// El grafo de habitaciones es una adyacencia simple (hasta 4 vecinos por room): es la
/// columna del pathfinding macro off-screen. El `Projection` (2D/iso/3D) vive fuera de
/// esta clase: aquí solo hay estado de mundo abstracto.
///
/// Verificación: HOST-153.

#include <eng/core/types.hpp>
#include <eng/core/util/static_vector.hpp>
#include <eng/sim/behavior.hpp>
#include <eng/sim/biome.hpp>
#include <eng/sim/climate.hpp>
#include <eng/sim/colony.hpp>
#include <eng/sim/communication.hpp>
#include <eng/sim/creature.hpp>
#include <eng/sim/culture.hpp>
#include <eng/sim/economy.hpp>
#include <eng/sim/lifecycle.hpp>
#include <eng/sim/lod.hpp>
#include <eng/sim/memory.hpp>
#include <eng/sim/mental_map.hpp>
#include <eng/sim/object.hpp>
#include <eng/sim/pack.hpp>
#include <eng/sim/planner.hpp>
#include <eng/sim/rumor.hpp>
#include <eng/sim/society.hpp>
#include <eng/sim/terrain.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Número máximo de vecinos por habitación en el grafo simplificado.
inline constexpr eng::u8 kMaxRoomLinks = 4u;

namespace detail {

/// Plan activo de una criatura (id + pasos).
template <eng::u8 Steps>
struct ActivePlan {
	EntityId id = no_entity;
	PlanRunner<Steps> runner {};
};

/// Contenedor del planificador compartido; vacío si `Traits::planning` es falso, de modo
/// que no ocupa RAM cuando la planificación está desactivada.
template <bool Enable, eng::u16 Nodes, eng::u8 Steps>
struct PlannerHolder {};

template <eng::u16 Nodes, eng::u8 Steps>
struct PlannerHolder<true, Nodes, Steps> {
	PlannerDriver<Nodes, Steps> driver {};
};

} // namespace detail

/// El mundo de simulación. Los parámetros de plantilla fijan la capacidad exacta y los
/// rasgos activan/desactivan módulos en compilación (`Traits`).
template <class Traits = SimTraits, eng::u16 MaxCreatures = 64u,
	  eng::u8 MaxTrackers = kDefaultMaxTrackers, eng::u8 MaxRelations = kDefaultMaxRelations,
	  eng::u8 MaxRooms = 64u, eng::u8 MaxPlans = 8u, eng::u16 PlannerNodes = 64u>
class SimWorld {
public:
	using Creature = AbstractCreature<MaxTrackers, MaxRelations>;
	using Ai = SimGoap;
	using Plan = detail::ActivePlan<kMaxPlanSteps>;

	static constexpr eng::u8 max_plan_steps = kMaxPlanSteps;
	static_assert(MaxCreatures > 0u, "SimWorld: MaxCreatures > 0");
	static_assert(MaxRooms > 0u && MaxRooms < 255u, "SimWorld: 1..254 rooms");
	static_assert(sizeof(Creature) <= 384u,
		      "SimWorld: la criatura supera el presupuesto; reduce trackers/relaciones");

	// --- Población ---

	[[nodiscard]] constexpr eng::usize creature_count() const noexcept {
		return m_creatures.size();
	}
	[[nodiscard]] constexpr const Creature& creature(eng::usize index) const noexcept {
		return m_creatures[index];
	}
	[[nodiscard]] constexpr Creature& creature(eng::usize index) noexcept {
		return m_creatures[index];
	}

	/// Crea una criatura. Devuelve su `EntityId` (>= 1) o `no_entity` si no cabe. Reutiliza
	/// el hueco de una criatura **muerta** con un id **nuevo y monótono**, de modo que el
	/// mundo no crece indefinidamente y ninguna referencia antigua (trackers, relaciones)
	/// apunta por accidente a la criatura reciclada.
	constexpr EntityId spawn(SpeciesId species, FactionId faction, RoomId room, eng::s16 x,
				 eng::s16 y) noexcept {
		const EntityId id = m_next_id++;
		for (eng::usize i = 0; i < m_creatures.size(); ++i) {
			if (!m_creatures[i].alive()) {
				init_creature(m_creatures[i], id, species, faction, room, x, y);
				return id;
			}
		}
		if (m_creatures.full()) {
			m_next_id = id; // no se consumió
			return no_entity;
		}
		Creature c {};
		init_creature(c, id, species, faction, room, x, y);
		(void)m_creatures.push_back(c);
		return id;
	}

	/// Crea una criatura con genoma y personalidad derivada (cría). Nace en la etapa
	/// infantil según `LifecycleParams`.
	constexpr EntityId spawn_with_genome(SpeciesId species, FactionId faction, RoomId room,
					     eng::s16 x, eng::s16 y, const Genome& genome) noexcept {
		const EntityId id = spawn(species, faction, room, x, y);
		if (id == no_entity) {
			return no_entity;
		}
		Creature* c = find(id);
		c->genome = genome;
		c->personality = genome_to_personality(genome);
		c->senses = senses_from_genome(genome, m_sense_genome_params);
		c->age = 0u;
		c->health = m_life.newborn_health;
		return id;
	}

	/// Busca por id (recorrido lineal: el id es monótono y único, nunca se reutiliza).
	[[nodiscard]] constexpr Creature* find(EntityId id) noexcept {
		if (id == no_entity || id == 0u) {
			return nullptr;
		}
		for (eng::usize i = 0; i < m_creatures.size(); ++i) {
			if (m_creatures[i].id == id) {
				return &m_creatures[i];
			}
		}
		return nullptr;
	}
	[[nodiscard]] constexpr const Creature* find(EntityId id) const noexcept {
		if (id == no_entity || id == 0u) {
			return nullptr;
		}
		for (eng::usize i = 0; i < m_creatures.size(); ++i) {
			if (m_creatures[i].id == id) {
				return &m_creatures[i];
			}
		}
		return nullptr;
	}

	// --- Grafo de habitaciones ---

	constexpr void link_rooms(RoomId a, RoomId b) noexcept {
		if (a >= MaxRooms || b >= MaxRooms || a == b) {
			return;
		}
		(void)push_link(a, b);
		(void)push_link(b, a);
	}

	[[nodiscard]] constexpr bool rooms_adjacent(RoomId a, RoomId b) const noexcept {
		if (a >= MaxRooms) {
			return false;
		}
		for (eng::usize i = 0; i < m_links[a].size(); ++i) {
			if (m_links[a][i] == b) {
				return true;
			}
		}
		return false;
	}

	[[nodiscard]] constexpr eng::u8 room_degree(RoomId a) const noexcept {
		return a < MaxRooms ? static_cast<eng::u8>(m_links[a].size()) : 0u;
	}

	[[nodiscard]] constexpr RoomId room_link(RoomId a, eng::u8 i) const noexcept {
		return (a < MaxRooms && i < m_links[a].size()) ? m_links[a][i] : no_room;
	}

	// --- Clima/peligro ambiental, terreno y sociedad ---

	/// Activa (o desactiva) un peligro ambiental en todas las regiones. Para clima por
	/// región, usa `climate().set(room, kind, severity)`.
	constexpr void set_hazard(HazardKind kind, eng::u8 severity) noexcept {
		for (eng::u8 r = 0; r < MaxRooms; ++r) {
			m_climate.set(r, kind, severity);
		}
	}

	/// Atajo de compatibilidad para la lluvia.
	constexpr void set_rain(bool approaching) noexcept {
		set_hazard(HazardKind::Rain, approaching ? static_cast<eng::u8>(200u) : 0u);
	}

	[[nodiscard]] constexpr HazardKind hazard() const noexcept {
		const RoomId r = m_climate.strongest();
		return r == no_room ? HazardKind::None : m_climate.at(r).kind;
	}
	[[nodiscard]] constexpr eng::u8 severity() const noexcept { return m_climate.max_severity(); }
	[[nodiscard]] constexpr bool environment_severe() const noexcept {
		return m_climate.max_severity() >= 100u;
	}

	/// Clima por región (formar/disipar peligros, consultar severidad).
	[[nodiscard]] constexpr Climate<MaxRooms>& climate() noexcept { return m_climate; }
	[[nodiscard]] constexpr const Climate<MaxRooms>& climate() const noexcept { return m_climate; }
	constexpr void tick_climate(eng::u8 decay) noexcept { m_climate.tick(decay); }

	/// Propaga el clima entre regiones vecinas (**frentes**): cada peligro salta una
	/// fracción `spread` (%) a sus vecinas y luego se disipa por `decay`. Así una tormenta
	/// avanza por el mapa en vez de estar congelada por región.
	constexpr void diffuse_climate(eng::u8 spread, eng::u8 decay) noexcept {
		eng::u8 add[MaxRooms] {};
		HazardKind add_kind[MaxRooms] {};
		for (eng::u8 r = 0; r < MaxRooms; ++r) {
			const eng::u8 sev = m_climate.severity(r);
			if (sev == 0u) {
				continue;
			}
			const eng::u8 front = u8_scale(sev, spread);
			if (front == 0u) {
				continue;
			}
			const HazardKind kind = m_climate.at(r).kind;
			for (eng::usize k = 0; k < m_links[r].size(); ++k) {
				const RoomId n = m_links[r][k];
				if (n < MaxRooms) {
					if (add[n] == 0u) {
						add_kind[n] = kind;
					}
					add[n] = u8_sat_add(add[n], front);
				}
			}
		}
		for (eng::u8 r = 0; r < MaxRooms; ++r) {
			if (add[r] > 0u) {
				m_climate.add(r, add_kind[r], add[r]);
			}
		}
		m_climate.tick(decay);
	}

	/// Clasificación del terreno por región (representación del mundo para el movimiento).
	constexpr void set_region(RoomId r, const RegionTerrain& rt) noexcept {
		if (r < MaxRooms) {
			m_regions[r] = rt;
		}
	}
	constexpr void set_terrain(RoomId r, TerrainKind k) noexcept {
		if (r < MaxRooms) {
			m_regions[r].dominant = k;
		}
	}
	[[nodiscard]] constexpr RegionTerrain region(RoomId r) const noexcept {
		return r < MaxRooms ? m_regions[r] : RegionTerrain {};
	}
	[[nodiscard]] constexpr TerrainKind terrain(RoomId r) const noexcept {
		return region(r).dominant;
	}
	/// ¿Una criatura con esas capacidades puede moverse por la región?
	[[nodiscard]] constexpr bool region_passable(RoomId r, eng::u8 movement_flags) const noexcept {
		return r < MaxRooms && can_traverse(movement_flags, m_regions[r].dominant);
	}

	/// Aplica un **evento de terreno** en caliente (derrumbe, inundación, incendio,
	/// regeneración): cambia el terreno y, si procede, sube/limpia el clima de la región.
	constexpr void apply_terrain_event(RoomId r, TerrainEvent e) noexcept {
		apply_terrain_event(r, e, m_terrain_events);
	}
	constexpr void apply_terrain_event(RoomId r, TerrainEvent e,
					   const TerrainEventParams& p) noexcept {
		if (r >= MaxRooms) {
			return;
		}
		m_regions[r].dominant = terrain_after_event(m_regions[r].dominant, e, p);
		const HazardKind hk = hazard_from_event(e);
		if (hk != HazardKind::None) {
			m_climate.add(r, hk, hazard_severity_from_event(e, p));
		} else if (e == TerrainEvent::Regrowth) {
			m_climate.set(r, HazardKind::None, 0u);
		}
	}
	constexpr void set_terrain_event_params(const TerrainEventParams& p) noexcept {
		m_terrain_events = p;
	}

	/// **Bioma de una región**: vuelca su perfil en el terreno (dominante, abrigo, peligro).
	constexpr void set_biome(RoomId r, BiomeKind b) noexcept {
		if (r >= MaxRooms) {
			return;
		}
		m_biome[r] = b;
		const BiomeProfile prof = biome_profile(b);
		m_regions[r].dominant = prof.dominant;
		m_regions[r].shelter = prof.shelter;
		m_regions[r].danger = prof.danger;
	}

	/// Como `set_biome` y, si `seed_climate`, siembra el clima típico del bioma.
	constexpr void apply_biome(RoomId r, BiomeKind b, bool seed_climate = false,
				   eng::u8 severity = 120u) noexcept {
		set_biome(r, b);
		if (!seed_climate) {
			return;
		}
		const BiomeProfile prof = biome_profile(b);
		if (prof.typical_hazard != HazardKind::None) {
			m_climate.set(r, prof.typical_hazard, severity);
		}
	}

	[[nodiscard]] constexpr BiomeKind biome(RoomId r) const noexcept {
		return r < MaxRooms ? m_biome[r] : BiomeKind::Plains;
	}

	[[nodiscard]] constexpr Society& society() noexcept { return m_society; }
	[[nodiscard]] constexpr const Society& society() const noexcept { return m_society; }

	[[nodiscard]] constexpr GroupMemory<kMaxFactions>& group_memory() noexcept {
		return m_group_memory;
	}
	[[nodiscard]] constexpr const GroupMemory<kMaxFactions>& group_memory() const noexcept {
		return m_group_memory;
	}
	constexpr void set_rumor_params(const RumorParams& p) noexcept { m_rumor_params = p; }

	constexpr void set_lifecycle(const LifecycleParams& p) noexcept { m_life = p; }
	[[nodiscard]] constexpr const LifecycleParams& lifecycle() const noexcept { return m_life; }
	constexpr void set_genetics(const GeneticsParams& p) noexcept { m_gene = p; }
	constexpr void set_colony(const ColonyParams& p) noexcept { m_colony_params = p; }
	[[nodiscard]] constexpr Colony& colony() noexcept { return m_colony; }
	[[nodiscard]] constexpr const Colony& colony() const noexcept { return m_colony; }

	[[nodiscard]] constexpr eng::u16 frame() const noexcept { return m_frame; }

	// --- LOD: marcar el conjunto realizado ---

	/// Marca como realizadas hasta `max_realized` criaturas vivas que estén en `room`;
	/// el resto de las de esa room quedan abstractas. Las de otras rooms no se tocan.
	constexpr void realize_room(RoomId room, eng::u8 max_realized) noexcept {
		eng::u8 n = 0u;
		for (eng::usize i = 0; i < m_creatures.size(); ++i) {
			Creature& c = m_creatures[i];
			if (c.room != room || !c.alive()) {
				continue;
			}
			if (n < max_realized) {
				c.set_realized(true);
				++n;
			} else {
				c.set_realized(false);
			}
		}
	}

	constexpr void clear_realized() noexcept {
		for (eng::usize i = 0; i < m_creatures.size(); ++i) {
			m_creatures[i].set_realized(false);
		}
	}

	/// Número de criaturas actualmente realizadas.
	[[nodiscard]] constexpr eng::u8 realized_count() const noexcept {
		eng::u8 n = 0u;
		for (eng::usize i = 0; i < m_creatures.size(); ++i) {
			if (m_creatures[i].alive() && m_creatures[i].realized()) {
				++n;
			}
		}
		return n;
	}

	/// Criaturas vivas no realizadas ni dormidas (tick abstracto).
	[[nodiscard]] constexpr eng::u8 abstract_count() const noexcept {
		eng::u8 n = 0u;
		for (eng::usize i = 0; i < m_creatures.size(); ++i) {
			const Creature& c = m_creatures[i];
			if (c.alive() && !c.realized() && !c.dormant()) {
				++n;
			}
		}
		return n;
	}

	/// Criaturas vivas dormidas (fuera del LOD; no cuestan CPU).
	[[nodiscard]] constexpr eng::u8 dormant_count() const noexcept {
		eng::u8 n = 0u;
		for (eng::usize i = 0; i < m_creatures.size(); ++i) {
			const Creature& c = m_creatures[i];
			if (c.alive() && c.dormant()) {
				++n;
			}
		}
		return n;
	}

	// --- Nivel de detalle (LOD) alrededor del jugador ---

	constexpr void set_lod_params(const LodParams& p) noexcept { m_lod = p; }
	[[nodiscard]] constexpr const LodParams& lod_params() const noexcept { return m_lod; }

	/// Ajusta la banda de detalle de cada criatura según su distancia al observador
	/// (normalmente el jugador): realized cerca, abstract a media distancia y **dormant**
	/// lejos. Así el jugador percibe un mundo rico alrededor mientras el resto apenas cuesta.
	constexpr void update_lod(eng::s16 px, eng::s16 py, RoomId room) noexcept {
		for (eng::usize i = 0; i < m_creatures.size(); ++i) {
			Creature& c = m_creatures[i];
			if (!c.alive()) {
				c.set_realized(false);
				c.set_dormant(false);
				continue;
			}
			const bool same_room = c.room == room && room != no_room;
			const eng::u16 dist = same_room ? manhattan(c.x, c.y, px, py)
							: static_cast<eng::u16>(255u);
			const LodBand band = band_for(dist, same_room, m_lod);
			c.set_realized(band == LodBand::Realized);
			c.set_dormant(band == LodBand::Dormant);
		}
	}

	// --- Aforo por region (capacidad del bioma) ---

	/// Aforo de la región según su bioma.
	[[nodiscard]] constexpr eng::u8 region_capacity(RoomId r) const noexcept {
		return biome_capacity(biome(r));
	}
	/// Criaturas vivas en la región.
	[[nodiscard]] constexpr eng::u8 region_population(RoomId r) const noexcept {
		eng::u8 n = 0u;
		for (eng::usize i = 0; i < m_creatures.size(); ++i) {
			if (m_creatures[i].alive() && m_creatures[i].room == r) {
				++n;
			}
		}
		return n;
	}
	/// ¿Queda sitio en la región para otra criatura?
	[[nodiscard]] constexpr bool region_has_space(RoomId r) const noexcept {
		return region_population(r) < region_capacity(r);
	}

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
			if (find(id) == nullptr) {
				return false;
			}
			const eng::Span<const typename Ai::Action> span {
				actions.data(), actions.size()};
			if (!m_planner.driver.replan(start, goal, span)) {
				abort_plan(id);
				return false;
			}
			Plan* p = get_or_make_plan(id);
			if (p == nullptr) {
				return false;
			}
			p->runner = m_planner.driver.runner();
			return true;
		}
	}

	[[nodiscard]] constexpr bool has_plan(EntityId id) const noexcept {
		const Plan* p = find_plan(id);
		return p != nullptr && p->runner.active;
	}

	[[nodiscard]] constexpr eng::u16 current_action(EntityId id) const noexcept {
		const Plan* p = find_plan(id);
		return p != nullptr ? p->runner.current() : static_cast<eng::u16>(0xffffu);
	}

	constexpr void advance_plan(EntityId id) noexcept {
		if (Plan* p = find_plan(id); p != nullptr) {
			p->runner.advance();
		}
	}

	constexpr void abort_plan(EntityId id) noexcept {
		if (Plan* p = find_plan(id); p != nullptr) {
			p->runner.abort();
		}
	}

	[[nodiscard]] constexpr eng::u8 planning_count() const noexcept {
		eng::u8 n = 0u;
		for (eng::usize i = 0; i < m_plans.size(); ++i) {
			if (m_plans[i].runner.active) {
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
		for (eng::usize i = 0; i < m_creatures.size(); ++i) {
			Creature& c = m_creatures[i];
			if (!c.alive() || !c.realized() || !c.repro.ready() ||
			    !region_has_space(c.room) ||
			    !can_reproduce(c.age, c.health, c.needs.hunger, c.needs.fatigue, m_life)) {
				continue;
			}
			for (eng::usize r = 0; r < c.relationships.size(); ++r) {
				const Relationship& rel = c.relationships[r];
				if (rel.kind != RelationKind::Mate) {
					continue;
				}
				const eng::s16 mag = rel.affect < 0 ? static_cast<eng::s16>(-rel.affect)
								    : static_cast<eng::s16>(rel.affect);
				if (mag < m_life.repro_min_bond) {
					continue;
				}
				Creature* mate = find(rel.target);
				if (mate == nullptr || !mate->alive()) {
					continue;
				}
				// Inicia la gestación (el nacimiento ocurre al avanzar el ciclo).
				c.repro.gestation = m_life.gestation_ticks;
				c.repro.partner = mate->id;
				c.repro.cooldown = m_life.repro_cooldown;
				mate->repro.cooldown = m_life.repro_cooldown;
				rise(c.needs, Need::Hunger, m_life.parental_cost);
				rise(mate->needs, Need::Hunger, m_life.parental_cost);
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
		for (eng::usize i = 0; i < m_creatures.size(); ++i) {
			Creature& c = m_creatures[i];
			if (!c.alive()) {
				continue;
			}
			if (c.repro.cooldown > 0u) {
				--c.repro.cooldown;
			}
			if (c.repro.gestation > 0u) {
				--c.repro.gestation;
				if (c.repro.gestation == 0u) {
					Creature* mate = find(c.repro.partner);
					const Genome child_g = newborn_genome(
						c.genome, mate != nullptr ? mate->genome : c.genome, rng, m_gene);
					if (spawn_with_genome(c.species, c.faction, c.room, c.x, c.y,
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
			for (eng::usize i = 0; i < m_creatures.size(); ++i) {
				Creature& c = m_creatures[i];
				if (!c.alive() || !c.realized() || !region_has_space(c.room) ||
				    caste_of(c.genome, cp) != Caste::Queen) {
					continue;
				}
				const Caste needed = m_colony.needed_caste(m_colony_params);
				Genome g = bias_for_caste(Genome::random(rng), needed, cp);
				const EntityId child = spawn_with_genome(c.species, c.faction, c.room, c.x,
									 c.y, g);
				if (child == no_entity) {
					return no_entity;
				}
				m_colony.note_birth(needed);
				return child;
			}
			return no_entity;
		}
	}

	// --- Objetos y economia ---

	/// Ejecuta materialmente un paso del dominio sobre la criatura (inventario) y el mundo.
	constexpr ActionResult execute_action(EntityId id, SimActionKind a) noexcept {
		Creature* c = find(id);
		if (c == nullptr || !c->alive()) {
			return ActionResult::Unknown;
		}
		return execute_domain_action(c->carrying, a, &m_items, c->room, c->x, c->y);
	}

	[[nodiscard]] constexpr ItemStore& items() noexcept { return m_items; }
	[[nodiscard]] constexpr const ItemStore& items() const noexcept { return m_items; }
	[[nodiscard]] constexpr Economy& economy() noexcept { return m_economy; }
	[[nodiscard]] constexpr const Economy& economy() const noexcept { return m_economy; }

	constexpr void set_economy_params(const EconomyParams& p) noexcept {
		m_econ_params = p;
		m_economy.reset(p);
	}
	constexpr void set_learning(const LearningParams& p) noexcept { m_learning = p; }

	/// Regalo de una facción a otra: sube la reputación del receptor y la demanda del
	/// objeto. Devuelve la reputación ganada.
	constexpr eng::s16 offer_gift(FactionId from, FactionId to, ItemKind k,
				      eng::u8 amount) noexcept {
		(void)from;
		return give_gift(m_society, m_economy, to, k, amount, m_econ_params);
	}

	/// **Trueque** entre dos criaturas: intercambia objetos a precio y sube su reputación.
	/// Devuelve `true` si el trato se cerró.
	constexpr bool offer_trade(EntityId a, EntityId b, const TradeOffer& offer,
				   const TradeParams& p = TradeParams {}) noexcept {
		Creature* ca = find(a);
		Creature* cb = find(b);
		if (ca == nullptr || cb == nullptr) {
			return false;
		}
		if (!execute_trade(ca->carrying, cb->carrying, m_economy, offer, p)) {
			return false;
		}
		m_society.adjust(ca->faction, p.rep_gain);
		m_society.adjust(cb->faction, p.rep_gain);
		return true;
	}

	/// Transmite conocimiento de una criatura a otra (padre→cría, explorador→manada).
	constexpr eng::u8 transfer_knowledge(EntityId from, EntityId to) noexcept {
		Creature* a = find(from);
		Creature* b = find(to);
		if (a == nullptr || b == nullptr) {
			return 0u;
		}
		return share(a->knowledge, b->knowledge, m_learning);
	}

	// --- Percepcion y memoria ---

	constexpr void set_sense_params(const SenseParams& p) noexcept { m_sense_params = p; }
	constexpr void set_memory_params(const MemoryParams& p) noexcept { m_memory_params = p; }

	/// Percibe el entorno: rellena `out` con las observaciones del observador (según sus
	/// sentidos y su orientación). La novedad se decide con su memoria de corto y largo
	/// plazo. Devuelve cuántas observaciones produjo.
	[[nodiscard]] constexpr eng::u8 sense(EntityId observer,
					      eng::Span<const SenseTarget> targets,
					      eng::Span<Observation> out, eng::s16 face_x = 1,
					      eng::s16 face_y = 0) const noexcept {
		const Creature* c = find(observer);
		if (c == nullptr) {
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
					   m_attention_params);
		return perceive(eff, o, targets, out, known, m_sense_params);
	}

	/// Integra las observaciones en la memoria de corto plazo del observador.
	constexpr void integrate_senses(EntityId id,
					eng::Span<const Observation> observations) noexcept {
		Creature* c = find(id);
		if (c == nullptr) {
			return;
		}
		integrate_observations(c->trackers, observations, m_frame);
	}

	/// Consolida la memoria de corto plazo en largo plazo (una criatura).
	constexpr eng::u8 consolidate_memory(EntityId id) noexcept {
		Creature* c = find(id);
		if (c == nullptr) {
			return 0u;
		}
		return consolidate(c->trackers, c->knowledge, m_memory_params);
	}

	/// Tick de memoria: olvido del corto plazo y consolidación periódica de lo realizado.
	constexpr eng::u8 tick_memory() noexcept {
		eng::u8 n = 0u;
		const bool due = m_memory_params.consolidation_period == 0u ||
				 (m_frame % m_memory_params.consolidation_period) == 0u;
		for (eng::usize i = 0; i < m_creatures.size(); ++i) {
			Creature& c = m_creatures[i];
			if (!c.alive() || !c.realized()) {
				continue;
			}
			forget_working(c.trackers, m_memory_params);
			if (due) {
				n = u8_sat_add(n, consolidate(c.trackers, c.knowledge, m_memory_params));
				decay_places(c.knowledge, m_memory_params.place_decay);
			}
		}
		return n;
	}

	/// Deriva los sentidos del genoma de una criatura (al nacer o al cambiar de morfo).
	constexpr void derive_senses(EntityId id) noexcept {
		Creature* c = find(id);
		if (c != nullptr) {
			c->senses = senses_from_genome(c->genome, m_sense_genome_params);
		}
	}

	constexpr void set_attention_params(const AttentionParams& p) noexcept {
		m_attention_params = p;
	}
	constexpr void set_sense_genome_params(const SenseGenomeParams& p) noexcept {
		m_sense_genome_params = p;
	}

	// --- Mapa mental aplicado al mundo (ruta fina y flujo) ---

	constexpr void set_mental_map_params(const MentalMapParams& p) noexcept {
		m_mental_params = p;
	}
	[[nodiscard]] constexpr const MentalMapParams& mental_map_params() const noexcept {
		return m_mental_params;
	}

	/// Sesgo de una región según lo que recuerda la criatura (macro).
	[[nodiscard]] constexpr eng::s16 mental_bias(EntityId id, RoomId room) const noexcept {
		const Creature* c = find(id);
		return c != nullptr ? place_bias(c->knowledge, room, m_mental_params) : 0;
	}

	/// Construye la capa de coste del mapa mental de una criatura para `eng::util::astar`.
	/// `room_at(idx)` mapea cada celda a su región (lo aporta el juego).
	template <eng::u16 W, eng::u16 H, class RoomAt>
	constexpr void stamp_mental_overlay(EntityId id, MentalOverlay<W, H>& overlay,
					    RoomAt room_at) const noexcept {
		const Creature* c = find(id);
		if (c != nullptr) {
			overlay.stamp(c->knowledge, room_at, m_mental_params);
		}
	}

	/// Deposita el peligro recordado por una criatura en un mapa de influencia.
	template <class Map, class RoomAt>
	constexpr void stamp_mental_danger(EntityId id, Map& influence,
					   RoomAt room_at) const noexcept {
		const Creature* c = find(id);
		if (c != nullptr) {
			deposit_mental_danger(c->knowledge, influence, room_at, m_mental_params);
		}
	}

	// --- Lenguaje y gestos ---

	constexpr void set_signal_params(const SignalParams& p) noexcept { m_signal_params = p; }

	/// Emite y entrega señales entre las criaturas realizadas: cada una comunica su
	/// conducta/emoción y las que la oyen (misma región, dentro de alcance) actualizan su
	/// memoria de corto plazo y su estado afectivo. Devuelve cuántas señales se oyeron.
	constexpr eng::u8 broadcast_signals() noexcept {
		eng::u8 heard = 0u;
		for (eng::usize i = 0; i < m_creatures.size(); ++i) {
			Creature& c = m_creatures[i];
			if (!c.alive() || !c.realized()) {
				continue;
			}
			const Signal s = make_signal(c.id, c.faction, c.room, c.x, c.y, c.behavior,
						     c.mind, c.senses, m_signal_params);
			if (s.intensity < m_signal_params.intensity_min) {
				continue;
			}
			heard = u8_sat_add(heard, deliver_signal(s, i));
		}
		return heard;
	}

	/// Actúa un **ritual** (cultura): efecto emocional propio y lo expresa con una señal a
	/// los de su región. Devuelve cuántos lo percibieron.
	constexpr eng::u8 enact_ritual(EntityId id, RitualKind r) noexcept {
		Creature* c = find(id);
		if (c == nullptr) {
			return 0u;
		}
		perform_ritual(c->mind, r, m_culture_params);
		Signal s {};
		s.sender = c->id;
		s.faction = c->faction;
		s.room = c->room;
		s.x = c->x;
		s.y = c->y;
		s.kind = signal_for_ritual(r);
		s.intensity = m_pack_params.call_intensity;
		s.range = u8_sat_add(m_pack_params.call_range,
				     static_cast<eng::u8>(c->senses.hearing / 8u));
		eng::usize self = 0u;
		for (eng::usize k = 0; k < m_creatures.size(); ++k) {
			if (m_creatures[k].id == c->id) {
				self = k;
				break;
			}
		}
		return deliver_signal(s, self);
	}

	/// **Coordina las manadas**: cada líder con una presa percibida envía a sus miembros
	/// (relaciones `Pack` hacia él) a posiciones de flanqueo alrededor del objetivo. Los
	/// miembros pasan a cazar y orientan su tracker de presa al punto que les toca.
	constexpr eng::u8 coordinate_packs() noexcept {
		eng::u8 coordinated = 0u;
		for (eng::usize i = 0; i < m_creatures.size(); ++i) {
			Creature& leader = m_creatures[i];
			if (!leader.alive() || !leader.realized()) {
				continue;
			}
			const Tracker* prey = best_attention_tracker(leader.trackers, TrackerKind::Prey);
			if (prey == nullptr || prey->room != leader.room) {
				continue;
			}
			eng::u8 idx = 0u;
			for (eng::usize j = 0; j < m_creatures.size(); ++j) {
				if (i == j) {
					continue;
				}
				Creature& m = m_creatures[j];
				if (!m.alive() || !m.realized() || m.room != leader.room) {
					continue;
				}
				const Relationship* rel = find_rel(m.relationships, leader.id);
				if (rel == nullptr || rel->kind != RelationKind::Pack) {
					continue;
				}
				const PackRole role = pack_role_for(false, idx);
				const eng::Point2s goal = flank_goal(eng::Point2s {prey->x, prey->y}, role,
								     idx, m_pack_params.flank_distance);
				observe(m.trackers, TrackerKind::Prey, prey->target, prey->room, goal.x,
					goal.y, prey->confidence, m_frame);
				m.behavior = Behavior::Hunt;
				++idx;
				++coordinated;
			}
		}
		return coordinated;
	}

	constexpr void set_culture_params(const CultureParams& p) noexcept { m_culture_params = p; }
	constexpr void set_pack_params(const PackParams& p) noexcept { m_pack_params = p; }

	// --- Mapa mental y rutas macro ---

	/// Ruta por el grafo de regiones (BFS). Escribe las regiones de `from` a `to` en `out`
	/// y devuelve la longitud (0 si no hay ruta o no cabe). Es el pathfinding macro de la
	/// simulación abstracta; la ruta fina dentro de la región la hace `eng::util`/`eng::ai`.
	[[nodiscard]] constexpr eng::u8 route_room(RoomId from, RoomId to,
						   eng::Span<RoomId> out) const noexcept {
		if (from >= MaxRooms || to >= MaxRooms || out.empty()) {
			return 0u;
		}
		if (from == to) {
			out[0] = from;
			return 1u;
		}
		RoomId queue[MaxRooms];
		RoomId came[MaxRooms];
		bool seen[MaxRooms] {};
		for (eng::u8 i = 0; i < MaxRooms; ++i) {
			came[i] = no_room;
		}
		eng::u16 head = 0u;
		eng::u16 tail = 0u;
		queue[tail++] = from;
		seen[from] = true;
		while (head < tail) {
			const RoomId cur = queue[head++];
			for (eng::usize k = 0; k < m_links[cur].size(); ++k) {
				const RoomId nx = m_links[cur][k];
				if (nx < MaxRooms && !seen[nx]) {
					seen[nx] = true;
					came[nx] = cur;
					if (tail < MaxRooms) {
						queue[tail++] = nx;
					}
				}
			}
		}
		if (!seen[to]) {
			return 0u;
		}
		RoomId rev[MaxRooms];
		eng::u8 len = 0u;
		RoomId cur = to;
		while (cur != from && len < MaxRooms) {
			rev[len++] = cur;
			cur = came[cur];
			if (cur == no_room) {
				return 0u;
			}
		}
		rev[len++] = from;
		eng::u8 n = 0u;
		while (n < len && n < out.size()) {
			out[n] = rev[static_cast<eng::u8>(len - 1u - n)];
			++n;
		}
		return n;
	}

	/// Primer paso de la ruta macro hacia `to` (`no_room` si no hay ruta).
	[[nodiscard]] constexpr RoomId route_first_step(RoomId from, RoomId to) const noexcept {
		RoomId path[MaxRooms];
		const eng::u8 n = route_room(from, to, eng::Span<RoomId> {path, MaxRooms});
		return n >= 2u ? path[1] : no_room;
	}

	/// Refugio preferido: una región recordada como refugio (mapa mental) o la guarida.
	[[nodiscard]] constexpr RoomId preferred_refuge(const Creature& c) const noexcept {
		const RoomId known = best_known_room(c.knowledge, KnowledgeKind::Shelter);
		return known != no_room ? known : c.den_room;
	}

	/// Difunde conocimiento: cada criatura realizada aporta lo suyo a la memoria de su
	/// facción y comparte con los correligionarios de su misma región; después la memoria
	/// colectiva se traduce en reputación y demanda (rumores). Devuelve cuántos eventos
	/// de difusión ocurrieron.
	constexpr eng::u8 diffuse_knowledge() noexcept {
		if constexpr (!Traits::knowledge) {
			return 0u;
		} else {
			eng::u8 events = 0u;
			for (eng::usize i = 0; i < m_creatures.size(); ++i) {
				Creature& c = m_creatures[i];
				if (!c.alive() || !c.realized()) {
					continue;
				}
				events = u8_sat_add(
					events, contribute(m_group_memory, c.faction, c.knowledge,
							   m_rumor_params, m_learning));
				for (eng::usize j = 0; j < m_creatures.size(); ++j) {
					Creature& d = m_creatures[j];
					if (j == i || !d.alive() || !d.realized() ||
					    d.faction != c.faction || d.room != c.room) {
						continue;
					}
					events = u8_sat_add(events,
							    share(c.knowledge, d.knowledge, m_learning));
				}
			}
			for (eng::u8 f = 0; f < kMaxFactions; ++f) {
				(void)apply_group_knowledge(m_society, m_economy, f, m_group_memory,
							    m_rumor_params);
			}
			return events;
		}
	}

	// --- Ticks ---

	/// Un frame del plano realizado: necesidades -> olvido de trackers -> mente ->
	/// decisión por utilidad. La integración física y el pathfinding los hace el juego.
	template <class Rng>
	constexpr void tick_realized(Rng& rng) noexcept {
		++m_frame;
		for (eng::usize i = 0; i < m_creatures.size(); ++i) {
			Creature& c = m_creatures[i];
			if (!c.alive() || !c.realized()) {
				continue;
			}
			if (c.behavior == Behavior::Sleep) {
				c.flags = static_cast<eng::u8>(c.flags | flags::asleep);
			} else {
				c.flags = static_cast<eng::u8>(c.flags & static_cast<eng::u8>(~flags::asleep));
			}
			tick(c.needs, m_rates, (c.flags & flags::asleep) != 0u);
			apply_environment(c);
			decay(c.trackers, 1u);
			if constexpr (Traits::knowledge) {
				decay_knowledge(c.knowledge, 1u);
			} else {
				(void)c;
			}
			if constexpr (Traits::emotions) {
				c.mind.update(c.needs, c.personality);
			}
			BehaviorContext ctx {};
			ctx.environment_severe = environment_severe();
			ctx.has_den = c.den_room != no_room;
			ctx.self_power = creature_power(c.personality, c.health);
			if constexpr (Traits::knowledge) {
				ctx.knowledge_confidence = top_confidence(c.knowledge);
			}
			(void)choose_behavior<Traits>(c, ctx, rng);
			if constexpr (Traits::knowledge) {
				if (c.behavior == Behavior::Tend) {
					tend_knowledge(c);
				}
			}
			update_den_flag(c);
			check_death(c);
			advance_life(c);
		}
	}

	/// Tick abstracto escalonado: en cada llamada se procesa 1/`stagger_period` de las
	/// criaturas no realizadas (las que caen en `m_cursor`). Avanza el cursor.
	template <class Rng>
	constexpr void tick_abstract(Rng& rng) noexcept {
		if constexpr (!Traits::abstract_tick) {
			(void)rng;
			return;
		}
		constexpr eng::u8 period = Traits::stagger_period == 0u ? 1u : Traits::stagger_period;
		for (eng::usize i = 0; i < m_creatures.size(); ++i) {
			Creature& c = m_creatures[i];
			if (!c.alive() || c.realized() || c.dormant()) {
				continue;
			}
			if ((c.id % period) != m_cursor) {
				continue;
			}
			tick(c.needs, m_rates, false);
			apply_environment(c);
			decay(c.trackers, 2u);
			if constexpr (Traits::knowledge) {
				decay_knowledge(c.knowledge, 2u);
			}
			if constexpr (Traits::emotions) {
				c.mind.update(c.needs, c.personality);
			}
			if (environment_severe() && c.room != preferred_refuge(c)) {
				move_toward_refuge(c);
			}
			update_den_flag(c);
			check_death(c);
			advance_life(c);
		}
		m_cursor = static_cast<eng::u8>((m_cursor + 1u) % period);
		++m_frame;
	}

private:
	[[nodiscard]] constexpr Plan* find_plan(EntityId id) noexcept {
		for (eng::usize i = 0; i < m_plans.size(); ++i) {
			if (m_plans[i].id == id) {
				return &m_plans[i];
			}
		}
		return nullptr;
	}
	[[nodiscard]] constexpr const Plan* find_plan(EntityId id) const noexcept {
		for (eng::usize i = 0; i < m_plans.size(); ++i) {
			if (m_plans[i].id == id) {
				return &m_plans[i];
			}
		}
		return nullptr;
	}
	[[nodiscard]] constexpr Plan* get_or_make_plan(EntityId id) noexcept {
		if (Plan* p = find_plan(id); p != nullptr) {
			return p;
		}
		if (m_plans.full()) {
			return nullptr;
		}
		Plan fresh {};
		fresh.id = id;
		(void)m_plans.push_back(fresh);
		return &m_plans[m_plans.size() - 1u];
	}

	/// Avanza la edad y comprueba muerte natural (solo si la genética/ciclo está activo).
	constexpr void advance_life(Creature& c) noexcept {
		if constexpr (Traits::genetics) {
			if (m_life.maturation_period != 0u &&
			    (m_frame % m_life.maturation_period) == 0u) {
				grow(c.age);
			}
			if (died_of_old_age(c.age, m_life)) {
				c.flags = static_cast<eng::u8>(c.flags &
							       static_cast<eng::u8>(~flags::alive));
				c.set_realized(false);
			}
		}
	}

	/// Transmite conocimiento del adulto a la cría que percibe (`Tend`).
	constexpr void tend_knowledge(Creature& c) noexcept {
		const Tracker* kin = best_tracker(c.trackers, TrackerKind::Kin);
		if (kin == nullptr) {
			return;
		}
		Creature* child = find(kin->target);
		if (child == nullptr) {
			return;
		}
		(void)transfer_knowledge(c.id, child->id);
	}

	/// Inicializa una criatura recién creada/reciclada.
	constexpr void init_creature(Creature& c, EntityId id, SpeciesId species, FactionId faction,
				     RoomId room, eng::s16 x, eng::s16 y) noexcept {
		c = Creature {};
		c.id = id;
		c.species = species;
		c.faction = faction;
		c.room = room;
		c.x = x;
		c.y = y;
	}

	/// Entrega una señal a las criaturas realizadas de su región dentro de alcance (salvo
	/// el emisor). Registra el tracker y aplica el efecto emocional.
	constexpr eng::u8 deliver_signal(const Signal& s, eng::usize emitter) noexcept {
		eng::u8 heard = 0u;
		for (eng::usize j = 0; j < m_creatures.size(); ++j) {
			if (j == emitter) {
				continue;
			}
			Creature& d = m_creatures[j];
			if (!d.alive() || !d.realized() || d.room != s.room || s.room == no_room) {
				continue;
			}
			const eng::u16 dist = manhattan(d.x, d.y, s.x, s.y);
			if (s.range == 0u || dist > s.range) {
				continue;
			}
			const eng::u8 strength = u8_scale(s.intensity, attenuation(dist, s.range));
			if (strength == 0u) {
				continue;
			}
			observe(d.trackers, tracker_for_signal(s.kind), s.sender, s.room, s.x, s.y,
				strength, m_frame);
			apply_signal_effect(d.mind, s.kind, m_signal_params);
			++heard;
		}
		return heard;
	}

	[[nodiscard]] constexpr bool push_link(RoomId a, RoomId b) noexcept {
		if (m_links[a].full()) {
			return false;
		}
		for (eng::usize i = 0; i < m_links[a].size(); ++i) {
			if (m_links[a][i] == b) {
				return true; // ya existe
			}
		}
		return m_links[a].push_back(b);
	}

	/// Migración off-screen: un paso por el grafo hacia el **refugio preferido** (una
	/// región recordada como refugio o, si no, la guarida). Usa el mapa mental.
	constexpr void move_toward_refuge(Creature& c) noexcept {
		const RoomId target = preferred_refuge(c);
		if (target == no_room || c.room == no_room || c.room == target) {
			return;
		}
		if (rooms_adjacent(c.room, target)) {
			c.room = target;
			return;
		}
		const RoomId step = route_first_step(c.room, target);
		if (step != no_room) {
			c.room = step;
		}
	}

	/// Aplica el peligro ambiental de la región: si hay, fija la exposición mitigada por el
	/// abrigo del terreno (y por estar a cubierto); si no, deja que ceda por el tick.
	constexpr void apply_environment(Creature& c) noexcept {
		const RegionHazard h = m_climate.at(c.room);
		if (h.severity == 0u) {
			clear_hazard(c.needs);
			return;
		}
		const eng::u8 eff = exposure_at(h, region(c.room), c.at_home());
		if (eff == 0u) {
			clear_hazard(c.needs);
		} else {
			set_exposure(c.needs, h.kind, eff);
		}
	}

	constexpr void update_den_flag(Creature& c) noexcept {		if (c.den_room != no_room && c.room == c.den_room) {
			c.flags = static_cast<eng::u8>(c.flags | flags::in_den);
		} else {
			c.flags = static_cast<eng::u8>(c.flags & static_cast<eng::u8>(~flags::in_den));
		}
	}

	constexpr void check_death(Creature& c) noexcept {
		if (c.needs.hunger >= 255u || c.health == 0u) {
			c.flags = static_cast<eng::u8>(c.flags & static_cast<eng::u8>(~flags::alive));
			c.set_realized(false);
		}
	}

	eng::util::StaticVector<Creature, MaxCreatures> m_creatures {};
	eng::u16 m_next_id = 1u;
	eng::util::StaticVector<RoomId, kMaxRoomLinks> m_links[MaxRooms] {};
	eng::util::StaticVector<Plan, MaxPlans> m_plans {};
	ItemStore m_items {};
	Economy m_economy {};
	EconomyParams m_econ_params {};
	LearningParams m_learning {};
	LifecycleParams m_life {};
	GeneticsParams m_gene {};
	Colony m_colony {};
	ColonyParams m_colony_params {};
	Society m_society {};
	NeedRates m_rates {};
	eng::u16 m_frame = 0;
	eng::u8 m_cursor = 0;
	Climate<MaxRooms> m_climate {};
	RegionTerrain m_regions[MaxRooms] {};
	GroupMemory<kMaxFactions> m_group_memory {};
	RumorParams m_rumor_params {};
	SenseParams m_sense_params {};
	MemoryParams m_memory_params {};
	AttentionParams m_attention_params {};
	SenseGenomeParams m_sense_genome_params {};
	MentalMapParams m_mental_params {};
	TerrainEventParams m_terrain_events {};
	BiomeKind m_biome[MaxRooms] {};
	SignalParams m_signal_params {};
	CultureParams m_culture_params {};
	PackParams m_pack_params {};
	LodParams m_lod {};
	[[no_unique_address]] detail::PlannerHolder<Traits::planning, PlannerNodes,
						     kMaxPlanSteps> m_planner {};
};

} // namespace eng::sim
