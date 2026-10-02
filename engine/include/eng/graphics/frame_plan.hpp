#pragma once

/// \file frame_plan.hpp
/// Descripcion minima de trabajos de render para un frame.
///
/// Este archivo empieza a separar tres niveles:
///
/// - la logica de juego/effectos dice que quiere cambiar;
/// - el `FramePlan` recoge esas intenciones de forma portable;
/// - el driver Amiga decide si las materializa como parches de copperlist, blits,
///   sprites hardware, CPU writes o cualquier otro mecanismo.
///
/// La primera version contenia solo parches de paleta. Ahora tambien describe
/// operaciones de Blitter para que las demos puedan pedir copias, save/restore y
/// BOBs sin escribir registros custom desde el juego. La tercera pieza es la lista
/// de dirty rects: las areas de pantalla que un frame ha tocado y que, por tanto,
/// pueden necesitar restauracion, redraw o analisis de presupuesto.
///
/// ```text
///   lógica de juego/efectos          FramePlan (portable)               driver Amiga
///   ──────────────────────           ────────────────────               ────────────
///   "cambia paleta 1..7"  ──►  PalettePatch[] ─┐
///   "copia/restaura BOB"  ──►  BlitJob[]      ─┼──► decide la materialización: parches de
///                                              │    copperlist · blits · sprites hardware ·
///   área tocada           ──►  dirty rects   ─┘    escrituras CPU (según el backend)
/// ```

#include <eng/core/math/arith.hpp>
#include <eng/core/types/box.hpp>
#include <eng/core/types/domains.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/array.hpp>
#include <eng/graphics/blit_job.hpp>
#include <eng/res/asset_cache.hpp>

namespace eng::graphics {

/// Lugar logico donde se aplicara un parche de paleta.
enum class PalettePatchTarget : u8 {
	Base,
	Zone,
};

/// Cambio de uno o varios colores fisicos RGB444.
///
/// `colors` es la paleta de dominio (`PaletteWords`, con su tamaño); `first/count`
/// selecciona el tramo que se quiere aplicar. Un efecto puede pedir "actualiza
/// colores 1..7" pasando su paleta completa sin crear arrays temporales.
struct PalettePatch {
	PalettePatchTarget target = PalettePatchTarget::Base;
	u8 line = 0;
	u8 first = 0;
	u8 count = 0;
	eng::PaletteWords colors {};
};

/// Tipos de trabajo de Blitter soportados por el plan actual.

/// Presupuesto acumulado de Blitter.
///
/// Es una estimacion deliberadamente sencilla: words procesadas por plano. No
/// intenta predecir todavia ciclos exactos de bus, pero ya permite comparar un BOB
/// de 16x16 con uno de 64x64 y fallar tests si una escena crece sin control.
struct BlitBudget {
	u16 jobs = 0;
	u32 words = 0;
	u16 masked_jobs = 0;
	u16 copy_jobs = 0;
	u16 no_save_jobs = 0;
	u16 tile_jobs = 0;
	u16 clear_jobs = 0;
};

/// Severidad del presupuesto de Blitter para un frame.
///
/// `Warning` significa que el frame todavia se puede materializar, pero la escena
/// esta entrando en una zona cara que conviene mostrar en telemetria. `Exceeded`
/// no descarta jobs automaticamente: el driver o la demo decide si degrada,
/// difiere trabajo o falla. Separar el diagnostico de la ejecucion permite usar el
/// mismo `FramePlan` en demos didacticas y en futuros drivers mas agresivos.
enum class BlitBudgetStatus : u8 {
	Ok,
	Warning,
	Exceeded,
};

/// Limites configurables de Blitter.
///
/// Los limites son intencionadamente abstractos: words procesadas por plano y
/// cantidad de jobs. Todavia no modelan ciclos exactos del bus Amiga, pero ya
/// fijan una frontera verificable para evitar que un frame crezca sin control.
struct BlitBudgetLimits {
	u32 warning_words = 0xffffffffu;
	u32 max_words = 0xffffffffu;
	u16 warning_jobs = 0xffffu;
	u16 max_jobs = 0xffffu;
};

/// Informe derivado de comparar `BlitBudget` contra `BlitBudgetLimits`.
struct BlitBudgetReport {
	BlitBudgetStatus status = BlitBudgetStatus::Ok;
	bool words_warning = false;
	bool words_exceeded = false;
	bool jobs_warning = false;
	bool jobs_exceeded = false;
};

/// **Política de ordenación de los trabajos del frame** (explícita, opt-in).
///
/// Por defecto el plan respeta el **orden de emisión**: dos trabajos que escriben la misma
/// región se ejecutan en el orden en que el juego los pidió (la última escritura manda).
/// Reordenar por estado del Blitter mejora la caché de registros, pero **solo es correcto
/// si los trabajos no se solapan** (no comparten píxeles de destino). Por eso la
/// reordenación es **explícita**: el juego declara `GroupByState` cuando puede garantizar
/// esa independencia. Ver `docs/engine/architecture/INTENT_PLANNER.md`.
enum class ReorderPolicy : u8 {
	PreserveOrder, ///< respeta el orden de emisión (seguro; por defecto)
	GroupByState,  ///< agrupa por estado común del Blitter; **requiere trabajos sin solape**
};

/// Rectangulo de pantalla en pixels.
///
/// Usamos coordenadas enteras pequenas y bordes exclusivos (`right/bottom`). Es el
/// formato mas comodo para fusionar rectangulos y para convertir despues a words
/// de Blitter, tiles o regiones de captura.
struct DirtyRect {
	s16 left = 0;
	s16 top = 0;
	s16 right = 0;
	s16 bottom = 0;

	constexpr bool valid() const {
		return right > left && bottom > top;
	}

	constexpr u16 width() const {
		return valid() ? static_cast<u16>(right - left) : 0;
	}

	constexpr u16 height() const {
		return valid() ? static_cast<u16>(bottom - top) : 0;
	}
};

/// Conversión `eng::Box` → `DirtyRect` (bordes `right`/`bottom` **exclusivos**).
[[nodiscard]] constexpr DirtyRect dirty_rect_of(const eng::Box& b) {
	return { b.x, b.y, static_cast<s16>(b.x + b.w), static_cast<s16>(b.y + b.h) };
}

/// Conversión `DirtyRect` → `eng::Box` (inclusivo en `right`/`bottom`).
[[nodiscard]] constexpr eng::Box box_of(const DirtyRect& d) {
	return d.valid() ? eng::Box::from_ltrb(d.left, d.top, static_cast<s16>(d.right - 1),
					       static_cast<s16>(d.bottom - 1))
			 : eng::Box {};
}

/// Metricas de dirty rects tras fusionar.
struct DirtyReport {
	u8 rects = 0;
	u16 area = 0;
	u16 merges = 0;
	bool overflow = false;
};


/// Plan de render de un frame.
///
/// Es un contenedor fijo, sin heap y sin STL. Cuando se llene, `ok()` pasa a false
/// para que la demo o el driver pueda fallar de forma controlada. En un engine de
/// Amiga es mejor rechazar un plan demasiado grande que degradarse en silencio y
/// descubrir corrupcion visual varios sistemas mas tarde.
class FramePlan {
public:
	static constexpr u8 kMaxPalettePatches = 8u;
	static constexpr u8 kMaxDirtyRects = 8u;
	static constexpr u8 kMaxDmaAssets = 8u;
	static constexpr u8 kMaxBlitJobs = 128u;
	static constexpr u8 max_palette_patches = kMaxPalettePatches;
	// A dual-playfield ring crossing both axes needs 12 shift copies plus 80
	// tile uploads (two playfields), so the former limit of 64 rejected a valid
	// frame plan before the backend could run it.
	static constexpr u8 max_blit_jobs = kMaxBlitJobs;
	static constexpr u8 max_dirty_rects = kMaxDirtyRects;
	static constexpr u8 max_dma_assets = kMaxDmaAssets; ///< owners Chip retenibles por nivel, sin coste en el ciclo del frame

	void clear() {
		m_palette_patch_count = 0;
		m_blit_job_count = 0;
		m_dirty_rect_count = 0;
		m_notify_count = 0;
		m_blit_budget = {};
		m_blit_budget_report = {};
		m_dirty_report = {};
		m_budget_dirty = false;
		m_ok = true;
	}

	/// Retiene un asset DMA Chip durante la vida de la escena; se adquiere en setup y no por frame.
	[[nodiscard]] bool retain_dma_asset(eng::res::AssetDmaLease&& lease) noexcept {
		if (!m_ok || !lease.valid() || m_dma_asset_count >= max_dma_assets) return false;
		m_dma_assets[m_dma_asset_count++] = static_cast<eng::res::AssetDmaLease&&>(lease);
		return true;
	}
	/// Suelta las leases al desmontar la escena, tras retirar sus consumidores DMA.
	void release_dma_assets() noexcept {
		for (u8 i = 0u; i < m_dma_asset_count; ++i) m_dma_assets[i].reset();
		m_dma_asset_count = 0u;
	}
	[[nodiscard]] constexpr u8 dma_asset_count() const noexcept { return m_dma_asset_count; }

	bool add_base_palette_patch(eng::PaletteWords colors, u8 first = 0, u8 count = 32) {
		return add_palette_patch({PalettePatchTarget::Base, 0, first, count, colors});
	}

	bool add_zone_palette_patch(u8 line, eng::PaletteWords colors, u8 first = 0, u8 count = 32) {
		return add_palette_patch({PalettePatchTarget::Zone, line, first, count, colors});
	}

	constexpr bool ok() const { return m_ok; }
	constexpr u8 palette_patch_count() const { return m_palette_patch_count; }

	/// **Agrupa los blits por estado común** (opt-in **explícito**). Reordena los `BlitJob` de
	/// forma **estable** para que los que comparten el **mismo estado del Blitter** —`kind`,
	/// `minterm`, `source_shift`, `descending`, `bitplane_count`, `words_per_row`, módulos e
	/// `interleaved`— queden **adyacentes**: así el backend encadena rachas y la caché de estado
	/// común de `submit_blit_job` omite las reprogramaciones. Conserva el orden relativo dentro de
	/// cada grupo (sort estable).
	///
	/// **Solo actúa si la política es `GroupByState`** (`set_reorder_policy`): con el defecto
	/// `PreserveOrder` no hace nada, porque reordenar cambiaría el resultado de trabajos que se
	/// solapan. El llamador declara la política —y con ella— que sus trabajos **no se solapan** en
	/// el destino (p. ej. blits a zonas disjuntas: tiles, columnas). Ver `RASTER.md`
	/// §"Prioridades" (agrupación) y `docs/engine/architecture/BLITTER_INTENT_QUEUE.md`.
	void sort_by_state() {
		if (m_reorder != ReorderPolicy::GroupByState) {
			return;
		}
		// Con avisos encolados, reordenar invalidaría sus puntos (`after_jobs` cuenta trabajos en
		// orden de declaración). Se respeta el orden: la agrupación es una optimización, los
		// avisos son un contrato. Ver `BLITTER_INTENT_QUEUE.md` §6.
		if (m_notify_count != 0u) {
			return;
		}
		// Sort por inserción estable (N pequeño, sin heap). `key_less` compara el estado común.
		for (u8 i = 1u; i < m_blit_job_count; ++i) {
			const BlitJob key = m_blit_jobs[i];
			u8 j = i;
			while (j > 0u && state_less(key, m_blit_jobs[j - 1u])) {
				m_blit_jobs[j] = m_blit_jobs[j - 1u];
				--j;
			}
			m_blit_jobs[j] = key;
		}
	}

	/// **Política de ordenación** de los trabajos (explícita). `GroupByState` habilita
	/// `sort_by_state()`; el llamador garantiza con ella que sus trabajos no se solapan.
	constexpr void set_reorder_policy(ReorderPolicy p) noexcept { m_reorder = p; }
	[[nodiscard]] constexpr ReorderPolicy reorder_policy() const noexcept { return m_reorder; }

	constexpr u8 blit_job_count() const { return m_blit_job_count; }
	constexpr u8 dirty_rect_count() const { return m_dirty_rect_count; }
	constexpr const BlitBudget& blit_budget() const { return m_blit_budget; }
	constexpr const BlitBudgetLimits& blit_budget_limits() const { return m_blit_budget_limits; }
	/// Informe de presupuesto. Se reconstruye **de forma perezosa** si quedó sucio tras el último
	/// `add`/`commit` (así el camino de encolado no es O(N²)); `finalize()` también lo refresca.
	const BlitBudgetReport& blit_budget_report() const {
		if (m_budget_dirty) {
			const_cast<FramePlan*>(this)->rebuild_blit_budget_report();
			m_budget_dirty = false;
		}
		return m_blit_budget_report;
	}
	constexpr const DirtyReport& dirty_report() const { return m_dirty_report; }

	/// **Marca de aviso** de la cadena: un `ticket` que la IRQ de fin de blit postea al cruzar el
	/// punto `after_jobs` (cuando ya han terminado ese número de trabajos). Un aviso al final de
	/// una ristra se declara **después** de encolar todos sus trabajos (`after_jobs` = total).
	struct NotifyMark {
		u8 after_jobs; ///< nº de trabajos que deben completarse antes de disparar
		u16 ticket;    ///< Id que viaja en el `MsgType::IntentDone`
	};
	static constexpr u8 kMaxNotifies = 16u;

	/// Encola un aviso con `ticket`, que se disparará cuando hayan terminado los trabajos
	/// encolados **hasta ahora**. Para avisar al final de la ristra, llamar tras el último
	/// `sprite`/`clear_box`/… Es una **intención más** del plan (no un trabajo): no ocupa Blitter.
	bool add_notify(u16 ticket) noexcept {
		if (m_notify_count >= kMaxNotifies) {
			m_ok = false;
			return false;
		}
		m_notifies[m_notify_count++] = NotifyMark {m_blit_job_count, ticket};
		return true;
	}
	[[nodiscard]] constexpr u8 notify_count() const noexcept { return m_notify_count; }
	[[nodiscard]] constexpr const NotifyMark& notify(u8 index) const noexcept {
		return m_notifies[index];
	}

	void set_blit_budget_limits(BlitBudgetLimits limits) {
		m_blit_budget_limits = limits;
		rebuild_blit_budget_report();
	}

	constexpr const PalettePatch& palette_patch(u8 index) const {
		return m_palette_patches[index];
	}

	constexpr const BlitJob& blit_job(u8 index) const {
		return m_blit_jobs[index];
	}

	constexpr const DirtyRect& dirty_rect(u8 index) const {
		return m_dirty_rects[index];
	}

	/// Anade un rectangulo sucio fusionandolo con los existentes.
	///
	/// Dos rectangulos se fusionan si se solapan o se tocan. Esto evita trabajos
	/// pequenos redundantes cuando un BOB se mueve poco: el area anterior y la nueva
	/// suelen formar una unica banda que conviene tratar junta a nivel de scheduler,
	/// aunque internamente el Blitter siga haciendo save/restore/draw concretos.
	bool add_dirty_rect(DirtyRect rect) {
		if (!rect.valid()) {
			m_ok = false;
			return false;
		}

		for (u8 i = 0; i < m_dirty_rect_count; ++i) {
			if (rects_touch_or_overlap(m_dirty_rects[i], rect)) {
				m_dirty_rects[i] = union_rect(m_dirty_rects[i], rect);
				++m_dirty_report.merges;
				rebuild_dirty_report();
				return true;
			}
		}

		if (m_dirty_rect_count >= max_dirty_rects) {
			m_dirty_report.overflow = true;
			m_ok = false;
			return false;
		}

		m_dirty_rects[m_dirty_rect_count++] = rect;
		rebuild_dirty_report();
		return true;
	}

	bool add_masked_bob(const BlitJob& job) {
		return add_blit_job(job, BlitJobKind::MaskedBobCookieCut);
	}

	bool add_masked_blob_no_save(const BlitJob& job) {
		return add_blit_job(job, BlitJobKind::MaskedBlobNoSave);
	}

	bool add_copy_rect(const BlitJob& job) {
		return add_blit_job(job, BlitJobKind::CopyRect);
	}

	bool add_restore_rect(const BlitJob& job) {
		return add_blit_job(job, BlitJobKind::RestoreRect);
	}

	bool add_tile_block_copy(const BlitJob& job) {
		return add_blit_job(job, BlitJobKind::TileBlockCopy);
	}

	/// Borrado de un rectángulo (solo D, minterm `$00`).
	bool add_clear_rect(const BlitJob& job) {
		return add_blit_job(job, BlitJobKind::ClearRect);
	}

	/// Clear con stride arbitrario dentro de cada scanline física (p. ej. interleaved).
	bool add_interleaved_clear_rect(const BlitJob& job) {
		if (!job.interleaved || job.height == 0u || job.bitplane_count <= 1u ||
		    job.destination.words() == nullptr || job.words_per_row == 0u ||
		    job.destination_plane_stride_bytes == 0u) {
			m_ok = false;
			return false;
		}
		if (m_blit_job_count >= max_blit_jobs) {
			m_ok = false;
			return false;
		}
		BlitJob clear_job = job;
		clear_job.kind = BlitJobKind::ClearRect;
		m_blit_jobs[m_blit_job_count++] = clear_job;
		m_blit_budget.jobs = m_blit_job_count;
		m_blit_budget.words += eng::math::mulu32x16(
			eng::math::mulu16(job.words_per_row, job.height), job.bitplane_count);
		++m_blit_budget.clear_jobs;
		++m_blit_budget.copy_jobs;
		rebuild_blit_budget_report();
		return true;
	}

	/// BOB OR (aditivo) por desplazamiento: `A`=objeto, `B=D`=destino, minterm `$FC`.
	/// El llamador rellena `source`/`destination` y deja `mask` vacia.
	__attribute__((always_inline)) inline bool add_or_blob(const BlitJob& job) {
		return add_blit_job(job, BlitJobKind::OrBlob);
	}

	/// **Blit con operación lógica** (`B = D`, minterm en `job.minterm`): `A` = fuente.
	/// El llamador fija `source`/`destination` y el minterm (`Or`/`And`/`Xor`).
	__attribute__((always_inline)) inline bool add_logic_blit(const BlitJob& job) {
		return add_blit_job(job, BlitJobKind::LogicBlit);
	}

	/// **Relleno con patrón** (`A` = patrón, `B = D` = destino, minterm `$FC`): el patrón es
	/// una fila de `words_per_row` palabras repetida en vertical con `source_modulo_bytes`.
	/// El llamador fija `source` (fila del patrón), `destination`, `words_per_row`,
	/// `height`, `bitplane_count` y `source_modulo_bytes = -(words_per_row*2)`.
	__attribute__((always_inline)) inline bool add_pattern_fill(const BlitJob& job) {
		return add_blit_job(job, BlitJobKind::PatternFill);
	}

	/// **Chunky→planar por Blitter** (`BlitJobKind::C2P`): `c2p.chunky` (Chip RAM, 2×
	/// `c2p.bytes`) → `c2p.planes` (4 planos). La vía Blitter del seam `Rasterizer::c2p`.
	bool add_c2p(const BlitJob& job) {
		if (job.c2p.chunky == nullptr || job.c2p.planes == nullptr ||
		    job.c2p.bytes == 0u || job.c2p.plane_stride == 0u) {
			m_ok = false;
			return false;
		}
		if (m_blit_job_count >= max_blit_jobs) {
			m_ok = false;
			return false;
		}
		BlitJob j = job;
		j.kind = BlitJobKind::C2P;
		m_blit_jobs[m_blit_job_count++] = j;
		m_blit_budget.jobs = m_blit_job_count;
		m_blit_budget.words += eng::math::mulu16(job.c2p.bytes, 13u); // 13 fases
		++m_blit_budget.copy_jobs;
		rebuild_blit_budget_report();
		return true;
	}

	/// **Línea por Blitter** (`BLTCON1` LINE) o **EOR/ONEDOT** (`LineEor`): `destination`
	/// = plano, `line.x0..line.y1` las coordenadas y `line.row_bytes` el módulo de fila.
	/// Sin fuentes/máscara. Para `LineEor`, `line.base` es la base del canal D.
	bool add_line(const BlitJob& job, BlitJobKind kind = BlitJobKind::Line) {
		if (job.destination.words() == nullptr || job.line.row_bytes == 0u ||
		    job.bitplane_count == 0u) {
			m_ok = false;
			return false;
		}
		if (m_blit_job_count >= max_blit_jobs) {
			m_ok = false;
			return false;
		}
		BlitJob j = job;
		j.kind = kind;
		m_blit_jobs[m_blit_job_count++] = j;
		m_blit_budget.jobs = m_blit_job_count;
		// Coste aproximado: una word por fila de la línea + 2 de arranque por plano
		// (`mulu16` = `mulu.w`, sin `__mulsi3`).
		const s32 dy = job.line.y1 > job.line.y0 ? job.line.y1 - job.line.y0
							 : job.line.y0 - job.line.y1;
		m_blit_budget.words += eng::math::mulu16(static_cast<u16>(dy + 3),
							 job.bitplane_count);
		++m_blit_budget.copy_jobs;
		rebuild_blit_budget_report();
		return true;
	}

	/// **Genérico**: añade un trabajo ya formado, **despachando por `job.kind`**. Es el punto de
	/// entrada del sumidero de la cola de intención (`BlitQueue` → plan): convierte la
	/// intención a `BlitJob` (`blit_job_from`) y lo añade aquí sin conocer el tipo concreto.
	bool add(const BlitJob& job) {
		switch (job.kind) {
		case BlitJobKind::C2P:
			return add_c2p(job);
		case BlitJobKind::Line:
			return add_line(job, BlitJobKind::Line);
		case BlitJobKind::LineEor:
			return add_line(job, BlitJobKind::LineEor);
		default:
			return add_blit_job(job, job.kind);
		}
	}

	/// **Construcción IN SITU** de un trabajo (camino caliente, coste cero): devuelve la ranura
	/// del array para que el llamador **rellene los campos directamente**, sin construir un
	/// `BlitJob` local (que en `-O0` es un `memset` + `memcpy` por objeto) ni copiarlo. Cierra con
	/// `commit_blit_job()`. El llamador debe fijar **todos los campos que el encoder lea** para ese
	/// `kind`; los grupos no usados (`line`/`c2p`) conservan valores previos (no se leen).
	/// Ver `docs/engine/architecture/ZERO_COST_FRAME_PATH.md`.
	[[nodiscard]] BlitJob& begin_blit_job(BlitJobKind kind) noexcept {
		BlitJob& slot = m_blit_jobs[m_blit_job_count];
		slot.kind = kind;
		return slot;
	}

	/// Valida y contabiliza la ranura abierta por `begin_blit_job`. `false` (y `ok()==false`) si
	/// el trabajo es inválido o no cabe; en ese caso la ranura **no** se consume.
	bool commit_blit_job() noexcept {
		if (m_blit_job_count >= max_blit_jobs) {
			m_ok = false;
			return false;
		}
		return finish_blit_job();
	}

	/// Reconstruye el informe de presupuesto si quedó sucio. Lo llama `App::present` al cerrar el
	/// plan; también de forma perezosa al leer `blit_budget_report()`.
	void finalize() noexcept {
		if (m_budget_dirty) {
			rebuild_blit_budget_report();
			m_budget_dirty = false;
		}
	}

private:
	static constexpr s16 min_s16(s16 a, s16 b) { return a < b ? a : b; }

	/// Orden **estable por estado común del Blitter** (agrupa rachas sin mirar punteros/tamaño).
	/// Clave: `kind`, `minterm`, `source_shift`, `descending`, `bitplane_count`, `words_per_row`,
	/// módulos de origen/destino e `interleaved`. Empate → `false` (el sort estable conserva orden).
	[[nodiscard]] static constexpr bool state_less(const BlitJob& a, const BlitJob& b) {
		if (a.kind != b.kind) return a.kind < b.kind;
		if (a.minterm != b.minterm) return a.minterm < b.minterm;
		if (a.source_shift != b.source_shift) return a.source_shift < b.source_shift;
		if (a.descending != b.descending) return a.descending < b.descending;
		if (a.bitplane_count != b.bitplane_count) return a.bitplane_count < b.bitplane_count;
		if (a.words_per_row != b.words_per_row) return a.words_per_row < b.words_per_row;
		if (a.source_modulo_bytes != b.source_modulo_bytes)
			return a.source_modulo_bytes < b.source_modulo_bytes;
		if (a.destination_modulo_bytes != b.destination_modulo_bytes)
			return a.destination_modulo_bytes < b.destination_modulo_bytes;
		if (a.interleaved != b.interleaved) return a.interleaved < b.interleaved;
		return false;
	}
	static constexpr s16 max_s16(s16 a, s16 b) { return a > b ? a : b; }

	static constexpr bool rects_touch_or_overlap(const DirtyRect& a, const DirtyRect& b) {
		return !(a.right < b.left || b.right < a.left || a.bottom < b.top || b.bottom < a.top);
	}

	static constexpr DirtyRect union_rect(const DirtyRect& a, const DirtyRect& b) {
		return {
			min_s16(a.left, b.left),
			min_s16(a.top, b.top),
			max_s16(a.right, b.right),
			max_s16(a.bottom, b.bottom),
		};
	}

	void rebuild_dirty_report() {
		const u16 previous_merges = m_dirty_report.merges;
		const bool previous_overflow = m_dirty_report.overflow;
		m_dirty_report = {};
		m_dirty_report.rects = m_dirty_rect_count;
		m_dirty_report.merges = previous_merges;
		m_dirty_report.overflow = previous_overflow;

		for (u8 i = 0; i < m_dirty_rect_count; ++i) {
			m_dirty_report.area = static_cast<u16>(
				m_dirty_report.area +
				static_cast<u16>(m_dirty_rects[i].width() * m_dirty_rects[i].height())
			);
		}
	}

	/// Camino caliente (1 vez por BOB): `always_inline` para no pagar un `jsr` por objeto
	/// ni recargar `m_blit_job_count`/`m_blit_budget` desde memoria en cada anadido.
	__attribute__((always_inline)) inline bool add_blit_job(const BlitJob& input, BlitJobKind kind) {
		if (m_blit_job_count >= max_blit_jobs) {
			m_ok = false;
			return false;
		}
		// Copia UNICA: se escribe directamente en la ranura del array (sin local intermedio).
		BlitJob& job = m_blit_jobs[m_blit_job_count];
		job = input;
		job.kind = kind;
		return finish_blit_job();
	}

	/// Valida la última ranura, la consume y suma el presupuesto. **No** reconstruye el informe
	/// (diferido: `finalize`/acceso al informe); así añadir N trabajos no es O(N²).
	__attribute__((always_inline)) inline bool finish_blit_job() noexcept {
		BlitJob& job = m_blit_jobs[m_blit_job_count];
		const BlitJobKind kind = job.kind;
		const bool masked = kind == BlitJobKind::MaskedBobCookieCut ||
				    kind == BlitJobKind::MaskedBlobNoSave;
		const bool clear = kind == BlitJobKind::ClearRect;
		if ((!clear && job.source.words() == nullptr) || job.destination.words() == nullptr ||
		    job.words_per_row == 0 || job.height == 0 || job.bitplane_count == 0 ||
		    job.source_shift >= 16u ||
		    (!clear && job.source_plane_stride_bytes == 0 && !job.interleaved) ||
		    (job.destination_plane_stride_bytes == 0 && !job.interleaved) ||
		    (masked && job.mask.words() == nullptr)) {
			m_ok = false;
			return false;
		}
		++m_blit_job_count;
		// El presupuesto se acumula **una vez** en `finalize()` (recorrido O(N)): así encolar N
		// trabajos no paga por-job ni es O(N²). Ver `ZERO_COST_FRAME_PATH.md`.
		m_budget_dirty = true;
		return true;
	}

	void rebuild_blit_budget_report() {
		// Recomputa el presupuesto desde cero en un único recorrido (coste O(N) por frame).
		m_blit_budget = {};
		for (u8 i = 0u; i < m_blit_job_count; ++i) {
			const BlitJob& job = m_blit_jobs[i];
			m_blit_budget.words += eng::math::mulu32x16(
				eng::math::mulu16(job.words_per_row, job.height),
				static_cast<u16>(job.bitplane_count));
			switch (job.kind) {
				case BlitJobKind::MaskedBobCookieCut:
				case BlitJobKind::MaskedBlobNoSave:
					++m_blit_budget.masked_jobs;
					break;
				default:
					++m_blit_budget.copy_jobs;
					break;
			}
			if (job.kind == BlitJobKind::ClearRect) ++m_blit_budget.clear_jobs;
			if (job.kind == BlitJobKind::MaskedBlobNoSave) ++m_blit_budget.no_save_jobs;
			if (job.kind == BlitJobKind::TileBlockCopy) ++m_blit_budget.tile_jobs;
		}
		m_blit_budget.jobs = m_blit_job_count;
		m_blit_budget_report = {};
		m_blit_budget_report.words_warning = m_blit_budget.words > m_blit_budget_limits.warning_words;
		m_blit_budget_report.words_exceeded = m_blit_budget.words > m_blit_budget_limits.max_words;
		m_blit_budget_report.jobs_warning = m_blit_budget.jobs > m_blit_budget_limits.warning_jobs;
		m_blit_budget_report.jobs_exceeded = m_blit_budget.jobs > m_blit_budget_limits.max_jobs;

		if (m_blit_budget_report.words_exceeded || m_blit_budget_report.jobs_exceeded) {
			m_blit_budget_report.status = BlitBudgetStatus::Exceeded;
		} else if (m_blit_budget_report.words_warning || m_blit_budget_report.jobs_warning) {
			m_blit_budget_report.status = BlitBudgetStatus::Warning;
		}
	}

	bool add_palette_patch(PalettePatch patch) {
		if (patch.colors.empty() || patch.first >= 32u || patch.count == 0u) {
			m_ok = false;
			return false;
		}
		// La vista debe cubrir el tramo pedido.
		if (static_cast<eng::u32>(patch.first) + patch.count > patch.colors.size()) {
			m_ok = false;
			return false;
		}
		if (patch.first + patch.count > 32u) {
			patch.count = static_cast<u8>(32u - patch.first);
		}
		if (m_palette_patch_count >= max_palette_patches) {
			m_ok = false;
			return false;
		}

		m_palette_patches[m_palette_patch_count++] = patch;
		return true;
	}

	eng::util::Array<PalettePatch, max_palette_patches> m_palette_patches {};
	eng::util::Array<BlitJob, max_blit_jobs> m_blit_jobs {};
	eng::util::Array<DirtyRect, max_dirty_rects> m_dirty_rects {};
	eng::util::Array<NotifyMark, kMaxNotifies> m_notifies {}; ///< avisos de la cadena async
	BlitBudget m_blit_budget {};
	BlitBudgetLimits m_blit_budget_limits {};
	BlitBudgetReport m_blit_budget_report {};
	DirtyReport m_dirty_report {};
	eng::res::AssetDmaLease m_dma_assets[max_dma_assets] {}; ///< owners DMA retenidos durante la vida del nivel
	u8 m_palette_patch_count = 0;
	u8 m_blit_job_count = 0;
	u8 m_dirty_rect_count = 0;
	u8 m_notify_count = 0; ///< avisos registrados en `m_notifies`
	u8 m_dma_asset_count = 0; ///< leases válidas en `m_dma_assets`, 0..max_dma_assets
	ReorderPolicy m_reorder = ReorderPolicy::PreserveOrder; ///< política de orden (explícita)
	mutable bool m_budget_dirty = false; ///< el informe de presupuesto está pendiente de reconstruir
	bool m_ok = true;
};

} // namespace eng::graphics
