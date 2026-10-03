/// \file amiga_blitter.cpp
/// Servicio de **Blitter** del backend Amiga: ejecucion del `FramePlan` y operaciones
/// (copias, cookie-cut, OR de BOBs, lineas, area fill, colision, relleno por plano).

#include <eng/graphics/blit_queue.hpp>
#include <eng/platform/amiga/blob_batch.hpp>

#include "amiga_internal.hpp"

using namespace eng::amiga::detail;

namespace eng::amiga {

namespace {
/// Job de cookie-cut interleaved con el contrato del par `[imagen][mascara]`: un solo blit
/// (`bitplane_count == 1`) y el minterm `$CA`. Es el caso que puede ejecutarse como lote de
/// **estado fijo** (`BlobBatch`).
///
/// Referencia del lote y su motivación: `docs/engine/architecture/BLITTER_INTENT_QUEUE.md` y
/// `docs/guides/roadmap/ROADMAP_BLITTER_COPPER.md` (§"copias, cookie-cut y fast blobs").
/// La medición A/B (2026-10, demo 213) y el detalle del API quedan ahí para la doc del API.
bool batchable_masked(const eng::graphics::BlitJob& j) {
	return j.kind == eng::graphics::BlitJobKind::MaskedBobCookieCut && j.interleaved &&
	       j.bitplane_count == 1u && j.source_words_per_row == 0u && !j.descending &&
	       j.minterm == eng::graphics::kBlitterMintermCookieCut;
}

/// Dos jobs de cookie-cut comparten el estado **fijo** del lote si coinciden en todo menos
/// los punteros y el desplazamiento fino (`source_shift`).
bool same_masked_state(const eng::graphics::BlitJob& a, const eng::graphics::BlitJob& b) {
	return a.words_per_row == b.words_per_row && a.height == b.height &&
	       a.source_modulo_bytes == b.source_modulo_bytes &&
	       a.destination_modulo_bytes == b.destination_modulo_bytes &&
	       a.minterm == b.minterm;
}

/// Job de clear interleaved en **bloque continuo** (una fila física por plano, `D=0`): el
/// contrato del `clear_box` de banda completa (`bitplane_count == 1`, sin saltos entre planos).
bool batchable_clear(const eng::graphics::BlitJob& j) {
	return j.kind == eng::graphics::BlitJobKind::ClearRect && j.interleaved &&
	       j.bitplane_count == 1u && j.destination_plane_stride_bytes == 0u && !j.descending &&
	       j.minterm == 0x00u;
}

/// Dos clears comparten estado fijo si coinciden en ancho, módulo de destino y minterm.
bool same_clear_state(const eng::graphics::BlitJob& a, const eng::graphics::BlitJob& b) {
	return a.words_per_row == b.words_per_row &&
	       a.destination_modulo_bytes == b.destination_modulo_bytes && a.minterm == b.minterm;
}

/// Ejecuta una racha `[from, to)` de clears interleaved con estado fijo (`BlobBatch`).
bool execute_clear_run(const eng::graphics::FramePlan& plan, u8 from, u8 to) {
	const eng::graphics::BlitJob& f = plan.blit_job(from);
	BlobBatch batch;
	batch.begin(custom_base, BlobOp::Clear, f.words_per_row, f.height, 0, 0, 0,
		    f.destination_modulo_bytes, g_blitter_service, g_blitter_service_user);
	for (u8 i = from; i < to; ++i) {
		const eng::graphics::BlitJob& j = plan.blit_job(i);
		batch.one(nullptr, nullptr, j.destination.words(), 0u);
	}
	return batch.end();
}

/// Ejecuta una racha `[from, to)` de cookie-cut interleaved con el estado común fijado UNA vez
/// (`BlobBatch`): sin re-codificar por job y con **una sola espera** por objeto. Es el camino
/// del `main.c` de referencia, aplicado a una racha homogénea del `FramePlan`.
bool execute_masked_run(const eng::graphics::FramePlan& plan, u8 from, u8 to) {
	const eng::graphics::BlitJob& f = plan.blit_job(from);
	BlobBatch batch;
	batch.begin(custom_base, BlobOp::CookieCut, f.words_per_row, f.height,
		    f.source_modulo_bytes, f.source_modulo_bytes,
		    f.destination_modulo_bytes, f.destination_modulo_bytes,
		    g_blitter_service, g_blitter_service_user);
	for (u8 i = from; i < to; ++i) {
		const eng::graphics::BlitJob& j = plan.blit_job(i);
		batch.one(j.mask.words(), j.source.words(), j.destination.words(),
			  static_cast<eng::u8>(j.source_shift));
	}
	return batch.end();
}
} // namespace

bool AmigaBackend::execute_frame_plan(const graphics::FramePlan& plan) {
	if (!plan.ok()) {
		return false;
	}

	m_blitter_starts = 0;
	m_blt_common_hits = 0;
	// Habilita el DMA del Blitter **solo si no lo está** (el Copper ya lo activa en el display):
	// un MOVE a DMACON es caro con `cpu_cycle_exact` y escribirlo por job dominaba el bucle
	// (16 BOBs/frame). La lectura de DMACONR cuesta una fracción de la escritura.
	if ((custom_base[custom_dmaconr_offset] & dma_blitter) == 0u) {
		custom_base[custom_dmacon_offset] = dma_setclr | dma_master | dma_blitter;
	}
	bool eor_open = false; // racha de líneas EOR con los registros comunes ya fijados
	const u8 job_count = plan.blit_job_count();
	u8 job_index = 0u;
	while (job_index < job_count) {
		// Racha homogénea de cookie-cut interleaved: se ejecuta con estado fijo (`BlobBatch`),
		// sin re-codificar registros por job ni pagar la espera extra del bucle por plano.
		if (batchable_masked(plan.blit_job(job_index))) {
			u8 end = static_cast<u8>(job_index + 1u);
			while (end < job_count &&
			       same_masked_state(plan.blit_job(job_index), plan.blit_job(end))) {
				++end;
			}
			if (static_cast<u8>(end - job_index) >= 2u) {
				if (!execute_masked_run(plan, job_index, end)) {
					return false;
				}
				// El lote escribió registros directamente: la caché de estado común ya no
				// refleja el hardware.
				m_blt_common_valid = false;
				m_blitter_starts += static_cast<u16>(end - job_index);
				job_index = end;
				continue;
			}
		}
		if (batchable_clear(plan.blit_job(job_index))) {
			u8 end = static_cast<u8>(job_index + 1u);
			while (end < job_count &&
			       same_clear_state(plan.blit_job(job_index), plan.blit_job(end))) {
				++end;
			}
			if (static_cast<u8>(end - job_index) >= 2u) {
				if (!execute_clear_run(plan, job_index, end)) {
					return false;
				}
				m_blt_common_valid = false;
				m_blitter_starts += static_cast<u16>(end - job_index);
				job_index = end;
				continue;
			}
		}
		if (!submit_blit_job(plan.blit_job(job_index), eor_open)) {
			return false;
		}
		++job_index;
	}

	return wait_blitter();
}

/// Diagnóstico del feeder async de `FramePlan` (leíble por el canal lateral del emulador con
/// `tools/debug/probe_*`): lanzamientos del primer job, pasos de la ISR de encadenado (uno por
/// fin de blit), cierres de cadena y submits con éxito dentro de la ISR.
volatile eng::u32 g_blit_async_launches = 0u;
volatile eng::u32 g_blit_async_irqs = 0u;
volatile eng::u32 g_blit_async_ends = 0u;
volatile eng::u32 g_blit_async_submits = 0u;

bool AmigaBackend::execute_frame_plan_async(const graphics::FramePlan& plan) {
	if (!plan.ok() || plan.blit_job_count() == 0u) {
		return false;
	}
	// Instala el servicio de IRQ de blit (una vez). No se desinstala al acabar la cadena: se
	// reutiliza en el siguiente frame (la ISR es no-op si `m_async_busy` esta bajo).
	if (!m_async_service_installed) {
		if (!set_blit_service(&AmigaBackend::frame_plan_async_step, *this)) {
			return false;
		}
		m_async_service_installed = true;
	}
	// Si la cadena del frame anterior sigue viva, se espera (no se puede pisar el plan anterior).
	while (m_async_busy) {
		wait_blitter();
	}
	m_async_plan = &plan;
	m_async_next = 1u;
	m_async_notify_next = 0u;
	m_async_busy = true;
	g_blit_async_launches = g_blit_async_launches + 1u;
	// Avisos con `after_jobs == 0` (declarados antes de encolar trabajo): se disparan ya.
	fire_due_notifies();
	if (!blitter_submit(plan.blit_job(0u), /*wait=*/false)) {
		m_async_busy = false;
		return false;
	}
	return true;
}

/// Dispara los avisos cuyo punto ya alcanzó la cadena (los trabajos `[0, m_async_next)` están
/// hechos). Se llama desde la ISR y desde el lanzamiento inicial; el callback debe ser IRQ-safe.
void AmigaBackend::fire_due_notifies() noexcept {
	const graphics::FramePlan* plan = m_async_plan;
	if (plan == nullptr) {
		return;
	}
	while (m_async_notify_next < plan->notify_count()) {
		const graphics::FramePlan::NotifyMark& mark = plan->notify(m_async_notify_next);
		if (mark.after_jobs > m_async_next) {
			break;
		}
		if (m_chain_notify != nullptr) {
			m_chain_notify(m_chain_notify_ctx, mark.ticket);
		}
		++m_async_notify_next;
	}
}

void AmigaBackend::frame_plan_async_step(AmigaBackend& self, eng::u16) {
	g_blit_async_irqs = g_blit_async_irqs + 1u;
	const graphics::FramePlan* plan = self.m_async_plan;
	if (plan == nullptr) {
		self.m_async_busy = false;
		return;
	}
	// Al entrar aquí ha terminado el trabajo `m_async_next - 1`: dispara los avisos que toquen.
	self.fire_due_notifies();
	const u16 next = self.m_async_next;
	if (next >= plan->blit_job_count() ||
	    !self.blitter_submit(plan->blit_job(next), /*wait=*/false)) {
		self.m_async_busy = false;
		g_blit_async_ends = g_blit_async_ends + 1u;
		return;
	}
	g_blit_async_submits = g_blit_async_submits + 1u;
	self.m_async_next = static_cast<u16>(next + 1u);
}

bool AmigaBackend::frame_plan_async_busy() const noexcept {
	return m_async_busy;
}

bool AmigaBackend::blitter_submit(const graphics::BlitJob& job, bool wait) {
	m_blitter_starts = 0;
	m_blt_common_hits = 0;
	if ((custom_base[custom_dmaconr_offset] & dma_blitter) == 0u) {
		custom_base[custom_dmacon_offset] = dma_setclr | dma_master | dma_blitter;
	}
	bool eor_open = false;
	if (!submit_blit_job(job, eor_open)) {
		return false;
	}
	return wait ? wait_blitter() : true;
}

/// Cuerpo comun de `execute_frame_plan` (encadena varios) y `blitter_submit` (uno).
/// `eor_open` mantiene la racha de lineas EOR entre jobs consecutivos.
bool AmigaBackend::submit_blit_job(const graphics::BlitJob& job, bool& eor_open) {
	const bool masked =
		job.kind == graphics::BlitJobKind::MaskedBobCookieCut ||
		job.kind == graphics::BlitJobKind::MaskedBlobNoSave;
	const bool copy =
		job.kind == graphics::BlitJobKind::CopyRect ||
		job.kind == graphics::BlitJobKind::RestoreRect ||
		job.kind == graphics::BlitJobKind::TileBlockCopy;
	const bool clear = job.kind == graphics::BlitJobKind::ClearRect;
	const bool fill = job.kind == graphics::BlitJobKind::FillRect;
	const bool or_blob = job.kind == graphics::BlitJobKind::OrBlob ||
			     job.kind == graphics::BlitJobKind::PatternFill;
	const bool logic = job.kind == graphics::BlitJobKind::LogicBlit;
	const bool line = job.kind == graphics::BlitJobKind::Line;
	const bool line_eor = job.kind == graphics::BlitJobKind::LineEor;
	const bool c2p = job.kind == graphics::BlitJobKind::C2P;

	if (!masked && !copy && !clear && !fill && !or_blob && !logic && !line && !line_eor && !c2p) {
		return false;
	}

	if (fill) {
		// **Relleno de color del rectangulo** (AHRM "Extracting a Range of Columns" + WinUAE
		// `custom.cpp` `BLTADAT`): `D = A`, **A deshabilitada** (sin fetch) con `BLTADAT`
		// preload `$FFFF`/`$0000`; `AFWM`/`ALWM` recortan la primera/ultima palabra. Se cargan
		// primero `BLTCON0/1` y despues `BLTADAT` (el orden importa: cargar datos antes del shift
		// da resultados impredecibles). `job.minterm` = `$FF` (plano a 1) o `$00` (plano a 0).
		if (!wait_blitter()) {
			return false;
		}
		custom_base[custom_bltcon0_offset] =
			static_cast<u16>(blt_use_d | graphics::kBlitterMintermCopyA); // D = A, sin USEA
		custom_base[custom_bltcon1_offset] = 0u;
		custom_base[custom_bltafwm_offset] = job.fill.afwm;
		custom_base[custom_bltalwm_offset] = job.fill.alwm;
		custom_base[custom_bltadat_offset] = (job.minterm == 0xffu) ? 0xffffu : 0x0000u;
		custom_base[custom_bltdmod_offset] = static_cast<u16>(job.destination_modulo_bytes);
		write_custom_pointer(custom_bltdpt_offset, job.destination.words());
		custom_base[custom_bltsize_offset] =
			static_cast<u16>((job.height << 6u) | job.words_per_row);
		m_blt_common_valid = false;
		++m_blitter_starts;
		return true;
	}

	if (line || line_eor) {
		// Línea por Blitter (LINE) o EOR/ONEDOT: programan `BLTCON*`/`BLTxMOD` por su cuenta (racha
		// EOR con preámbulo propio), así que invalidan la caché de estado común.
		m_blt_common_valid = false;
		// Línea por Blitter (LINE) o EOR/ONEDOT sobre el plano del `destination`.
		eng::PlaneBytes pb {reinterpret_cast<eng::u8*>(job.destination.words()), 0u};
		eng::u8* d_base = job.line.base.words() != nullptr
					  ? reinterpret_cast<eng::u8*>(job.line.base.words())
					  : nullptr;
		if (line) {
			eor_open = false;
			if (!blitter_line(pb, job.line.row_bytes, job.line.x0, job.line.y0,
					  job.line.x1, job.line.y1)) {
				return false;
			}
		} else {
			// Racha EOR: fija los registros comunes UNA vez (no por arista×plano).
			if (!eor_open) {
				blitter_lines_eor_begin(job.line.row_bytes);
				eor_open = true;
			}
			LineEorParams p;
			if (blitter_line_eor_prepare(p, job.line.row_bytes, job.line.x0,
						     job.line.y0, job.line.x1, job.line.y1)) {
				blitter_line_eor_draw(p, reinterpret_cast<eng::u8*>(job.destination.words()),
						      d_base);
			}
		}
		return true;
	}
	eor_open = false; // cualquier otro job cierra la racha EOR

	if (c2p) {
		m_blt_common_valid = false; // C2P reprograma los registros por fases: invalida la caché
		// Chunky->planar por Blitter: 13 fases encadenadas (cada `step` espera al
		// Blitter). Es la via Blitter del seam `Rasterizer::c2p`.
		C2p4State st {};
		st.chunky = job.c2p.chunky;
		st.bytes = job.c2p.bytes;
		for (u8 pl = 0; pl < 4u; ++pl) {
			st.planes[pl] = job.c2p.planes +
					static_cast<u32>(pl) * job.c2p.plane_stride;
		}
		for (u8 ph = 0; ph < 13u; ++ph) {
			if (!c2p_4bpp_step(st)) {
				return false;
			}
		}
		return true;
	}

	const u32 source_plane_stride_words = job.source_plane_stride_bytes / sizeof(u16);
	const u32 destination_plane_stride_words = job.destination_plane_stride_bytes / sizeof(u16);
	// Todos los registros compartidos del Blitter deben fijarse solo cuando el job anterior
	// terminó: el blit en curso lee BLTCON/MOD durante su ejecución. Bartman hace WaitBlit antes
	// de programarlos; esperar únicamente antes de cambiar los punteros deja una carrera real.
	if (!wait_blitter()) {
		return false;
	}
	// Registros derivados de la intención por el **encoder único** (`blitter_job_from`): la
	// codificación (BLTCON/MOD/minterm) NO se duplica aquí. Se calcula **una vez por job** (no por
	// plano: los comunes no dependen del plano) y los PUNTEROS sí se re-apuntan por canal y plano.
	const graphics::BlitterJob b = graphics::blitter_job_from(job);
	// Estado común del Blitter (`BLTCON*`, ventanas de A y módulos) — **idéntico entre planos**.
	// Se escribe **por racha**: si un job consecutivo comparte estos registros con el anterior, se
	// omiten las escrituras a custom (el hardware ignora reescribir el mismo valor, pero cada
	// escritura cuesta bus). La caché se invalida cuando otro camino toca los registros (línea/EOR/
	// C2P), que programan su propia secuencia.
	const bool common_same =
		m_blt_common_valid && m_blt_con0 == b.bltcon0 && m_blt_con1 == b.bltcon1 &&
		m_blt_afwm == b.bltafwm && m_blt_alwm == b.bltalwm && m_blt_amod == b.bltamod &&
		m_blt_bmod == b.bltbmod && m_blt_cmod == b.bltcmod && m_blt_dmod == b.bltdmod;
	if (common_same) {
		// Racha de estado común: el job comparte `BLTCON*`/ventanas/módulos con el anterior, así
		// que se omiten sus ~8 escrituras a custom (solo se re-apuntan los punteros y `BLTSIZE`).
		++m_blt_common_hits;
	} else {
		custom_base[custom_bltcon0_offset] = b.bltcon0;		custom_base[custom_bltcon1_offset] = b.bltcon1;
		custom_base[custom_bltafwm_offset] = b.bltafwm;
		custom_base[custom_bltalwm_offset] = b.bltalwm;
		custom_base[custom_bltamod_offset] = static_cast<u16>(b.bltamod);
		custom_base[custom_bltbmod_offset] = static_cast<u16>(b.bltbmod);
		custom_base[custom_bltcmod_offset] = static_cast<u16>(b.bltcmod);
		custom_base[custom_bltdmod_offset] = static_cast<u16>(b.bltdmod);
		m_blt_con0 = b.bltcon0;
		m_blt_con1 = b.bltcon1;
		m_blt_afwm = b.bltafwm;
		m_blt_alwm = b.bltalwm;
		m_blt_amod = b.bltamod;
		m_blt_bmod = b.bltbmod;
		m_blt_cmod = b.bltcmod;
		m_blt_dmod = b.bltdmod;
		m_blt_common_valid = true;
	}
	const bool explicit_interleaved_clear = clear && job.interleaved && job.bitplane_count > 1u;
	const u32 destination_stride_words = explicit_interleaved_clear
		? job.destination_plane_stride_bytes / sizeof(u16)
		: destination_plane_stride_words;
	for (u8 plane = 0; plane < job.bitplane_count; ++plane) {
		// Solo hay que esperar entre planos: el plano 0 ya viene de la espera común de arriba
		// (antes de reprogramar los registros compartidos). Para `bitplane_count == 1`
		// (interleaved) el bucle no vuelve a esperar.
		if (plane > 0u && !wait_blitter()) {
			return false;
		}

		const u16* source_plane = job.source.words() + static_cast<u32>(plane) * source_plane_stride_words;
		u16* destination_plane = job.destination.words() + static_cast<u32>(plane) * destination_stride_words;

		const bool shifted_copy = !masked && !or_blob && !logic && job.source_shift != 0u;
		if (clear) {
			write_custom_pointer(custom_bltdpt_offset, destination_plane);
		} else if (masked) {
			// Cookie-cut `$CA`: A = máscara, B = imagen, C = D = destino. La máscara va en
			// `job.mask` y la imagen en `job.source` (o en `source_plane` si el layout es
			// planar con un stride por plano). Con plano intercalado hay un solo job
			// (`bitplane_count == 1`) y ambas mitades del par avanzan con `BLTxMOD`.
			write_custom_pointer(custom_bltapt_offset, job.mask.words());
			write_custom_pointer(custom_bltbpt_offset, source_plane);
			write_custom_pointer(custom_bltcpt_offset, destination_plane);
			write_custom_pointer(custom_bltdpt_offset, destination_plane);
		} else if (or_blob || logic) {
			// B = D = destino (el mismo puntero): aplica el minterm del job.
			write_custom_pointer(custom_bltapt_offset, source_plane);
			write_custom_pointer(custom_bltbpt_offset, destination_plane);
			write_custom_pointer(custom_bltdpt_offset, destination_plane);
		} else if (shifted_copy) {
			write_custom_pointer(custom_bltapt_offset, source_plane);
			write_custom_pointer(custom_bltdpt_offset, destination_plane);
		} else {
			write_custom_pointer(custom_bltcpt_offset, source_plane);
			write_custom_pointer(custom_bltdpt_offset, destination_plane);
		}
		custom_base[custom_bltsize_offset] = b.bltsize;
		++m_blitter_starts;
	}
	return true;
}

bool AmigaBackend::fill_triangles_blitter(const FlatTriangle* tris, u32 count,
					    eng::PlaneBytes dst, u8 planes, u16 row_bytes, u32 plane_bytes,
					    eng::MaskBuffer mask) {
	if (tris == nullptr || dst.data() == nullptr || mask.data() == nullptr || planes == 0u) {
		return false;
	}
	custom_base[custom_dmacon_offset] = static_cast<u16>(dma_setclr | dma_master | dma_blitter);

	for (u32 i = 0; i < count; ++i) {
		const FlatTriangle& t = tris[i];
		s16 xmin = t.x0, xmax = t.x0, ymin = t.y0, ymax = t.y0;
		if (t.x1 < xmin) xmin = t.x1;
		if (t.x1 > xmax) xmax = t.x1;
		if (t.x2 < xmin) xmin = t.x2;
		if (t.x2 > xmax) xmax = t.x2;
		if (t.y1 < ymin) ymin = t.y1;
		if (t.y1 > ymax) ymax = t.y1;
		if (t.y2 < ymin) ymin = t.y2;
		if (t.y2 > ymax) ymax = t.y2;
		if (xmax < 0 || ymax < 0 || xmin > 319 || ymin > 255) {
			return true;
		}
		if (xmin < 0) xmin = 0;
		if (ymin < 0) ymin = 0;
		if (xmax > 319) xmax = 319;
		if (ymax > 255) ymax = 255;

		const u16 wx0 = static_cast<u16>(xmin) & 0xfff0u;
		const u16 wx1 = static_cast<u16>(xmax) | 0x000fu;
		const u16 words = static_cast<u16>((static_cast<u16>(wx1 - wx0) + 16u) >> 4);
		const u16 h = static_cast<u16>(ymax - ymin + 1);

		// 1) mascara limpia, 2) contorno, 3) area fill, 4) cookie-cut a color.
		blit_clear_region(mask.data(), row_bytes, wx0, ymin, words, h);
		blit_line(mask.data(), row_bytes, t.x0, t.y0, t.x1, t.y1);
		blit_line(mask.data(), row_bytes, t.x1, t.y1, t.x2, t.y2);
		blit_line(mask.data(), row_bytes, t.x2, t.y2, t.x0, t.y0);
		blit_fill_region(mask.data(), row_bytes, wx0, ymin, words, h);
		for (u8 p = 0; p < planes; ++p) {
			blit_mask_to_plane(dst.data() + static_cast<u32>(p) * plane_bytes, row_bytes, mask.data(),
					   row_bytes, wx0, ymin, words, h, ((t.color >> p) & 1u) != 0u);
		}
	}
	return wait_blitter();
}

bool AmigaBackend::blitter_fill_polygon(eng::PlaneBytes dst, u8 planes, u16 row_bytes, u32 plane_bytes,
					  const s16* xs, const s16* ys, u8 n, u8 color, eng::MaskBuffer mask) {
	if (dst.data() == nullptr) {
		return false;
	}
	// Planos CONTIGUOS: la base del plano p está a `plane_bytes` del anterior y
	// cada fila avanza `row_bytes`. Se delega en la ruta con strides explícitos.
	return blitter_fill_polygon_strided(dst.data(), planes, plane_bytes, row_bytes, row_bytes,
					    320, 256, xs, ys, n, color, mask);
}

bool AmigaBackend::blitter_fill_polygon_strided(eng::u8* plane_base, u8 planes, u32 plane_stride,
						  u32 row_stride, u16 row_bytes, u16 bitmap_w, u16 bitmap_h,
						  const s16* xs, const s16* ys, u8 n, u8 color, eng::MaskBuffer mask) {
	if (plane_base == nullptr || mask.data() == nullptr || xs == nullptr || ys == nullptr ||
	    n < 3u || planes == 0u || bitmap_w == 0u || bitmap_h == 0u) {
		return false;
	}
	s16 xmin = xs[0], xmax = xs[0], ymin = ys[0], ymax = ys[0];
	for (u8 i = 1u; i < n; ++i) {
		if (xs[i] < xmin) xmin = xs[i];
		if (xs[i] > xmax) xmax = xs[i];
		if (ys[i] < ymin) ymin = ys[i];
		if (ys[i] > ymax) ymax = ys[i];
	}
	const s16 max_x = static_cast<s16>(bitmap_w - 1u);
	const s16 max_y = static_cast<s16>(bitmap_h - 1u);
	if (xmax < 0 || ymax < 0 || xmin > max_x || ymin > max_y) {
		return true;
	}
	if (xmin < 0) xmin = 0;
	if (ymin < 0) ymin = 0;
	if (xmax > max_x) xmax = max_x;
	if (ymax > max_y) ymax = max_y;
	const u16 wx0 = static_cast<u16>(xmin) & 0xfff0u;
	const u16 wx1 = static_cast<u16>(xmax) | 0x000fu;
	const u16 words = static_cast<u16>((static_cast<u16>(wx1 - wx0) + 16u) >> 4);
	const u16 h = static_cast<u16>(ymax - ymin + 1);

	custom_base[custom_dmacon_offset] = static_cast<u16>(dma_setclr | dma_master | dma_blitter);
	blit_clear_region(mask.data(), row_bytes, wx0, ymin, words, h);
	for (u8 i = 0u; i < n; ++i) {
		const u8 j = static_cast<u8>((static_cast<u8>(i) + 1u) % n);
		blit_line(mask.data(), row_bytes, xs[i], ys[i], xs[j], ys[j]);
	}
	blit_fill_region(mask.data(), row_bytes, wx0, ymin, words, h);
	for (u8 p = 0u; p < planes; ++p) {
		eng::u8* d = plane_base + static_cast<u32>(p) * plane_stride;
		blit_mask_to_plane(d, row_stride, mask.data(), row_bytes, wx0, ymin, words, h,
				   ((color >> p) & 1u) != 0u);
	}
	return wait_blitter();
}

bool AmigaBackend::blit_fill_from_mask(eng::MaskBytes mask, eng::PlaneBytes dst, u8 planes, u16 row_bytes,
					 u32 plane_bytes, s16 x, s16 y, u16 w, u16 h, u8 color) {
	if (mask.data() == nullptr || dst.data() == nullptr || planes == 0u || w == 0u || h == 0u) {
		return false;
	}
	s16 x0 = x, y0 = y;
	s16 x1 = static_cast<s16>(x + static_cast<s16>(w) - 1);
	s16 y1 = static_cast<s16>(y + static_cast<s16>(h) - 1);
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > 319) x1 = 319;
	if (y1 > 255) y1 = 255;
	if (x0 > x1 || y0 > y1) {
		return true;
	}
	const u16 wx0 = static_cast<u16>(x0) & 0xfff0u;
	const u16 wx1 = static_cast<u16>(x1) | 0x000fu;
	const u16 words = static_cast<u16>((static_cast<u16>(wx1 - wx0) + 16u) >> 4);
	const u16 hh = static_cast<u16>(y1 - y0 + 1);

	custom_base[custom_dmacon_offset] = static_cast<u16>(dma_setclr | dma_master | dma_blitter);
	for (u8 p = 0; p < planes; ++p) {
		blit_mask_to_plane(dst.data() + static_cast<u32>(p) * plane_bytes, row_bytes, mask.data(),
				   row_bytes, wx0, y0, words, hh, ((color >> p) & 1u) != 0u);
	}
	return wait_blitter();
}

bool AmigaBackend::blitter_line(eng::PlaneBytes plane, u16 row_bytes, s16 x0, s16 y0, s16 x1, s16 y1) {
	if (plane.data() == nullptr) {
		return false;
	}
	custom_base[custom_dmacon_offset] = static_cast<u16>(dma_setclr | dma_master | dma_blitter);

	wait_blitter();
	// Registros derivados por el ENCODER ÚNICO (`blitter_job_from`, modo LINE): la matemática
	// de octante/error ya no se duplica aquí.
	graphics::BlitJob job {};
	job.kind = graphics::BlitJobKind::Line;
	job.destination = graphics::BlitPtr::from_storage(reinterpret_cast<u16*>(plane.data()));
	job.line.x0 = x0;
	job.line.y0 = y0;
	job.line.x1 = x1;
	job.line.y1 = y1;
	job.line.row_bytes = row_bytes;
	const graphics::BlitterJob b = graphics::blitter_job_from(job);
	custom_base[custom_bltcon0_offset] = b.bltcon0;
	custom_base[custom_bltcon1_offset] = b.bltcon1;
	custom_base[custom_bltafwm_offset] = b.bltafwm;
	custom_base[custom_bltalwm_offset] = b.bltalwm;
	custom_base[custom_bltadat_offset] = b.bltadat;
	custom_base[custom_bltbdat_offset] = b.bltbdat;
	custom_base[custom_bltamod_offset] = static_cast<u16>(b.bltamod);
	custom_base[custom_bltbmod_offset] = static_cast<u16>(b.bltbmod);
	custom_base[custom_bltcmod_offset] = static_cast<u16>(b.bltcmod);
	custom_base[custom_bltdmod_offset] = static_cast<u16>(b.bltdmod);
	write_custom_pointer(custom_bltapt_offset, b.bltapt);
	write_custom_pointer(custom_bltcpt_offset, b.bltcpt);
	write_custom_pointer(custom_bltdpt_offset, b.bltdpt);
	custom_base[custom_bltsize_offset] = b.bltsize;
	return wait_blitter();
}

bool AmigaBackend::blitter_collide(eng::PlaneBytes a, eng::PlaneBytes b, eng::PlaneBytes scratch,
				     u8 planes, u16 row_bytes, u32 plane_bytes, u16 words, u16 rows) {
	if (a.data() == nullptr || b.data() == nullptr || scratch.data() == nullptr ||
	    planes == 0u || words == 0u || rows == 0u) {
		return false;
	}
	custom_base[custom_dmacon_offset] = static_cast<u16>(dma_setclr | dma_master | dma_blitter);
	// D = A & B (minterm $C0) por plano, a un scratch; luego escaneo CPU del scratch.
	custom_base[custom_bltcon0_offset] = static_cast<u16>(blt_use_a | blt_use_b | blt_use_d | 0x00c0);
	custom_base[custom_bltcon1_offset] = 0;
	custom_base[custom_bltafwm_offset] = 0xffff;
	custom_base[custom_bltalwm_offset] = 0xffff;
	custom_base[custom_bltamod_offset] = static_cast<u16>(row_bytes - words * 2u);
	custom_base[custom_bltbmod_offset] = static_cast<u16>(row_bytes - words * 2u);
	custom_base[custom_bltdmod_offset] = static_cast<u16>(row_bytes - words * 2u);
	for (u8 p = 0; p < planes; ++p) {
		if (!wait_blitter()) return false;
		write_custom_pointer(custom_bltapt_offset, a.data() + static_cast<u32>(p) * plane_bytes);
		write_custom_pointer(custom_bltbpt_offset, b.data() + static_cast<u32>(p) * plane_bytes);
		write_custom_pointer(custom_bltdpt_offset, scratch.data() + static_cast<u32>(p) * plane_bytes);
		custom_base[custom_bltsize_offset] =
			static_cast<u16>((static_cast<u16>(rows) << 6u) | words);
		++m_blitter_starts;
	}
	if (!wait_blitter()) return false;
	for (u8 p = 0; p < planes; ++p) {
		const eng::u8* s = scratch.data() + static_cast<u32>(p) * plane_bytes;
		for (u16 r = 0; r < rows; ++r) {
			const u16* w = reinterpret_cast<const u16*>(s + static_cast<u32>(r) * row_bytes);
			for (u16 i = 0; i < words; ++i) {
				if (w[i] != 0u) return true;
			}
		}
	}
	return false;
}

bool AmigaBackend::fill_polygons_by_plane(const graphics::PlanePolygon* faces, u32 n_faces,
					    eng::PlaneBytes dest, u16 row_bytes, u32 plane_bytes,
					    u8 planes, u16 width, u16 height) {
	if (faces == nullptr || n_faces == 0u || dest.data() == nullptr || planes == 0u ||
	    row_bytes == 0u || plane_bytes == 0u) {
		return false;
	}
	for (u8 p = 0; p < planes; ++p) {
		eng::PlaneBytes plane =
			dest.subspan(static_cast<u32>(p) * plane_bytes, plane_bytes);
		// 1) limpia el plano.
		blitter_clear(plane, 1u, row_bytes, plane_bytes, width, height);
		// 2) contorno XOR (ONEDOT) de las caras cuyo color tiene el bit `p` a 1; las
		//    aristas compartidas por dos caras del mismo bit se dibujan dos veces y el
		//    fill even-odd las cancela. Los registros comunes EOR se fijan UNA vez por
		//    plano (`blitter_lines_eor_begin`), no por arista.
		blitter_lines_eor_begin(row_bytes);
		for (u32 f = 0; f < n_faces; ++f) {
			const graphics::PlanePolygon& face = faces[f];
			if ((face.color & (1u << p)) == 0u || face.count < 3u) {
				continue;
			}
			for (u8 i = 0; i < face.count; ++i) {
				const u8 j = static_cast<u8>((i + 1u) % face.count);
				LineEorParams eor;
				if (blitter_line_eor_prepare(eor, row_bytes, face.xs[i], face.ys[i],
							     face.xs[j], face.ys[j])) {
					blitter_line_eor_draw(eor, plane.data(), plane.data());
				}
			}
		}
		// 3) area fill (FILL_XOR) del plano in situ (un fill por plano). Se acota al
		//    plano (`blitter_area_fill_rect`): `blitter_area_fill` barre 1024 filas
		//    (pensado para el bitmap multicapa contiguo de 116) y desbordaria el plano.
		blitter_area_fill_rect(plane, row_bytes, 0, 0, static_cast<u16>(width / 16u), height,
				       true);
	}
	return true;
}

bool AmigaBackend::blitter_line_eor(eng::PlaneBytes plane, u16 row_bytes, s16 x0, s16 y0, s16 x1, s16 y1,
				      eng::u8* d_base) {
	// Variante autónoma: begin + prepare/draw + wait final.
	blitter_lines_eor_begin(row_bytes);
	LineEorParams p;
	if (blitter_line_eor_prepare(p, row_bytes, x0, y0, x1, y1)) {
		blitter_line_eor_draw(p, plane.data(), d_base);
	}
	return wait_blitter();
}

void AmigaBackend::blitter_lines_eor_begin(u16 row_bytes) {
	// Setup común para una secuencia de líneas EOR (ONEDOT), equivalente al preludio
	// de `DrawObject` en flatshade-convex (`bltafwm/bltalwm=-1, bltadat=0x8000,
	// bltbdat=0xffff, bltcmod/bltdmod=WIDTH/8`). Se fija UNA vez por grupo de líneas:
	// cada escritura a registro custom cuesta ~57 ciclos con `cpu_cycle_exact`, así
	// que reescribirlos por arista×plano es desperdicio (el original no lo hace).
	// NO espera al Blitter: el primer `blitter_line_eor_draw` (que sí espera)
	// sincroniza con cualquier blit previo (el clear).
	custom_base[custom_dmacon_offset] = static_cast<u16>(dma_setclr | dma_master | dma_blitter);
	custom_base[custom_bltafwm_offset] = 0xffff;
	custom_base[custom_bltalwm_offset] = 0xffff;
	custom_base[custom_bltadat_offset] = 0x8000;
	custom_base[custom_bltbdat_offset] = 0xffff;
	custom_base[custom_bltcmod_offset] = row_bytes;
	custom_base[custom_bltdmod_offset] = row_bytes;
}

bool AmigaBackend::blitter_line_eor_prepare(LineEorParams& out, u16 row_bytes, s16 x0, s16 y0,
					      s16 x1, s16 y1) {
	// El original (`DrawObject` de flatshade-convex) DESCARTA las aristas
	// horizontales: no aportan contorno util y, dibujadas, meterian píxeles
	// extra en los vertices que descuadran el area fill (cruces impares).
	if (y0 == y1) {
		return false;
	}
	if (y0 > y1) {
		s16 t = x0; x0 = x1; x1 = t;
		t = y0; y0 = y1; y1 = t;
	}
	s16 dmax = static_cast<s16>(x1 - x0);
	s16 dmin = static_cast<s16>(y1 - y0);
	u16 bltcon1 = static_cast<u16>(blt_linemode | blt_onedot);
	if (dmax < 0) {
		dmax = static_cast<s16>(-dmax);
	}
	if (dmax >= dmin) {
		bltcon1 = static_cast<u16>(bltcon1 | (x0 >= x1 ? (blt_aul | blt_sud) : blt_sud));
	} else {
		if (x0 >= x1) {
			bltcon1 = static_cast<u16>(bltcon1 | blt_sul);
		}
		const s16 t = dmax; dmax = dmin; dmin = t;
	}
	out.row_offset = row_offset(y0, row_bytes) + ((static_cast<u32>(x0) >> 3) & ~1u);
	out.bltcon0 = static_cast<u16>(ror16(static_cast<u16>(x0 & 15), 4) | blt_line_eor);
	out.bltcon1 = static_cast<u16>(bltcon1 | ror16(static_cast<u16>(x0 & 15), 4));
	dmin = static_cast<s16>(dmin << 1);
	out.derr = static_cast<s16>(dmin - dmax);
	out.bltamod = static_cast<u16>(out.derr - dmax);
	out.bltbmod = static_cast<u16>(dmin);
	out.bltsize = static_cast<u16>((static_cast<u16>(dmax) << 6) + 66u);
	return true;
}

void AmigaBackend::blitter_line_eor_draw(const LineEorParams& p, eng::u8* plane_ptr, eng::u8* d_base) {
	u8* data = plane_ptr + p.row_offset;
	wait_blitter();
	custom_base[custom_bltcon0_offset] = p.bltcon0;
	custom_base[custom_bltcon1_offset] = p.bltcon1;
	custom_base[custom_bltamod_offset] = p.bltamod;
	custom_base[custom_bltbmod_offset] = p.bltbmod;
	write_custom_pointer(custom_bltapt_offset,
			     reinterpret_cast<void*>(static_cast<u32>(static_cast<s32>(p.derr))));
	write_custom_pointer(custom_bltcpt_offset, data);
	write_custom_pointer(custom_bltdpt_offset, d_base != nullptr ? d_base : data);
	custom_base[custom_bltsize_offset] = p.bltsize;
	// Sin esperar aqui: la siguiente operacion (o el swap de copperlist) sincroniza.
}

bool AmigaBackend::blitter_area_fill(eng::PlaneBytes dst, u8 planes, u16 row_bytes, u32 plane_bytes, u16 width, u16 height,
				       bool wait) {
	if (dst.data() == nullptr || planes == 0u || width < 16u || height == 0u) {
		return false;
	}
	custom_base[custom_dmacon_offset] = static_cast<u16>(dma_setclr | dma_master | dma_blitter);
	// Semilla = ultima palabra del bitmap (planos contiguos), descendente. El area
	// fill recorre `height` filas conmutando el bit de relleno en cada pixel del
	// contorno, de modo que rellena el interior (port de `BlitterFillArea`; el
	// original `flatshade-convex` usaba altura 0 por un bug, insuficiente).
	u8* bltpt = dst.data() + static_cast<u32>(plane_bytes) * planes - 2u;
	const u16 bltsize = static_cast<u16>((0u << 6) | (width >> 4));
	wait_blitter();
	write_custom_pointer(custom_bltapt_offset, bltpt);
	write_custom_pointer(custom_bltdpt_offset, bltpt);
	custom_base[custom_bltamod_offset] = 0;
	custom_base[custom_bltdmod_offset] = 0;
	custom_base[custom_bltcon0_offset] = static_cast<u16>(blt_use_a | blt_use_d | blt_minterm_copy_a);
	custom_base[custom_bltcon1_offset] = static_cast<u16>(blt_reverse | blt_fill_xor);
	custom_base[custom_bltafwm_offset] = 0xffff;
	custom_base[custom_bltalwm_offset] = 0xffff;
	custom_base[custom_bltsize_offset] = bltsize;
	return wait ? wait_blitter() : true;
}

bool AmigaBackend::blitter_clear(eng::PlaneBytes dst, u8 planes, u16 row_bytes, u32 plane_bytes, u16 w, u16 h,
				   bool wait) {
	if (dst.data() == nullptr || planes == 0u || w < 16u || h == 0u) {
		return false;
	}
	custom_base[custom_dmacon_offset] = static_cast<u16>(dma_setclr | dma_master | dma_blitter);
	const u16 words = static_cast<u16>(w / 16u);
	// Planos contiguos con filas contiguas (`plane_bytes == row_bytes*h`): UN solo
	// blit barre los `planes` planos de una pasada (como `BitmapClearFast` del
	// original: altura 0 = 1024 lineas si `planes*h` desborda los 10 bits). Si no,
	// se limpia plano a plano.
	if (static_cast<u32>(row_bytes) * h == plane_bytes) {
		blit_clear_region(dst.data(), row_bytes, 0, 0, words, static_cast<u16>(planes * h));
	} else {
		for (u8 p = 0; p < planes; ++p) {
			blit_clear_region(dst.data() + static_cast<u32>(p) * plane_bytes, row_bytes, 0, 0, words, h);
		}
	}
	return wait ? wait_blitter() : true;
}

bool AmigaBackend::blitter_memcpy(eng::Span<u8> dst, eng::Span<const u8> src, bool wait) {
	if (dst.data() == nullptr || src.data() == nullptr || dst.size() < src.size()) {
		return false;
	}
	const u32 bytes = static_cast<u32>(src.size()) & ~1u; // solo palabras completas
	if (bytes == 0u) {
		return true;
	}
	custom_base[custom_dmacon_offset] = static_cast<u16>(dma_setclr | dma_master | dma_blitter);
	// D = A (minterm $F0): BLTCON0 = USEA|USED|$F0; sin shift; módulos 0 (RAM lineal).
	custom_base[custom_bltcon0_offset] = static_cast<u16>(blt_use_a | blt_use_d | blt_minterm_copy_a);
	custom_base[custom_bltcon1_offset] = 0;
	custom_base[custom_bltafwm_offset] = 0xffff;
	custom_base[custom_bltalwm_offset] = 0xffff;
	custom_base[custom_bltamod_offset] = 0;
	custom_base[custom_bltbmod_offset] = 0;
	custom_base[custom_bltcmod_offset] = 0;
	custom_base[custom_bltdmod_offset] = 0;

	const u16* s = reinterpret_cast<const u16*>(src.data());
	u16* d = reinterpret_cast<u16*>(dst.data());
	u32 words = bytes / 2u;
	while (words > 0u) {
		if (!wait_blitter()) {
			return false;
		}
		u16 width;
		u16 height;
		if (words >= 64u) {
			u32 h = words / 64u;
			if (h > 1024u) {
				h = 1024u;
			}
			width = 64u;
			height = static_cast<u16>(h);
		} else {
			width = static_cast<u16>(words);
			height = 1u;
		}
		write_custom_pointer(custom_bltapt_offset, const_cast<u16*>(s));
		write_custom_pointer(custom_bltdpt_offset, d);
		custom_base[custom_bltsize_offset] = static_cast<u16>((height << 6) | width);
		const u32 copied = static_cast<u32>(width) * height;
		s += copied;
		d += copied;
		words -= copied;
	}
	return wait ? wait_blitter() : true;
}

void AmigaBackend::blitter_or_bobs_begin(u16 words, u16 height, s16 source_modulo,
					   s16 dest_modulo) {
	// Misma implementacion que el camino `inline` de coste cero (blob.hpp): una sola
	// fuente de verdad para la secuencia de registros.
	m_or_bob.begin(custom_base, words, height, source_modulo, dest_modulo);
}

void AmigaBackend::blitter_or_bobs_one(const void* source, void* dest, u8 shift) {
	m_or_bob.one(source, dest, shift);
}

bool AmigaBackend::blitter_or_bobs_end() {
	return m_or_bob.end();
}

void AmigaBackend::blitter_blob_run_begin(eng::amiga::BlobOp op, u16 words, u16 height, s16 amod,
					  s16 bmod, s16 cmod, s16 dmod) {
	m_blob_run.begin(custom_base, op, words, height, amod, bmod, cmod, dmod, g_blitter_service,
			 g_blitter_service_user);
	m_blt_common_valid = false; // el lote programó los registros comunes directamente
}

void AmigaBackend::blitter_blob_run_one(const void* a, const void* b, void* d, u8 shift) {
	m_blob_run.one(a, b, d, shift);
}

bool AmigaBackend::blitter_blob_run_end() {
	return m_blob_run.end();
}

bool AmigaBackend::blitter_strip_column(const void* src, void* dst, u16 words, s16 dmod,
					u16 planelines, u8 shift) {
	if (src == nullptr || dst == nullptr || words == 0u || planelines == 0u) {
		return false;
	}
	// Origen contiguo (BLTAMOD=0): avanza `words*2` bytes por planelínea. Destino en el anillo
	// interleaved: avanza `words*2 + dmod` (el dmod ya recorta el resto de la fila interleaved).
	const eng::u32 src_stride = static_cast<eng::u32>(words) * 2u;
	const eng::u32 dst_stride = src_stride + static_cast<eng::u16>(dmod);
	const eng::u8* s = static_cast<const eng::u8*>(src);
	eng::u8* d = static_cast<eng::u8*>(dst);
	eng::u16 done = 0u;
	while (done < planelines) {
		// H de BLTSIZE = 10 bits (max 1024 planelíneas): trocea la columna si hace falta.
		const eng::u16 h = static_cast<eng::u16>((planelines - done) > 1024u ? 1024u
									     : (planelines - done));
		blitter_blob_run_begin(eng::amiga::BlobOp::Opaque, words, h, 0, 0, 0, dmod);
		blitter_blob_run_one(s + static_cast<eng::u32>(done) * src_stride, nullptr,
				     d + static_cast<eng::u32>(done) * dst_stride, shift);
		if (!blitter_blob_run_end()) {
			return false;
		}
		done = static_cast<eng::u16>(done + h);
	}
	return true;
}

bool AmigaBackend::blitter_or_bobs(const OrBobEntry* entries, u32 count, u16 words, u16 height,
				     s16 source_modulo, s16 dest_modulo) {
	if (entries == nullptr || count == 0u || words == 0u || height == 0u) {
		return false;
	}
	blitter_or_bobs_begin(words, height, source_modulo, dest_modulo);
	for (u32 i = 0; i < count; ++i) {
		blitter_or_bobs_one(entries[i].source, entries[i].dest, entries[i].shift);
	}
	return blitter_or_bobs_end();
}

bool AmigaBackend::blitter_clear_rect(eng::PlaneBytes plane, u16 row_bytes, u16 wx0, s16 y0, u16 words, u16 rows,
					bool wait) {
	if (plane.data() == nullptr || words == 0u || rows == 0u) {
		return false;
	}
	custom_base[custom_dmacon_offset] = static_cast<u16>(dma_setclr | dma_master | dma_blitter);
	blit_clear_region(plane.data(), row_bytes, wx0, y0, words, rows);
	return wait ? wait_blitter() : true;
}

bool AmigaBackend::blitter_area_fill_rect(eng::PlaneBytes plane, u16 row_bytes, u16 wx0, s16 y0, u16 words, u16 rows,
					    bool wait) {
	if (plane.data() == nullptr || words == 0u || rows == 0u || words * 2u > row_bytes) {
		return false;
	}
	custom_base[custom_dmacon_offset] = static_cast<u16>(dma_setclr | dma_master | dma_blitter);
	const u16 mod = static_cast<u16>(row_bytes - words * 2u);
	eng::u8* seed = plane.data() + row_offset(static_cast<eng::s16>(y0 + rows - 1), row_bytes) +
			(wx0 >> 3) + (words - 1u) * 2u;
	wait_blitter();
	write_custom_pointer(custom_bltapt_offset, seed);
	write_custom_pointer(custom_bltdpt_offset, seed);
	custom_base[custom_bltamod_offset] = mod;
	custom_base[custom_bltdmod_offset] = mod;
	custom_base[custom_bltcon0_offset] = static_cast<u16>(blt_use_a | blt_use_d | blt_minterm_copy_a);
	custom_base[custom_bltcon1_offset] = static_cast<u16>(blt_reverse | blt_fill_xor);
	custom_base[custom_bltafwm_offset] = 0xffff;
	custom_base[custom_bltalwm_offset] = 0xffff;
	custom_base[custom_bltsize_offset] = static_cast<u16>((rows << 6) | words);
	return wait ? wait_blitter() : true;
}

bool AmigaBackend::blitter_fill_rect(eng::u8* plane_base, u8 planes, u32 plane_stride,
				       u32 row_stride, u16 row_bytes, u16 bitmap_w, u16 bitmap_h,
				       s32 x, s32 y, u16 w, u16 h, u8 color, bool wait) {
	if (plane_base == nullptr || planes == 0u || row_bytes == 0u || w == 0u || h == 0u) {
		return false;
	}
	(void)row_bytes; // el stride de fila real es `row_stride` (contiguo == row_bytes)
	// Recorta el rect a los limites del bitmap.
	if (x < 0) {
		const s32 d = -x;
		if (d >= static_cast<s32>(w)) return true;
		w = static_cast<u16>(w - d);
		x = 0;
	}
	if (y < 0) {
		const s32 d = -y;
		if (d >= static_cast<s32>(h)) return true;
		h = static_cast<u16>(h - d);
		y = 0;
	}
	if (x >= static_cast<s32>(bitmap_w) || y >= static_cast<s32>(bitmap_h)) {
		return true;
	}
	if (x + static_cast<s32>(w) > static_cast<s32>(bitmap_w)) {
		w = static_cast<u16>(static_cast<s32>(bitmap_w) - x);
	}
	if (y + static_cast<s32>(h) > static_cast<s32>(bitmap_h)) {
		h = static_cast<u16>(static_cast<s32>(bitmap_h) - y);
	}
	// Rango de palabras y mascaras de borde (para x/w no alineados a 16).
	const u16 wx0 = static_cast<u16>(x & ~15); // pixel x de la primera palabra
	const u16 wx1 = static_cast<u16>((x + static_cast<s32>(w) - 1) & ~15);
	const u16 words = static_cast<u16>(((wx1 - wx0) >> 4) + 1u);
	const u16 afwm = static_cast<u16>(0xffffu >> (x & 15));
	const u16 alwm = static_cast<u16>(0xffffu << (15 - ((x + static_cast<s32>(w) - 1) & 15)));
	constexpr u16 kMaxWords = 64u; // >= 20 palabras (320 px); cubre hasta 1024 px de ancho
	constexpr u16 kMaxRows = 256u; // altura de pantalla; rects mayores se rechazan
	if (words == 0u || words > kMaxWords || h > kMaxRows) {
		return false;
	}
	const u16 rstride = static_cast<u16>(row_stride);
	// Habilita el Blitter SIN borrar el resto del DMA (bitplane/copper/sprite): leer `DMACONR` y
	// reescribir el estado actual + el bit del Blitter. Escribir solo `Master|Blitter` con el
	// bit 15 (set/clear) borraria el DMA de bitplane/copper y dejaria la pantalla en blanco
	// hasta el siguiente VBlank (visible cuando el relleno se usa durante el frame).
	const u16 dma_cur = static_cast<u16>(custom_base[custom_dmaconr_offset] & 0x03ffu);
	custom_base[custom_dmacon_offset] =
		static_cast<u16>(dma_setclr | (dma_cur | dma_master | dma_blitter));
	// Un blit por plano con canal A constante (`BLTADAT`) y mascaras de borde `AFWM`/`ALWM`: el
	// hardware recorta la primera y ultima palabra, asi que NO hace falta guardar/restaurar los
	// bordes por CPU (la version anterior usaba dos arrays de 256 words y 2 esperas/plano).
	for (u8 p = 0u; p < planes; ++p) {
		eng::u8* plane = plane_base + static_cast<u32>(p) * plane_stride;
		const bool on = (color & (1u << p)) != 0u;
		const u16 fill = on ? 0xffffu : 0x0000u;
		blit_fill_region(plane, rstride, wx0, static_cast<eng::s16>(y), words, h, fill,
				 afwm, alwm);
	}
	return wait ? wait_blitter() : true;
}

bool AmigaBackend::blitter_busy() const {
	return (custom_base[custom_dmaconr_offset] & dmaconr_blitter_busy) != 0u;
}

bool AmigaBackend::wait_blitter() {
	return ::wait_blitter();
}

} // namespace eng::amiga
