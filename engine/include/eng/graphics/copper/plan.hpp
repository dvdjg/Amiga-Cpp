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

#include <eng/core/domains.hpp>
#include <eng/core/ptr.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/algorithm.hpp>
#include <eng/core/util/array.hpp>
#include <eng/graphics/copper/double_buffer.hpp>
#include <eng/debug/prof.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/raster_intent.hpp>
#include <eng/memory/arena.hpp>

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

class Plan {
public:
	/// Capacidad de intenciones por frame (fijo, sin heap). `add` marca overflow si se
	/// supera; `end_frame` devuelve false en ese caso (no se publica una lista parcial).
	/// Con 320 caben los gradientes **por línea** de una escena (256 líneas + objetos).
	static constexpr u16 max_intents = 320;

	bool begin(eng::MemorySystem& memory, const PlanConfig& cfg = {}) {
		m_cfg = cfg;
		m_count = 0;
		m_overflow = false;
		m_ok = m_owned.begin(memory, cfg.copper_bytes);
		m_copper = m_owned;
		return m_ok;
	}

	/// Enlaza el plan a un `DoubleBuffer` **externo** (el llamador decide dónde vive la
	/// memoria: p. ej. el que ya posee un driver o un compositor). Así el plan no impone
	/// ser dueño de la buffering de copperlist; solo la orquesta (orden + presupuesto +
	/// publicación).
	void attach(DoubleBuffer& copper) {
		m_copper = copper;
		m_count = 0;
		m_overflow = false;
		m_ok = copper.ok();
	}

	/// Abre el frame: limpia las intenciones y sitúa el emisor en el bloque **trasero**.
	void begin_frame() {
		m_count = 0;
		m_overflow = false;
		m_sched.retarget(m_copper->inactive_block()); // sin copiar la Timeline (512+ B)
	}

	/// Emisor del frame (bloque trasero). El llamador emite aquí la parte estática.
	Scheduler& scheduler() { return m_sched; }
	const Scheduler& scheduler() const { return m_sched; }

	void add(const graphics::CopperIntent& it) { add_prioritized(&it, 1u, 0u, 0u); }

	void add(const graphics::CopperIntent* intents, u16 count) {
		add_prioritized(intents, count, 0u, 0u);
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
		// Se emite por `m_perm`, pero **agrupando rachas contiguas del mismo tipo** para no
		// entrar/salir de `emit_copper_intents` una vez por intención (v1 llamaba 1 vez por
		// intención: ~355 instrucciones/intención medidas en la 086). Las intenciones no
		// contiguas en `m_intents` no se pueden emitir como lote sin copiarlas, así que se
		// emite de una en una pero con el emisor inline (sin coste de llamada por elemento).
		ENG_PROF_BEGIN(eng::debug::prof_emit);
		for (u16 i = 0; i < m_count; ++i) {
			m_sched.emit_copper_intents_fast(&m_intents[m_perm[i]], 1u);
		}
		ENG_PROF_END(eng::debug::prof_emit);
	}

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
	template <typename Backend>
	void commit(Backend& backend) const {
		m_copper->install(backend);
	}

	/// Toma el control del display mostrando el buffer delantero (una vez).
	template <typename Backend>
	void takeover(Backend& backend) const {
		m_copper->takeover(backend);
	}

	constexpr u16 intent_count() const { return m_count; }
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
	eng::util::Array<u16, 257u> m_line_start {};       ///< inicio de grupo por línea
	/// Contadores del counting sort: miembros (no pila) para que el compilador no
	/// reconstruya el marco ni recalcule punteros a la pila en cada acceso.
	eng::util::Array<u16, 256u> m_count_by_line {};    ///< nº de intenciones por línea
	eng::util::Array<u16, 256u> m_line_cursor {};      ///< cursor de relleno por línea (counting)
	u16 m_count = 0;       ///< nº de intenciones registradas
	u16 m_words = 0;       ///< palabras de Copper de la última lista materializada
	ScheduleReport m_report {}; ///< informe del scheduler de la última materialización
	bool m_overflow = false;    ///< se superó `max_intents`
	bool m_ok = false;          ///< la última lista cupo y quedó publicada
};

} // namespace eng::copper
