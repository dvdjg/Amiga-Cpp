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
/// Ver `docs/engine/architecture/DISPLAY_COMPOSITION.md` §5.

#include <eng/core/domains.hpp>
#include <eng/core/types.hpp>
#include <eng/graphics/copper/double_buffer.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/raster_intent.hpp>
#include <eng/memory/arena.hpp>

namespace eng::copper {

struct PlanConfig {
	u32 copper_bytes = 8192u; ///< tamaño de CADA bloque del doble buffer
};

class Plan {
public:
	/// Capacidad de intenciones por frame (fijo, sin heap). `add` marca overflow si se
	/// supera; `end_frame` devuelve false en ese caso (no se publica una lista parcial).
	static constexpr u8 max_intents = 64;

	bool begin(eng::MemorySystem& memory, const PlanConfig& cfg = {}) {
		m_cfg = cfg;
		m_count = 0;
		m_overflow = false;
		m_ok = m_owned.begin(memory, cfg.copper_bytes);
		m_copper = &m_owned;
		return m_ok;
	}

	/// Enlaza el plan a un `DoubleBuffer` **externo** (el llamador decide dónde vive la
	/// memoria: p. ej. el que ya posee un driver o un compositor). Así el plan no impone
	/// ser dueño de la buffering de copperlist; solo la orquesta (orden + presupuesto +
	/// publicación).
	void attach(DoubleBuffer& copper) {
		m_copper = &copper;
		m_count = 0;
		m_overflow = false;
		m_ok = copper.ok();
	}

	/// Abre el frame: limpia las intenciones y sitúa el emisor en el bloque **trasero**.
	void begin_frame() {
		m_count = 0;
		m_overflow = false;
		m_sched = m_copper->inactive_scheduler();
	}

	/// Emisor del frame (bloque trasero). El llamador emite aquí la parte estática.
	Scheduler& scheduler() { return m_sched; }
	const Scheduler& scheduler() const { return m_sched; }

	void add(const graphics::CopperIntent& it) {
		if (m_count < max_intents) {
			m_intents[m_count++] = it;
		} else {
			m_overflow = true;
		}
	}

	void add(const graphics::CopperIntent* intents, u8 count) {
		for (u8 i = 0; i < count; ++i) {
			add(intents[i]);
		}
	}

	/// Ordena las intenciones por `top` (scanline) y las emite en la posición ACTUAL del
	/// emisor (así el llamador decide dónde van: p. ej. tras la paleta base y antes de la
	/// cola). Orden estable: las de igual línea conservan el orden de inserción.
	void materialize() {
		sort_by_top();
		if (m_count != 0u) {
			m_sched.emit_copper_intents(m_intents, m_count);
		}
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

	constexpr u8 intent_count() const { return m_count; }
	constexpr bool overflow() const { return m_overflow; }
	constexpr bool ok() const { return m_ok; }
	constexpr u16 words() const { return m_words; }
	constexpr const ScheduleReport& report() const { return m_report; }

	/// Depuración/tests: words del bloque ACTIVO (el que ejecuta el Copper).
	constexpr u16* active_words() const { return m_copper->active_words(); }
	constexpr u16* inactive_words() const { return m_copper->inactive_words(); }

private:
	/// Ordenación por inserción (n ≤ 64, sin STL ni heap). El scheduler exige las
	/// intenciones en orden ASCENDENTE de línea; hacerlo aquí libera al llamador de ese
	/// invariante implícito.
	void sort_by_top() {
		for (u8 i = 1; i < m_count; ++i) {
			const graphics::CopperIntent key = m_intents[i];
			u8 j = i;
			while (j > 0u && m_intents[j - 1u].top > key.top) {
				m_intents[j] = m_intents[j - 1u];
				--j;
			}
			m_intents[j] = key;
		}
	}

	PlanConfig m_cfg {};
	DoubleBuffer m_owned {};
	DoubleBuffer* m_copper = nullptr;
	Scheduler m_sched {};
	graphics::CopperIntent m_intents[max_intents] {};
	u8 m_count = 0;
	u16 m_words = 0;
	ScheduleReport m_report {};
	bool m_overflow = false;
	bool m_ok = false;
};

} // namespace eng::copper
