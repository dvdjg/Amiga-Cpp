#pragma once

/// \file sprite_director.hpp
/// **Fachada pública única de sprites** (`eng::SpriteDirector`): el juego declara
/// `VisualId`/`CompositeId`, posición e intención (`SpriteHint`), y el director compone
/// sprites HW + BOB + paleta por banda con un solo presupuesto/reporte.
///
/// Es la capa de alto nivel del sistema; `SpriteScene` y `CompositeScene` quedan como
/// implementación/“modo avanzado”. El director:
///
///   - resuelve contenido por `SceneAssets` (handles generacionales, sin `const Visual*`
///     en la API);
///   - compone primero los compuestos y después los simples con un `SpriteChannelLedger`
///     compartido (los canales ocupados por un compuesto no se ofrecen a un simple);
///   - combina placements y eventos de paleta en una sola copperlist, con `DmaChain`/
///     `Rearm`/`PerLine`/`Horizontal` elegidos por `SpriteHint::reuse`;
///   - añade `BandPalette` por frame en `top + scroll_y` a un `copper::Plan`;
///   - reporta el coste con `SpriteLoadReport` (mismo vocabulario en runtime y preflight).
///
/// No toca hardware ni registros: escribe el `FramePlan`/`Plan` del llamador y deja los
/// placements para `present`. La validación visual/demo se hace con el procedimiento
/// normal de demos; los tests host cubren la contabilidad y la composición.

#include <eng/api/objects.hpp>
#include <eng/api/sprites.hpp>
#include <eng/graphics/copper/plan.hpp>
#include <eng/graphics/sprite_line_layer.hpp>
#include <eng/graphics/sprite_manager.hpp>
#include <eng/scene/sprite_director_types.hpp>
#include <eng/scene/sprite_visual.hpp>

namespace eng::scene {

/// **Director de sprites** de capacidad fija: fachada única para contenido simple y
/// compuesto, presupuesto real y reporte. Ver el diagrama y el contrato del fichero.
///
/// El objeto es **estático/BSS**: todas las capacidades se fijan en compilación y no hay
/// heap. La huella crece con los arrays de abajo; elegir capacidades ajustadas al juego
/// (no el máximo) reduce BSS y el coste de inicialización.
///
/// ```text
///   SpriteDesc + VisualId/CompositeId
///              │
///              ▼
///   SpriteDirector ──► SceneAssets (VisualStore/CompositeStore)
///              │
///              ├─► SpriteScene<MaxSprites>       (simples, max 4 intents/actor)
///              └─► CompositeScene<MaxComposites> (compuestos, max 4 intents/actor)
///              │
///              ├─► SpriteChannelLedger compartido (compuestos reservan canal; simples evitan)
///              └─► m_placements[MaxPlacements] + m_palette_events[MaxPaletteEvents]
/// ```
///
/// \tparam MaxSprites Capacidad de la escena simple (`SpriteScene<MaxSprites>`), en
///         entidades simples vivas. Rango 1..65534; por defecto 64. No cuenta compuestos
///         ni contenido. Cada actor puede generar hasta 4 intents internos, así que el
///         coste de trabajo crece con este valor; no dimensionar a la ligera.
/// \tparam MaxVisuals Capacidad de `VisualStore` (contenido simple registrado), en
///         descriptores `Visual` vivos. Rango 1..65534; por defecto 128. Independiente de
///         `MaxSprites`: un visual puede compartirse entre varias entidades. No copia
///         píxeles, solo la vista descriptora.
/// \tparam MaxComposites Capacidad de `CompositeStore` y de `CompositeScene`, en
///         `CompositeVisual` vivos. Rango 1..65534; por defecto 16. El compuesto puede
///         contener varias partes; el límite es de compuestos, no de partes. `CompositeScene`
///         recibe hasta `MaxComposites * 4` intents internos.
/// \tparam MaxBands Capacidad del array de `BandPalette` copiadas por el director. Rango
///         1..65534; por defecto 16 (coincide con `copper::Plan::max_bands`). Las bandas
///         que excedan este valor se ignoran y el reporte no las cuenta.
/// \tparam MaxPlacements Capacidad del buffer combinado de `HwSpritePlacement` (simples +
///         compuestos), antes de `present`. Rango 1..65534; por defecto 312
///         (8 canales × 39 reusos, con margen bajo los 320 intents del `copper::Plan`).
///         Debe cubrir el peor caso de la escena: si se supera, los placements sobrantes
///         se truncan y el reporte avisa `Truncated`/`ok == false`.
/// \tparam MaxPaletteEvents Capacidad de cada buffer de `SpritePaletteEvent` (eventos de
///         actores y de bandas) que se fusionan en `present`. Rango 1..65534; por defecto
///         64 (4 eventos por banda × 16 bandas). Al llenarse, los eventos sobrantes se
///         omiten y el reporte avisa `Truncated`; no invalida los placements ya compuestos.
///
/// **Restricciones comunes:** todos los parámetros son `u16` y deben ser > 0; el total de
/// entidades (`MaxSprites + MaxComposites`) debe caber en `u16` y en `Pool` (≤ 65534), y
/// `MaxPlacements` debe ser al menos el número de placements del frame. Los `static_assert`
/// de la clase y de `eng::util::Pool` cierran los casos no representables.
///
/// **Coste:** `emit` recorre los intents y ordena placements/eventos por inserción; `present`
/// recorre los placements combinados y las capas avanzadas. No hay heap, STL, excepciones ni
/// dispatch virtual en el camino de frame.
template <eng::u16 MaxSprites = 64u, eng::u16 MaxVisuals = 128u,
	  eng::u16 MaxComposites = 16u, eng::u16 MaxBands = 16u,
	  eng::u16 MaxPlacements = 312u, eng::u16 MaxPaletteEvents = 64u>
class SpriteDirector {
	static_assert(MaxSprites > 0u && MaxVisuals > 0u && MaxComposites > 0u &&
		      MaxBands > 0u && MaxPlacements > 0u && MaxPaletteEvents > 0u);
	static constexpr eng::u16 kMaxEntities = static_cast<eng::u16>(MaxSprites + MaxComposites);
	using CompositeSceneType = eng::CompositeScene<MaxComposites,
						       static_cast<eng::u16>(MaxComposites * 4u)>;
	using SimpleSceneType = eng::SpriteScene<MaxSprites>;

	/// Entrada interna por entidad: descripción + handle en la escena simple o compuesta.
	struct Entity {
		/// Copia de la descripción de alta (hints, vistas, posición inicial).
		SpriteDesc desc {};
		/// Handle en `SpriteScene` para entidades simples; inválido para compuestas.
		eng::scene::ActorId actor {};
		/// Índice en `CompositeScene` para entidades compuestas; `kNoId` para simples.
		typename CompositeSceneType::Id composite = CompositeSceneType::kNoId;
		/// Emisor efectivo elegido al alta (Rearm/Chain/PerLine/Horizontal/Bob/Cpu).
		SpriteEmitter emitter = SpriteEmitter::Rearm;
		/// `true` si la entidad vive en `CompositeScene`.
		bool is_composite = false;
	};
	using PoolType = eng::util::Pool<Entity, kMaxEntities>;

public:
	/// Alias de `SceneAssets` con las capacidades de este director (`MaxVisuals`,
	/// `MaxComposites`). No añade almacenamiento: expone el store interno.
	using Assets = SceneAssets<MaxVisuals, MaxComposites>;

	/// Acceso al store de contenido (alta/baja de `Visual`/`CompositeVisual`).
	/// \return referencia no propietaria al store interno; vive con el director.
	[[nodiscard]] Assets& assets() noexcept { return m_assets; }
	/// Versión const del store de contenido.
	[[nodiscard]] const Assets& assets() const noexcept { return m_assets; }

	/// Registra contenido simple y devuelve su handle generacional.
	/// \param v Vista descriptora (`Visual`) que debe sobrevivir a la entidad; el store
	///          copia la vista, no los píxeles.
	/// \return `VisualId` válido, o inválido si `MaxVisuals` está lleno.
	[[nodiscard]] VisualId add_visual(const eng::graphics::Visual& v) noexcept {
		return m_assets.add_visual(v);
	}
	/// Registra contenido compuesto y devuelve su handle generacional.
	/// \param v Vista `CompositeVisual` (partes/secuencias) que debe sobrevivir; el store
	///          copia la vista.
	/// \return `CompositeId` válido, o inválido si `MaxComposites` está lleno.
	[[nodiscard]] CompositeId add_composite(
		const eng::graphics::CompositeVisual& v) noexcept {
		return m_assets.add_composite(v);
	}
	/// Baja contenido simple; las entidades que lo referencien quedan inválidas para
	/// futuras composiciones (el director no las borra).
	/// \param id handle devuelto por `add_visual`.
	/// \return `true` si el id era válido y se liberó.
	[[nodiscard]] bool remove_visual(VisualId id) noexcept {
		return m_assets.remove_visual(id);
	}
	/// Baja contenido compuesto; las entidades que lo referencien quedan inválidas.
	/// \param id handle devuelto por `add_composite`.
	/// \return `true` si el id era válido y se liberó.
	[[nodiscard]] bool remove_composite(CompositeId id) noexcept {
		return m_assets.remove_composite(id);
	}

	/// Da de alta una entidad. Devuelve un id inválido si no hay hueco, el contenido no
	/// existe o la escena interna rechaza el actor.
	/// \param desc Descripción completa (contenido, hint, posición, animación, paleta,
	///             Copper, formato BOB). Se copia en la entidad; los punteros/vistas que
	///             contiene deben seguir vivos mientras la entidad exista.
	/// \return `SpriteId` generacional, o `{}` si `content` no es válido, el pool está
	///         lleno o `SpriteScene`/`CompositeScene` rechazan el alta.
	/// \post en éxito se ocupa un slot y, para simple, un actor interno; para compuesto,
	///       una entrada en `CompositeScene`.
	[[nodiscard]] SpriteId add(const SpriteDesc& desc) noexcept {
		const SpriteVisualView view = m_assets.view(desc.content);
		if (!view.valid()) {
			return {};
		}
		const auto h = m_entities.add();
		if (!h.valid()) {
			return {};
		}
		Entity& e = *m_entities.get(h);
		e = Entity {};
		e.desc = desc;
		e.emitter = choose_sprite_emitter(desc.hint, !desc.sprite_template.empty(),
						  desc.line_layer.valid(),
						  desc.hint.strip_span > 1u);
		if (view.kind() == SpriteVisualKind::Simple) {
			eng::scene::ActorDesc a = make_actor(desc, *view.simple);
			e.actor = m_simple.add_direct(a);
			if (!e.actor.valid()) {
				(void)m_entities.remove(h);
				return {};
			}
			e.is_composite = false;
		} else {
			e.composite = m_composites.add(*view.composite, desc.x, desc.y, desc.sequence,
						       desc.facing_left);
			if (e.composite == CompositeSceneType::kNoId) {
				(void)m_entities.remove(h);
				return {};
			}
			e.is_composite = true;
		}
		return SpriteId {h.index, h.generation};
	}

	/// Baja una entidad y libera su slot; el contenido registrado (`Visual`/`Composite`)
	/// no se borra (puede estar compartido). Un `SpriteId` viejo deja de ser válido.
	/// \param id handle devuelto por `add`.
	/// \return `true` si el id era válido; `false` si ya estaba libre/invalidado.
	[[nodiscard]] bool remove(SpriteId id) noexcept {
		const auto h = to_handle(id);
		eng::Ref<Entity> e = m_entities.get(h);
		if (!e.valid()) {
			return false;
		}
		if (e->is_composite) {
			(void)m_composites.remove(e->composite);
		} else {
			(void)m_simple.remove(e->actor);
		}
		return m_entities.remove(h);
	}

	/// Vacía entidades y escenas internas; **no** borra `SceneAssets` (los ids de
	/// contenido siguen vivos hasta `remove_visual`/`remove_composite`).
	void clear() noexcept {
		m_entities.reset();
		m_simple.clear();
		m_composites.clear();
	}

	/// Número de entidades vivas (simples + compuestas); no incluye contenido huérfano.
	[[nodiscard]] eng::u16 count() const noexcept {
		return static_cast<eng::u16>(m_entities.size());
	}
	/// ¿El handle corresponde a una entidad viva? Comprueba índice y generación.
	/// \param id handle a validar.
	/// \return `true` solo si `id` no fue invalidado por `remove`/`clear`/reciclaje.
	[[nodiscard]] bool valid(SpriteId id) const noexcept {
		return m_entities.valid(to_handle(id));
	}

	/// Actor simple interno de una entidad (para moverla/animarla desde la app).
	/// \param id entidad; debe ser simple.
	/// \return vista no propietaria al `Actor`, o vacía si el id no existe o es compuesto.
	[[nodiscard]] eng::Ref<eng::scene::Actor> actor(SpriteId id) noexcept {
		eng::Ref<Entity> e = m_entities.get(to_handle(id));
		return (e.valid() && !e->is_composite) ? m_simple.store().get(e->actor) : nullptr;
	}
	/// Versión const de `actor`.
	[[nodiscard]] eng::Ref<const eng::scene::Actor> actor(SpriteId id) const noexcept {
		eng::Ref<const Entity> e = m_entities.get(to_handle(id));
		return (e.valid() && !e->is_composite) ? m_simple.store().get(e->actor) : nullptr;
	}

	/// Estado de un compuesto (posición, secuencia, frame, espejo).
	/// \param id entidad; debe ser compuesta.
	/// \return vista mutable, o vacía si el id no existe o es simple.
	[[nodiscard]] eng::Ref<eng::scene::CompositeState> composite_state(SpriteId id) noexcept {
		eng::Ref<Entity> e = m_entities.get(to_handle(id));
		return (e.valid() && e->is_composite) ? m_composites.state(e->composite) : nullptr;
	}
	/// Versión const de `composite_state`.
	[[nodiscard]] eng::Ref<const eng::scene::CompositeState> composite_state(
		SpriteId id) const noexcept {
		eng::Ref<const Entity> e = m_entities.get(to_handle(id));
		return (e.valid() && e->is_composite) ? m_composites.state(e->composite)
							 : nullptr;
	}

	/// Caja de una parte de un compuesto, en coordenadas de pantalla y con el espejo del
	/// estado vigente aplicado.
	/// \param id entidad compuesta.
	/// \param part índice en `CompositeVisual::parts`; fuera de rango devuelve `{}`.
	/// \return caja `{x,y,w,h}` o caja vacía si no aplica.
	[[nodiscard]] eng::Box part_box(SpriteId id, eng::usize part) const noexcept {
		eng::Ref<const Entity> e = m_entities.get(to_handle(id));
		return (e.valid() && e->is_composite) ? m_composites.part_box(e->composite, part)
							 : eng::Box {};
	}
	/// Hitboxes del frame vigente de un compuesto, en pantalla (espejo incluido).
	/// \param id entidad compuesta.
	/// \param out buffer de salida; se escribe hasta `out.size()` cajas.
	/// \return número de cajas escritas (0 si no hay frame, no es compuesto o no caben).
	[[nodiscard]] eng::u8 hitboxes(SpriteId id, eng::Span<eng::Box> out) const noexcept {
		eng::Ref<const Entity> e = m_entities.get(to_handle(id));
		return (e.valid() && e->is_composite)
			       ? m_composites.hitboxes(e->composite, out)
			       : 0u;
	}

	/// Avanza animaciones simples y compuestas `ticks` ticks de juego.
	/// \param ticks ticks de simulación (1 = un frame lógico); `0` no avanza nada.
	void tick(eng::u16 ticks) noexcept {
		m_composites.tick(ticks);
		m_entities.for_each([&](Entity& e, auto) {
			if (!e.is_composite) {
				auto a = m_simple.store().get(e.actor);
				if (a.valid()) {
					(void)eng::scene::actor_tick(*a, ticks);
				}
			}
		});
	}

	/// Presupuesto de la fachada (canales, scanline, BOB, Copper, paleta, gap).
	/// \param b límites del frame; se comparan en `emit` y producen avisos/`ok == false`
	///          sin abortar la composición. Se puede cambiar entre frames.
	constexpr void set_budget(const SpriteBudget& b) noexcept { m_budget = b; }
	/// Presupuesto vigente (copia).
	[[nodiscard]] constexpr const SpriteBudget& budget() const noexcept { return m_budget; }

	/// Paletas por banda (se copian; `scroll_y` se puede cambiar con `band()`).
	/// \param bands bandas a copiar; si hay más de `MaxBands`, las sobrantes se ignoran
	///              y `emit` marca `ok == false`/no las cuenta.
	void set_band_palettes(eng::Span<const BandPalette> bands) noexcept {
		m_band_count = static_cast<eng::u16>(bands.size() > MaxBands ? MaxBands : bands.size());
		for (eng::u16 i = 0u; i < m_band_count; ++i) {
			m_bands[i] = bands[i];
		}
		m_band_overflow = bands.size() > MaxBands;
	}
	/// Acceso mutable a una banda copiada (para animar `scroll_y`/`top` sin reenviar todo
	/// el span cada frame).
	/// \param index 0..`band_count()-1`.
	/// \return vista, o vacía si el índice no existe.
	[[nodiscard]] eng::Ref<BandPalette> band(eng::u16 index) noexcept {
		return index < m_band_count ? &m_bands[index] : nullptr;
	}
	/// Versión const de `band(index)`.
	[[nodiscard]] eng::Ref<const BandPalette> band(eng::u16 index) const noexcept {
		return index < m_band_count ? &m_bands[index] : nullptr;
	}
	/// Número de bandas copiadas (0..`MaxBands`); no incluye las que se ignoraron.
	[[nodiscard]] eng::u16 band_count() const noexcept { return m_band_count; }

	/// Pool Chip para pares *attached* (simple y compuesto): el compositor cocina ahí
	/// las dos estructuras DMA. Debe cubrir todos los pares vivos del frame; sin pool o
	/// sin espacio, el par se degrada a BOB y el reporte lo refleja.
	/// \param pool vista Chip sobre almacenamiento alineado que sobrevive al director.
	void set_cooked_pool(eng::ChipView<eng::SpriteTag> pool) noexcept {
		m_simple.set_cooked_pool(pool);
		m_composites.set_cooked_pool(pool);
	}

	/// Cadenas DMA de la escena simple (los canales compuestos no se encadenan).
	/// \param chains almacenamiento Chip por canal (máx. 8); se conserva la vista.
	/// \param budget presupuesto de palabras de DATA para reconstruir cadenas.
	/// \param stable_channels_mask bits de canales persistentes (1 = no reconstruir).
	void set_chains(eng::Span<eng::graphics::SpriteDmaChain> chains,
			const eng::graphics::SpriteChainBudget& budget = {},
			eng::u8 stable_channels_mask = 0u) noexcept {
		m_simple.set_chains(chains, budget, stable_channels_mask);
	}
	/// Prepara las cadenas del último `emit` (Build/Keep/Rearm por canal). Llamar tras
	/// `emit` y antes de `present`; sin cadenas configuradas es un no-op.
	[[nodiscard]] eng::graphics::SpriteChainPrep prepare_chains() noexcept {
		return m_simple.prepare_chains();
	}
	/// Cadenas configuradas (vacío = el present usa rearmes).
	[[nodiscard]] eng::Span<const eng::graphics::SpriteDmaChain> chains() const noexcept {
		return m_simple.chains();
	}

	/// Formato de los BOB derivados (layout/transparencia) para las entidades simples.
	/// \param layout `Planar` o `Interleaved`; debe coincidir con la hoja del `Visual`.
	/// \param transparency política de cookie-cut o copia opaca.
	constexpr void set_bob_format(eng::graphics::BobLayout layout,
				      TransparencyMode transparency) noexcept {
		m_bob_layout = layout;
		m_bob_transparency = transparency;
	}

	/// Política de presentación declarativa (informativa para el llamador; el commit de
	/// copperlist lo hace `present`/el driver).
	/// \param p `Auto`/`VBlankPreWrap`/`BeforeBand`/`DoubleBuffer`/`SafeWindow`.
	constexpr void set_present_policy(SpritePresent p) noexcept { m_present = p; }
	/// Política de presentación vigente.
	[[nodiscard]] constexpr SpritePresent present_policy() const noexcept { return m_present; }
	/// Deadline de Blitter para BOBs de CPU en single buffer (se comprueba en `emit`).
	/// \param d ventana de líneas y tope de palabras; `hard` es informativo para el caller.
	constexpr void set_blit_deadline(const BlitDeadline& d) noexcept { m_deadline = d; }
	/// Deadline vigente.
	[[nodiscard]] constexpr const BlitDeadline& blit_deadline() const noexcept {
		return m_deadline;
	}

	/// **Compone el frame**. Los BOB (degradados o política `Bob`) van al `plan`; los
	/// Sprite HW a `placements()`, y las paletas de banda a `copper` (si se aporta).
	/// \param plan  plan de blits del frame; recibe los BOB y sus dirty rects.
	/// \param ctx   contexto de emisión (targets, cámara, clip, buffer, display_top).
	///              Los compuestos usan `ctx.targets[0]`; sin target se omiten y el
	///              reporte marca `CompositeUnsupported`.
	/// \param copper plan de Copper opcional; si es válido recibe los `PaletteLine` de
	///               banda y las necesidades ancladas de los actores simples.
	/// \return reporte del frame; `ok == false` indica rechazo controlado/truncado, no
	///         corrupción. Los placements/paletas quedan disponibles en los getters.
	/// \post no toca hardware: escribe `plan`, `copper` y los buffers internos.
	[[nodiscard]] SpriteLoadReport emit(eng::graphics::FramePlan& plan,
					    const eng::scene::ActorEmitContext& ctx,
					    eng::Ref<eng::copper::Plan> copper = {}) noexcept {
		m_place_count = 0u;
		m_palette_count = 0u;
		m_band_events_count = 0u;
		SpriteLoadReport r {};
		r.entities = count();

		(void)plan.blit_budget_report();
		const eng::u32 words_before = plan.blit_budget().words;
		const eng::u16 jobs_before = plan.blit_budget().jobs;

		// 1) Compuestos primero: sus canales se reflejan en el ledger que verá el
		// compositor simple.
		if (m_composites.count() != 0u) {
			if (ctx.targets.empty()) {
				r.warn(SpriteWarning::CompositeUnsupported);
				r.ok = false;
			} else {
				m_composite_result = m_composites.emit(
					plan, ctx.targets[0], m_bob_layout, m_bob_transparency);
				r.bob += m_composite_result.bobs;
				r.hw += m_composite_result.sprites;
				r.degraded += m_composite_result.degraded;
				r.ok = r.ok && m_composite_result.ok;
				append_placements(m_composites.placements(), r);
			}
		}
		m_ledger.reset();
		for (eng::u16 i = 0u; i < m_place_count; ++i) {
			const eng::graphics::HwSpritePlacement& p = m_placements[i];
			if (p.channel >= eng::graphics::kSpriteChannels || p.height == 0u) {
				continue;
			}
			const eng::u16 bottom = static_cast<eng::u16>(p.vstart + p.height);
			if (!m_ledger.occupy(p.channel, p.vstart, bottom)) {
				r.warn(SpriteWarning::Channels);
				r.ok = false;
			}
		}

		// 2) Simples: el allocator evita los canales ocupados por compuestos.
		if (m_simple.count() != 0u) {
			const eng::scene::SpriteComposeResult sr =
				m_simple.emit(plan, ctx, copper, m_ledger);
			const SpriteLoadReport simple_report = report_from_compose(sr);
			r.bob += simple_report.bob;
			r.hw += simple_report.hw;
			r.degraded += simple_report.degraded;
			r.copper_intents += simple_report.copper_intents;
			r.palette_events += simple_report.palette_events;
			r.channels_used = static_cast<eng::u8>(r.channels_used |
							      simple_report.channels_used);
			if (simple_report.max_reuse > r.max_reuse) r.max_reuse = simple_report.max_reuse;
			r.ok = r.ok && simple_report.ok;
			append_placements(m_simple.placements(), r);
			append_palette(m_simple.palette_events());
		}

		// 3) Paletas por banda: playfield al Plan, sprites a la lista que fusiona el emisor.
		if (m_band_count != 0u) {
			BandPaletteReport br = emit_band_palettes(
				eng::Span<const BandPalette> {m_bands, m_band_count}, copper,
				eng::Span<eng::graphics::SpritePaletteEvent> {m_band_events,
									     MaxPaletteEvents});
			r.bands = br.bands;
			r.band_lines = br.lines;
			r.band_overlaps = br.overlaps;
			r.band_out_of_range = br.out_of_range;
			r.copper_intents = static_cast<eng::u16>(r.copper_intents +
								br.playfield_events);
			r.ok = r.ok && br.ok && !m_band_overflow;
			if (!br.ok) r.warn(SpriteWarning::BandOutOfRange);
			if (br.overlaps != 0u) r.warn(SpriteWarning::BandOverlap);
			for (eng::u16 i = 0u; i < br.sprite_events; ++i) {
				if (m_palette_count >= MaxPaletteEvents) {
					r.warn(SpriteWarning::Truncated);
					r.ok = false;
					break;
				}
				m_palette_events[m_palette_count++] = m_band_events[i];
			}
		}

		sort_placements();
		sort_palette_events();

		// 4) Contabilidad de placements/canales/scanline/gap.
		r.hw = m_place_count;
		r.parts = static_cast<eng::u16>(r.hw + r.bob);
		r.palette_events = m_palette_count;
		u16 scan[512] {};
		u16 last_bottom[eng::graphics::kSpriteChannels] {};
		bool seen_channel[eng::graphics::kSpriteChannels] {};
		u8 reuse_count[eng::graphics::kSpriteChannels] {};
		u8 channels = 0u;
		u16 attached = 0u;
		for (eng::u16 i = 0u; i < m_place_count; ++i) {
			const eng::graphics::HwSpritePlacement& p = m_placements[i];
			if (p.channel >= eng::graphics::kSpriteChannels || p.height == 0u) {
				continue;
			}
			channels = static_cast<u8>(channels | (1u << p.channel));
			++reuse_count[p.channel];
			if (p.attach) ++attached;
			if (seen_channel[p.channel] &&
			    p.vstart < static_cast<eng::u16>(
					  last_bottom[p.channel] +
					  eng::graphics::kSpriteVerticalGapLines)) {
				++r.gap_violations;
			}
			seen_channel[p.channel] = true;
			last_bottom[p.channel] = static_cast<eng::u16>(p.vstart + p.height);
			const eng::u16 to = static_cast<eng::u16>(p.vstart + p.height) < 512u
						    ? static_cast<eng::u16>(p.vstart + p.height)
						    : 512u;
			for (eng::u16 line = p.vstart; line < to; ++line) {
				++scan[line];
				if (scan[line] > r.scanline_peak) r.scanline_peak = scan[line];
			}
		}
		r.channels_used = static_cast<u8>(r.channels_used | channels);
		for (eng::u8 c = 0u; c < eng::graphics::kSpriteChannels; ++c) {
			if (reuse_count[c] > r.max_reuse) r.max_reuse = reuse_count[c];
		}
		r.attached_pairs = attached; // un placement impar con ATTACH = un par
		if (r.gap_violations != 0u) r.warn(SpriteWarning::Gap);

		// 5) Hints que no pueden materializarse: se reportan, no se silencian.
		m_entities.for_each([&](const Entity& e, auto) {
			if (e.desc.hint.gap_lines < eng::graphics::kSpriteVerticalGapLines) {
				++r.gap_violations;
				r.warn(SpriteWarning::Gap);
			}
			if (e.emitter == SpriteEmitter::PerLine) ++r.emitter_per_line;
			else if (e.emitter == SpriteEmitter::DmaChain) ++r.emitter_chain;
			else if (e.emitter == SpriteEmitter::Horizontal) ++r.emitter_horizontal;
			else ++r.emitter_rearm;
			if (e.desc.hint.reuse == SpriteReuse::PerLine &&
			    !e.desc.line_layer.valid()) {
				r.warn(SpriteWarning::PerLineMissing);
			}
			if (e.desc.hint.strip_span > 1u) {
				++r.strips;
				if (e.desc.hint.strip_id == 0u) r.warn(SpriteWarning::StripShort);
			}
		});

		// 6) Coste de Blitter del frame (delta del plan: no atribuye trabajos ajenos).
		(void)plan.blit_budget_report();
		if (plan.blit_budget().words >= words_before) {
			r.bob_words = plan.blit_budget().words - words_before;
			r.blit_words = r.bob_words;
		}
		if (plan.blit_budget().jobs >= jobs_before) {
			r.bob_jobs = static_cast<eng::u16>(plan.blit_budget().jobs - jobs_before);
		}
		r.copper_words = static_cast<eng::u16>(r.copper_intents * 4u +
						      static_cast<eng::u32>(r.palette_events) * 2u);
		if (!blit_deadline_ok(m_deadline, r.bob_words)) {
			r.warn(SpriteWarning::BlitDeadline);
			r.ok = false;
		}
		apply_budget(r, m_budget);
		m_last_report = r;
		return r;
	}

	/// Placements HW combinados del último `emit`, ordenados por `vstart`.
	/// \return vista de solo lectura; válida hasta el siguiente `emit`.
	[[nodiscard]] eng::Span<const eng::graphics::HwSpritePlacement> placements() const
		noexcept {
		return eng::Span<const eng::graphics::HwSpritePlacement> {m_placements,
									   m_place_count};
	}
	/// Eventos de paleta de sprites combinados (actores + bandas), ordenados por línea.
	/// \return vista de solo lectura; se pasa como `extra` a `SpriteManager` en `present`.
	[[nodiscard]] eng::Span<const eng::graphics::SpritePaletteEvent> palette_events() const
		noexcept {
		return eng::Span<const eng::graphics::SpritePaletteEvent> {m_palette_events,
									 m_palette_count};
	}
	/// Último reporte calculado por `emit` (copia; válido tras el primer `emit`).
	[[nodiscard]] const SpriteLoadReport& last_report() const noexcept { return m_last_report; }

	/// **Publica el frame** en la copperlist: placements combinados + paletas de banda +
	/// capas `PerLine` avanzadas. Llamar una vez tras `emit`.
	/// \tparam Sched tipo de emisor de Copper (debe ofrecer `wait_line`/`move` y los
	///         helpers que usa `SpriteManager::emit_placements_into`).
	/// \param sched         emisor de Copper (p. ej. `SchedulerT` o el de `copper::Plan`).
	/// \param arm_line      línea temprana de armado (HSTART/WAIT); debe ser menor que el
	///                      primer `vstart` visible. 32 es el valor típico bajo `DIWSTRT=0x2c`.
	/// \param clear_unused  si `true`, desactiva los canales sin placement del frame.
	/// \post escribe MOVEs/WAITs en `sched`; no toca registros hasta que el Copper ejecute.
	template <class Sched>
	void present(Sched& sched, eng::u16 arm_line = 32u, bool clear_unused = false) const
		noexcept {
		eng::graphics::SpriteManager::emit_placements_into(
			sched, placements(), arm_line, palette_events(), m_simple.chains(),
			clear_unused);
		m_entities.for_each([&](const Entity& e, auto) {
			if (e.emitter == SpriteEmitter::PerLine && e.desc.line_layer.valid()) {
				e.desc.line_layer->emit_into(sched);
			}
		});
	}

private:
	/// Convierte el id público al handle generacional del pool interno.
	/// \param id `SpriteId` público (índice + generación).
	/// \return handle equivalente; no valida (lo hace `Pool`).
	[[nodiscard]] static constexpr typename PoolType::Handle to_handle(SpriteId id) noexcept {
		return {id.index, id.generation};
	}

	/// Traduce `SpriteDesc` + `Visual` al `ActorDesc` interno de `SpriteScene`.
	/// \param d descripción pública; sus hints/posición se copian.
	/// \param v vista simple ya resuelta; debe seguir viva mientras el actor exista.
	/// \return actor listo para `add_direct`, con `x` convertida a HSTART si es sprite y
	///         `force_bob` derivado de política/reuse/attach.
	[[nodiscard]] static eng::scene::ActorDesc make_actor(
		const SpriteDesc& d, const eng::graphics::Visual& v) noexcept {
		eng::scene::ActorDesc a {};
		a.visual = v;
		a.sprite_template = d.sprite_template;
		a.sprite_palette = d.sprite_palette;
		a.animation = d.animation;
		a.y = d.y;
		bool force_bob = d.hint.policy == SpritePolicy::Bob ||
				 d.hint.policy == SpritePolicy::Cpu ||
				 d.hint.policy == SpritePolicy::Layer ||
				 d.hint.reuse == SpriteReuse::None;
		if (d.hint.attach == SpriteAttach::Off &&
		    eng::graphics::visual_is_attached_pair(v)) {
			force_bob = true; // 4 planos no caben en un canal
		}
		// `SpriteDesc::x` es X de pantalla; el registro de sprite usa HSTART absoluto.
		// Un BOB no lleva ese origen (el Blitter escribe píxeles de pantalla).
		a.x = force_bob ? d.x
				: static_cast<eng::s16>(d.x + eng::graphics::kSpriteScreenXOrigin);
		a.surface = d.surface;
		a.z = d.z;
		a.anchor = d.anchor;
		a.offset = d.offset;
		a.anim_rate_num = d.anim_rate_num;
		a.anim_rate_den = d.anim_rate_den;
		a.transparency = d.transparency;
		a.background = d.background;
		a.layout = d.layout;
		a.sheet_row_bytes = d.sheet_row_bytes;
		a.copper = d.copper;
		a.assign_rank = d.hint.rank;
		a.preferred_channel =
			d.hint.channel < eng::graphics::kSpriteChannels ? d.hint.channel : 0u;
		a.sprite_priority = d.hint.sprite_priority;
		a.group = d.hint.group;
		a.group_index = d.hint.group_index;
		a.group_span = d.hint.group_span;
		a.strip_id = d.hint.strip_id;
		a.strip_index = d.hint.strip_index;
		a.strip_span = d.hint.strip_span;
		a.force_bob = force_bob;
		return a;
	}

	/// Copia placements al buffer combinado; si no caben, marca el reporte y trunca.
	/// \param ps placements de una escena (simple o compuesta).
	/// \param r reporte del frame que se actualiza con `Truncated`/`ok`.
	void append_placements(eng::Span<const eng::graphics::HwSpritePlacement> ps,
			       SpriteLoadReport& r) noexcept {
		for (eng::usize i = 0u; i < ps.size(); ++i) {
			if (m_place_count >= MaxPlacements) {
				r.warn(SpriteWarning::Truncated);
				r.ok = false;
				break;
			}
			m_placements[m_place_count++] = ps[i];
		}
	}
	/// Copia eventos de paleta al buffer combinado; los sobrantes se omiten sin abortar.
	/// \param ps eventos de una escena; la vista se copia (contiene su propio span).
	void append_palette(eng::Span<const eng::graphics::SpritePaletteEvent> ps) noexcept {
		for (eng::usize i = 0u; i < ps.size(); ++i) {
			if (m_palette_count >= MaxPaletteEvents) {
				return;
			}
			m_palette_events[m_palette_count++] = ps[i];
		}
	}
	/// Ordena los placements por `vstart` (estable para pares attached/orden de emisión).
	void sort_placements() noexcept {
		for (eng::u16 i = 1u; i < m_place_count; ++i) {
			const eng::graphics::HwSpritePlacement cur = m_placements[i];
			eng::u16 j = i;
			while (j > 0u && m_placements[j - 1u].vstart > cur.vstart) {
				m_placements[j] = m_placements[j - 1u];
				--j;
			}
			m_placements[j] = cur;
		}
	}
	void sort_palette_events() noexcept {
		for (eng::u16 i = 1u; i < m_palette_count; ++i) {
			const eng::graphics::SpritePaletteEvent cur = m_palette_events[i];
			eng::u16 j = i;
			while (j > 0u && m_palette_events[j - 1u].line > cur.line) {
				m_palette_events[j] = m_palette_events[j - 1u];
				--j;
			}
			m_palette_events[j] = cur;
		}
	}

	Assets m_assets {};                  ///< contenido simple/compuesto con handles generacionales
	SimpleSceneType m_simple {};         ///< escena interna de entidades simples
	CompositeSceneType m_composites {};  ///< escena interna de entidades compuestas
	PoolType m_entities {};              ///< handles `SpriteId` (índice+generación de entidad)
	eng::graphics::SpriteChannelLedger m_ledger {}; ///< canales ocupados por compuestos en el frame
	SpriteBudget m_budget {};            ///< límites usados por `apply_budget` en `emit`
	SpritePresent m_present = SpritePresent::Auto; ///< política declarativa de presentación
	BlitDeadline m_deadline {};          ///< límite de Blitter comprobado en `emit`
	eng::graphics::BobLayout m_bob_layout = eng::graphics::BobLayout::Planar; ///< layout BOB por defecto
	TransparencyMode m_bob_transparency = TransparencyMode::Mask1Bit; ///< transparencia BOB por defecto
	BandPalette m_bands[MaxBands] {};    ///< copias de bandas (0..`MaxBands-1`)
	eng::u16 m_band_count = 0u;          ///< bandas válidas copiadas (0..`MaxBands`)
	bool m_band_overflow = false;        ///< `true` si `set_band_palettes` recibió más de `MaxBands`
	eng::graphics::HwSpritePlacement m_placements[MaxPlacements] {}; ///< placements combinados del frame
	eng::u16 m_place_count = 0u;         ///< placements válidos en `m_placements` (0..`MaxPlacements`)
	eng::graphics::SpritePaletteEvent m_palette_events[MaxPaletteEvents] {}; ///< eventos combinados
	eng::u16 m_palette_count = 0u;       ///< eventos válidos en `m_palette_events`
	eng::graphics::SpritePaletteEvent m_band_events[MaxPaletteEvents] {}; ///< staging de bandas
	eng::u16 m_band_events_count = 0u;   ///< eventos de banda escritos por `emit_band_palettes`
	typename CompositeSceneType::EmitResult m_composite_result {}; ///< resultado del compositor compuesto
	SpriteLoadReport m_last_report {};   ///< copia del último reporte devuelto por `emit`
};

} // namespace eng::scene

namespace eng {
using scene::AnimationView;
using scene::BandPalette;
using scene::CompositeId;
using scene::CopperCue;
using scene::SceneAssets;
using scene::SpriteBudget;
using scene::SpriteDesc;
using scene::SpriteDirector;
using scene::SpriteHint;
using scene::SpriteId;
using scene::SpriteLoadReport;
using scene::SpriteVisual;
using scene::SpriteVisualRef;
using scene::VisualId;
} // namespace eng
