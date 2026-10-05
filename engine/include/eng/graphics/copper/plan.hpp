#pragma once

/// \file plan.hpp
/// **Plan de Copper de una escena**: recolecta las intenciones de las capas/efectos
/// ("tracks"), las **ordena por scanline** y las materializa en la copperlist del buffer
/// trasero de un `copper::DoubleBuffer`, que se publica con el swap de `COP1LC`.
///
/// Es el eslabón que faltaba entre el vocabulario portable (`graphics::CopperIntent`) y los
/// emisores (`copper::Scheduler`/`ListBuilder`): hoy cada demo/efecto emite su copper a mano
/// y debe acordarse de mantener las intenciones en orden ascendente de línea, elegir bloque
/// y no pisar la lista activa. Con el plan, eso pasa a ser responsabilidad del engine.
///
/// Uso por frame (el plan es el único dueño de la lista activa):
///
///   plan.begin_frame();                       // limpia intenciones y sitúa el emisor detrás
///   plan.scheduler().emit_planes_display(...);  // parte estática (display, módulos)
///   plan.scheduler().emit_palette(base);
///   plan.add(intents, n);                     // aportaciones de los tracks (sin ordenar)
///   plan.materialize();                       // ORDENA por scanline y las emite AQUÍ
///   plan.scheduler().wait_line(0xf8);         // cola de la lista
///   plan.scheduler().move(copper::Register::COLOR00, 0);
///   if (!plan.end_frame()) { /* overflow o lista no cupo */ }
///   // tras VBlank:
///   plan.commit(backend);                     // instala la lista (swap de COP1LC)
///
/// ```text
///   capas / efectos                     Plan (no posee la lista)               Copper
///   ───────────────                     ───────────────────────                ──────
///   graphics::CopperIntent ──add()──► [ intents (máx. 320, SIN ordenar) ]
///                                           │
///                                     materialize() ── ordena por línea RELATIVA (first_line)
///                                           │            y emite con el Scheduler
///                                           ▼
///   Scheduler / ListBuilder ──────────► buffer TRASERO del DoubleBuffer ──commit()──► COP1LC
///   (el CPU escribe mientras el           (el CPU rellena el trasero;        swap en VBlank
///    Copper ejecuta el frontal)            el Copper ejecuta el frontal)      = se publica
/// ```
///
/// Ver `docs/engine/architecture/DISPLAY_COMPOSITION.md` §5.

#include <eng/core/types/domains.hpp>
#include <eng/core/types/ptr.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/algorithm.hpp>
#include <eng/core/util/array.hpp>
#include <eng/core/util/noncopyable.hpp>
#include <eng/graphics/copper/double_buffer.hpp>
#include <eng/debug/prof.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/raster_intent.hpp>
#include <eng/memory/arena.hpp>
#include <eng/memory/memory_manager.hpp>
#include <eng/res/asset_cache.hpp>

namespace eng::copper {

struct PlanConfig {
	u32 copper_bytes = 8192u; ///< tamaño de CADA bloque del doble buffer
	/// Línea de raster donde arranca la ventana visible (p. ej. 0x2c en PAL 256). El
	/// Copper ejecuta la lista en el orden en que el raster alcanza las líneas, y el
	/// display **envuelve** a las 256 líneas: las intenciones se ordenan por línea
	/// RELATIVA a `first_line` ((top - first_line) & 0xff), no por `top` absoluto, para
	/// que una zona de la parte baja (top < first_line) vaya DESPUÉS de una de la parte
	/// alta y no antes.
	u16 first_line = 0u;
};

/// Tramo de raster reclamado por un efecto, para **detectar solapes** entre efectos que
/// escriben los mismos registros en las mismas líneas. `register_mask` = 0 significa
/// "cualquier registro" (conflicto con cualquier otro en el tramo). Ver
/// `docs/engine/architecture/EFFECT_MODEL.md` §4.
struct BandScope {
	u16 first_line = 0;
	u16 last_line = 0;     ///< inclusivo
	u16 register_mask = 0; ///< bits = registros reclamados (0 = cualquiera)
};

/// Coste **declarado** de un efecto (huella estimada), para que el plan sume y avise de
/// quién agota el presupuesto. Ver `docs/engine/architecture/EFFECT_MODEL.md` §5.
struct EffectCost {
	u16 intents = 0; ///< nº de intenciones que aportará
	u16 words = 0;   ///< palabras de Copper estimadas
};

/// Índice "ningún efecto" de `over_budget_effect()`.
inline constexpr u8 no_effect = 0xffu;

class Plan : public eng::util::Noncopyable {
public:
	Plan() = default;
	~Plan() { release(); }

	/// Capacidad de intenciones por frame (fijo, sin heap). `add` marca overflow si se
	/// supera; `end_frame` devuelve false en ese caso (no se publica una lista parcial).
	/// Con 320 caben los gradientes **por línea** de una escena (256 líneas + objetos).
	static constexpr u16 max_intents = 320;
	/// Capacidad de reservas de banda por frame (fijo, sin heap).
	static constexpr u8 max_bands = 16;
	static constexpr u8 max_dma_assets = 8; ///< owners Chip retenibles durante la vida de la copperlist

	/// Reserva el doble buffer de copperlist (Chip) y prepara el plan.
	/// \param memory  gestor de memoria.
	/// \param cfg     configuración (`copper_bytes`, `first_line`, …).
	/// \return `false` si no cabe (sin dejar recursos).
	bool begin(eng::MemoryManager& memory, const PlanConfig& cfg = {}) {
		release();
		m_cfg = cfg;
		m_ok = m_owned.begin(memory, cfg.copper_bytes);
		if (!m_ok) return false;
		m_copper = m_owned;
		return true;
	}

	/// Enlaza el plan a un `DoubleBuffer` **externo** (el llamador decide dónde vive la
	/// memoria: p. ej. el que ya posee un driver o un compositor). Así el plan no impone
	/// ser dueño de la buffering de copperlist; solo la orquesta (orden + presupuesto +
	/// publicación).
	void attach(DoubleBuffer& copper) {
		release();
		if (!copper.ok()) return;
		m_copper = copper;
		m_count = 0u;
		m_slot_count = 0u;
		m_band_count = 0u;
		m_cost_words = 0u;
		m_cost_count = 0u;
		m_over_effect = no_effect;
		m_overflow = false;
		m_words = 0u;
		m_report = {};
		m_ok = true;
	}

	/// Retiene assets DMA leídos por la lista durante la vida del plan; adquisición de setup, no por frame.
	[[nodiscard]] bool retain_dma_asset(eng::res::AssetDmaLease&& lease) noexcept {
		if (!m_ok || !lease.valid() || m_dma_asset_count >= max_dma_assets) return false;
		m_dma_assets[m_dma_asset_count++] = static_cast<eng::res::AssetDmaLease&&>(lease);
		return true;
	}
	[[nodiscard]] constexpr u8 dma_asset_count() const noexcept { return m_dma_asset_count; }

	/// Libera únicamente el `DoubleBuffer` propio. Un buffer enlazado con `attach()` pertenece
	/// al llamador y no se toca. Debe ejecutarse cuando el Copper ya no pueda leer la lista.
	void release() noexcept {
		m_copper.reset();
		m_owned.release();
		m_sched = {};
		m_ok = false;
		m_words = 0u;
		m_count = 0u;
		m_slot_count = 0u;
		m_band_count = 0u;
		m_cost_words = 0u;
		m_cost_count = 0u;
		m_over_effect = no_effect;
		m_overflow = false;
		m_report = {};
		m_bands = {};
		m_costs = {};
		m_intents = {};
		m_prio = {};
		m_perm = {};
		m_slot_word = {};
		m_slot_reg = {};
		m_slot_line = {};
		m_line_start = {};
		m_count_by_line = {};
		m_line_cursor = {};
		release_dma_assets();
	}

	/// Libera los assets tras retirar/publicar una lista que ya no pueda consumirlos; llamarlo antes del teardown del cache.
	void release_dma_assets() noexcept {
		for (u8 i = 0u; i < m_dma_asset_count; ++i) m_dma_assets[i].reset();
		m_dma_asset_count = 0u;
	}

	/// Abre el frame: limpia las intenciones y sitúa el emisor en el bloque **trasero**.
	void begin_frame() {
		m_count = 0;
		m_band_count = 0;
		m_cost_words = 0;
		m_cost_count = 0;
		m_over_effect = no_effect;
		m_overflow = false;
		m_slot_count = 0;
		m_sched.retarget(m_copper->inactive_block()); // sin copiar la Timeline (512+ B)
	}

	/// Emisor del frame (bloque trasero). El llamador emite aquí la parte estática.
	Scheduler& scheduler() { return m_sched; }
	const Scheduler& scheduler() const { return m_sched; }

	void add(const graphics::CopperIntent& it) { add_prioritized(&it, 1u, 0u, 0u); }

	void add(const graphics::CopperIntent* intents, u16 count) {
		add_prioritized(intents, count, 0u, 0u);
	}

	/// Ancla `count` intenciones (líneas **relativas**) a `base_line` absoluto y las añade con
	/// su prioridad `(surface, z)`. Única verdad del anclaje: lo usan el camino de actor
	/// (`scene::actor_add_copper`) y el de intención (`scene::SpritePlanExecutor`).
	/// \param intents    intenciones con líneas **relativas**.
	/// \param count      nº de intenciones.
	/// \param base_line  línea absoluta a la que se anclan.
	/// \param surface    superficie (`(surface, z)` da la prioridad de fusión).
	/// \param z          orden de superposición.
	void add_anchored(const graphics::CopperIntent* intents, eng::usize count, s32 base_line,
			  u8 surface, u8 z) {
		for (eng::usize i = 0u; i < count; ++i) {
			graphics::CopperIntent abs = intents[i];
			abs.top = static_cast<u16>(base_line + static_cast<s32>(intents[i].top));
			abs.bottom = static_cast<u16>(base_line + static_cast<s32>(intents[i].bottom));
			add_prioritized(&abs, 1u, surface, z);
		}
	}

	/// Reserva el tramo de raster `[first, last]` para los registros de `register_mask`
	/// (0 = cualquiera). Devuelve `false` si **solapa** con otra reserva (misma línea y
	/// registros) o si no caben más; así el conflicto entre efectos se detecta en vez de
	/// resolverse en silencio. Las reservas se limpian en `begin_frame()`.
	/// \param first,last    tramo de líneas raster (se ordena internamente).
	/// \param register_mask registros reclamados (`0` = cualquiera).
	/// \return `false` si solapa con otra reserva o no caben más.
	[[nodiscard]] bool reserve_band(u16 first, u16 last, u16 register_mask = 0u) {
		if (first > last) {
			const u16 t = first;
			first = last;
			last = t;
		}
		for (u8 i = 0; i < m_band_count; ++i) {
			const BandScope& b = m_bands[i];
			const bool lines = !(last < b.first_line || first > b.last_line);
			const bool regs = (register_mask == 0u) || (b.register_mask == 0u) ||
					  ((register_mask & b.register_mask) != 0u);
			if (lines && regs) {
				return false;
			}
		}
		if (m_band_count >= max_bands) {
			return false;
		}
		m_bands[m_band_count++] = BandScope {first, last, register_mask};
		return true;
	}
	/// Nº de reservas de banda del frame.
	[[nodiscard]] constexpr u8 band_count() const { return m_band_count; }

	/// Registra el **coste declarado** de un efecto y lo suma al del frame. Devuelve
	/// `false` si con este efecto el total supera la capacidad del bloque; el índice del
	/// culpable queda en `over_budget_effect()`. Se limpia en `begin_frame()`.
	/// \param c  coste declarado del efecto (`{intents, words}`).
	/// \return `false` si el total supera la capacidad del bloque.
	[[nodiscard]] bool note_effect_cost(EffectCost c) {
		if (m_cost_count >= max_bands) {
			return false;
		}
		m_costs[m_cost_count] = c;
		m_cost_words = static_cast<u16>(m_cost_words + c.words);
		const bool fits = m_cost_words <= words_capacity();
		if (!fits) {
			m_over_effect = m_cost_count;
		}
		++m_cost_count;
		return fits;
	}
	/// Índice del primer efecto que agotó el presupuesto, o `no_effect`.
	[[nodiscard]] constexpr u8 over_budget_effect() const { return m_over_effect; }
	/// Palabras declaradas acumuladas este frame.
	[[nodiscard]] constexpr u16 cost_words() const { return m_cost_words; }
	/// Capacidad del bloque de copperlist en palabras (`copper_bytes / 2`).
	[[nodiscard]] constexpr u16 words_capacity() const {
		return static_cast<u16>(m_cfg.copper_bytes / 2u);
	}

	/// Igual que `add`, pero anotando de quién viene cada intención: `surface` (índice de
	/// la superficie de la composición) y `z` (orden dentro de ella). En conflicto — dos
	/// intenciones que escriben el mismo registro en la MISMA línea — gana la de mayor
	/// `(surface, z)`: se ordena para emitirse la última, y en el Copper la última
	/// escritura a un registro manda. Ver `docs/engine/architecture/OBJECT_SYSTEM.md` §7.
	///
	/// Alcance: cada intención se materializa como un punto en `top` (el scheduler no usa
	/// `bottom`), así que esto cubre todos los conflictos que existen hoy.
	void add_prioritized(const graphics::CopperIntent* intents, u16 count, u8 surface, u8 z) {
		const u16 prio = static_cast<u16>((static_cast<u16>(surface) << 8u) | z);
		for (u16 i = 0; i < count; ++i) {
			if (m_count < max_intents) {
				m_intents[m_count] = intents[i];
				m_prio[m_count] = prio;
				++m_count;
			} else {
				m_overflow = true;
			}
		}
	}

	/// Ordena las intenciones por `top` (scanline) y las emite en la posición ACTUAL del
	/// emisor (así el llamador decide dónde van: p. ej. tras la paleta base y antes de la
	/// cola). Dentro de cada línea, ordena por prioridad (ver `add_prioritized`).
	void materialize() {
		sort_by_top();
		sort_priority_within_lines();
		// Emite por el orden indirecto; PaletteLine compatibles de una línea se procesan en
		// un lote para compartir WAIT y evitar el dispatch por intención. Otras intenciones
		// conservan el camino rápido/general, sin copiar estructuras de 40 B.
		ENG_PROF_BEGIN(eng::debug::prof_emit);
		for (u16 i = 0u; i < m_count;) {
			const u16 idx = m_perm[i];
			const graphics::CopperIntent& first = m_intents[idx];
			u16 end = static_cast<u16>(i + 1u);
			if (first.kind == graphics::CopperIntentKind::PaletteLine && first.count == 1u) {
				while (end < m_count) {
					const graphics::CopperIntent& next = m_intents[m_perm[end]];
					if (next.kind != graphics::CopperIntentKind::PaletteLine || next.count != 1u ||
					    next.top != first.top) break;
					++end;
				}
			}
			const u16 group_count = static_cast<u16>(end - i);
			if (group_count > 1u && m_sched.emit_palette_line_batch(
					m_intents.span(), m_perm.span().subspan(i, static_cast<u16>(end - i)),
					m_slot_word.span(), i)) {
				for (u16 slot = i; slot < end; ++slot) {
					const graphics::CopperIntent& it = m_intents[m_perm[slot]];
					m_slot_reg[slot] = it.first;
					m_slot_line[slot] = it.top;
				}
				i = end;
				continue;
			}
			m_sched.emit_copper_intents_fast(m_intents.span().subspan(idx, 1u));
			// El slot del dato depende del tipo y del número de words emitidos por una intención;
			// para la emisión individual sigue siendo el último word, igual que en la ruta previa.
			m_slot_word[i] = static_cast<u16>(m_sched.words_used() - 1u);
			m_slot_reg[i] = first.first;
			m_slot_line[i] = first.top;
			++i;
		}
		m_slot_count = m_count;
		ENG_PROF_END(eng::debug::prof_emit);
	}

	/// Slots de las intenciones materializadas en el último `materialize()` (misma cantidad
	/// que intenciones emitidas). Permiten que un efecto **pre-construido una vez** (setup)
	/// actualice sus colores por frame escribiendo solo la palabra de DATO, sin re-emitir.
	/// Casan por identidad `(slot_reg, slot_line)`; típicamente `PaletteLine` de 1 color.
	[[nodiscard]] constexpr u16 slot_count() const { return m_slot_count; }
	[[nodiscard]] constexpr u16 slot_word(u16 j) const { return m_slot_word[j]; }
	[[nodiscard]] constexpr u8 slot_reg(u16 j) const { return m_slot_reg[j]; }
	[[nodiscard]] constexpr u16 slot_line(u16 j) const { return m_slot_line[j]; }

	/// Cierra la lista (`end`), guarda el informe y **voltea** el buffer. Devuelve false si
	/// hubo overflow o la lista no cupo; en ese caso no debe publicarse.
	bool end_frame() {
		m_sched.end();
		m_words = m_sched.words_used();
		m_report = m_sched.report();
		m_ok = m_sched.ok();
		m_copper->flip();
		return m_ok && !m_overflow;
	}

	/// Publica el buffer delantero (swap de `COP1LC`). Llamar tras VBlank.
	/// \param backend  el backend (instala la copperlist).
	template <typename Backend>
	void commit(Backend& backend) const {
		m_copper->install(backend);
	}

	/// Voltea el bloque activo/inactivo **sin re-emitir** la lista. Lo usa un modo que
	/// preconstruye la estructura en ambos bloques y por frame solo parchea datos (copper
	/// chunky): escribir en el inactivo → `flip()` → `commit(backend)`.
	void flip() { m_copper->flip(); }

	/// Toma el control del display mostrando el buffer delantero (una vez).
	/// \param backend  el backend (congela el SO y arranca la copperlist).
	template <typename Backend>
	void takeover(Backend& backend) const {
		m_copper->takeover(backend);
	}

	constexpr u16 intent_count() const { return m_count; }
	/// ¿Hay más de una intención en alguna scanline de este frame?
	constexpr bool overflow() const { return m_overflow; }
	constexpr bool ok() const { return m_ok; }
	constexpr u16 words() const { return m_words; }
	constexpr const ScheduleReport& report() const { return m_report; }

	/// Depuración/tests: words del bloque ACTIVO (el que ejecuta el Copper).
	constexpr u16* active_words() const { return m_copper->active_words(); }
	constexpr u16* inactive_words() const { return m_copper->inactive_words(); }

private:
	/// Cuenta y ordena por línea **relativa al inicio del display** en O(n + 256): counting
	/// por 256 líneas y un array de orden (`m_perm`). No reordena los `CopperIntent`: el
	/// scheduler exige las intenciones en el orden en que el raster las alcanza, y eso se
	/// consigue emitiendo por `m_perm` (mover structs de 40 B en Chip RAM costaba ~2.700
	/// ciclos por intención).
	///
	/// Los contadores (`m_count_by_line`, `m_line_start`, `m_line_cursor`) son miembros y
	/// no locales: 3 arrays de 256 `u16` en pila costaban 1 KB de marco por frame y gcc
	/// los recargaba con `lea`/`-1024(sp)` en cada acceso (medido en la 086).
	__attribute__((always_inline)) inline void sort_by_top() {
		ENG_PROF_BEGIN(eng::debug::prof_sort_lines);
		if (m_count < 2u) {
			for (u16 i = 0; i < m_count; ++i) m_perm[i] = i;
			ENG_PROF_END(eng::debug::prof_sort_lines);
			return;
		}
		m_count_by_line.fill(u16(0u));
		for (u16 i = 0; i < m_count; ++i) ++m_count_by_line[raster_key(m_intents[i].top)];
		u16 acc = 0;
		for (u16 l = 0; l < 256u; ++l) {
			m_line_start[l] = acc; // inicio del grupo de la línea l (para prioridades)
			m_line_cursor[l] = acc;
			acc = static_cast<u16>(acc + m_count_by_line[l]);
		}
		m_line_start[256] = m_count;
		// Orden estable (FIFO dentro de la línea) por índices.
		for (u16 i = 0; i < m_count; ++i) {
			m_perm[m_line_cursor[raster_key(m_intents[i].top)]++] = i;
		}
		ENG_PROF_END(eng::debug::prof_sort_lines);
	}

	/// Dentro de cada línea, ordena por prioridad ASCENDENTE (estable): la de mayor
	/// `(superficie, z)` se emite la última y, en el Copper, la última escritura al mismo
	/// registro de la misma línea es la que manda. Es la resolución de conflictos de
	/// `OBJECT_SYSTEM.md` §7 para intenciones que comparten `top`. Coste O(k²) por línea
	/// con k = intenciones de esa línea (pequeño en la práctica: k=1 en un cielo por línea).
	void sort_priority_within_lines() {
		ENG_PROF_BEGIN(eng::debug::prof_sort_prio);
		for (u16 l = 0; l < 256u; ++l) {
			const u16 lo = m_line_start[l];
			const u16 hi = m_line_start[static_cast<u16>(l + 1u)];
			for (u16 i = static_cast<u16>(lo + 1u); i < hi; ++i) {
				const u16 cur = m_perm[i];
				const u16 pr = m_prio[cur];
				u16 j = i;
				while (j > lo && m_prio[m_perm[static_cast<u16>(j - 1u)]] > pr) {
					m_perm[j] = m_perm[static_cast<u16>(j - 1u)];
					--j;
				}
				m_perm[j] = cur;
			}
		}
		ENG_PROF_END(eng::debug::prof_sort_prio);
	}

	/// Línea de raster relativa al inicio del display (el listado envuelve a 256 líneas).
	constexpr u8 raster_key(u16 top) const {
		return static_cast<u8>((static_cast<u16>(top) - m_cfg.first_line) & 0xffu);
	}

	PlanConfig m_cfg {}; ///< configuración del plan (tamaño de bloque, `first_line`)
	DoubleBuffer m_owned {}; ///< doble buffer propio (dueño) de la copperlist
	eng::Ref<DoubleBuffer> m_copper {}; // no-propietario
	Scheduler m_sched {}; ///< emisor de MOVE/WAIT compartido con el `Plan`
	/// `Array` (no `T v[N]`) para que el tamaño viaje con el objeto; mismo layout y
	/// coste cero (`eng::util::Array`). `max_intents`/256 líneas.
	eng::util::Array<graphics::CopperIntent, max_intents> m_intents {}; ///< intenciones registradas este frame
	eng::util::Array<u16, max_intents> m_prio {};      ///< (superficie << 8) | z
	eng::util::Array<u16, max_intents> m_perm {};      ///< orden de emisión (índices a `m_intents`)
	eng::util::Array<u16, max_intents> m_slot_word {}; ///< palabra de DATO de cada intent materializado
	eng::util::Array<u8, max_intents> m_slot_reg {};   ///< registro COLOR de arranque (identidad del slot)
	eng::util::Array<u16, max_intents> m_slot_line {}; ///< línea raster (identidad del slot)
	u16 m_slot_count = 0; ///< nº de slots registrados en el último `materialize()`
	eng::util::Array<u16, 257u> m_line_start {};       ///< inicio de grupo por línea
	/// Contadores del counting sort: miembros (no pila) para que el compilador no
	/// reconstruya el marco ni recalcule punteros a la pila en cada acceso.
	eng::util::Array<u16, 256u> m_count_by_line {};    ///< nº de intenciones por línea
	eng::util::Array<u16, 256u> m_line_cursor {};      ///< cursor de relleno por línea (counting)
	eng::res::AssetDmaLease m_dma_assets[max_dma_assets] {}; ///< owners Chip retenidos hasta desmontar el plan
	u8 m_dma_asset_count = 0u; ///< leases activas, 0..max_dma_assets
	u16 m_count = 0;       ///< nº de intenciones registradas
	eng::util::Array<BandScope, max_bands> m_bands {}; ///< reservas de banda del frame
	u8 m_band_count = 0;   ///< nº de reservas de banda
	eng::util::Array<EffectCost, max_bands> m_costs {}; ///< costes declarados por efecto
	u16 m_cost_words = 0;  ///< palabras declaradas acumuladas
	u8 m_cost_count = 0;   ///< nº de efectos con coste registrado
	u8 m_over_effect = no_effect; ///< primer efecto que agotó el presupuesto
	u16 m_words = 0;       ///< palabras de Copper de la última lista materializada
	ScheduleReport m_report {}; ///< informe del scheduler de la última materialización
	bool m_overflow = false;    ///< se superó `max_intents`
	bool m_ok = false;          ///< el plan posee o tiene enlazado un buffer válido
};

} // namespace eng::copper
