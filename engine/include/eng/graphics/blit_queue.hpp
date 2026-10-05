#pragma once

/// \file blit_queue.hpp
/// **API de Blitter por intención** (estilo OpenGL): el desarrollador declara peticiones
/// (`fill`/`stamp`) y **no espera** a que se dibujen. Se **encolan** (una a una o en array) y el
/// **feeder** las ejecuta cuando el Blitter está libre, de a una (el Blitter tiene un solo juego de
/// registros). Solo `flush()`/`wait()` son puntos de bloqueo explícitos (como `glFinish`).
///
/// Diseño y alternativas: `docs/engine/architecture/BLITTER_INTENT_QUEUE.md`.
///
/// El **ejecutor** (quien sondea `BBUSY` y programa los registros) se inyecta por plantilla (un
/// `concept`, no `void*`+puntero a función — CODING_STYLE): en Amiga lo aporta el backend; en host,
/// un doble de prueba. Aquí **no se nombran registros**.

#include <eng/core/types/box.hpp>
#include <eng/core/types/domains.hpp>
#include <eng/core/types/memory_kind.hpp>
#include <eng/core/types/ptr.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/types/typed.hpp>
#include <eng/graphics/bitmap_view.hpp>
#include <eng/graphics/blit_job.hpp>
#include <eng/graphics/blitter_state.hpp>
#include <eng/graphics/intent_queue.hpp>
#include <eng/graphics/raster_intent.hpp>
#include <eng/res/asset_cache.hpp>

namespace eng::graphics {

/// Una petición de blit **por intención** (sin registros). `Fill` = limpiar un rectángulo
/// (`D = 0`); `Stamp` = OR de un asset sobre el destino (`D = A | D`), con `ASH` fino. La intención
/// es **una zona + un rectángulo**; los registros (`BLT*`) los deriva `blit_job_from()`.
///
/// **`src`/`dst` son `BitmapView` tipados**: zonas (base + geometría + layout) con el **banco Chip
/// en el tipo** (el Blitter es DMA y solo ve Chip RAM). Además el **tag** distingue el papel: el
/// destino es una zona de **plano de playfield** (`PlaneTag`) y el origen un **asset de BOB**
/// (`BobTag`), de modo que **intercambiarlos no compila**. Ver
/// `docs/engine/architecture/BLITTER_INTENT_QUEUE.md` §7.
struct BlitOp {
	enum class Kind : eng::u8 {
		Fill,        ///< borrar (`D = 0`)
		Stamp,       ///< OR de una imagen sin máscara (`D = A | B`, B = D = destino)
		MaskedStamp, ///< cookie-cut: `D = (A & B) | (~A & C)`, A = máscara, B = imagen, C = D
	};
	Kind kind = Kind::Fill;
	BitmapView<PlaneTag, MemoryKind::Chip> dst {}; ///< zona destino (bitmap intercalado en Chip)
	BitmapView<BobTag, MemoryKind::Chip> src {};   ///< `Stamp`/`MaskedStamp`: imagen (atlas Chip)
	BitmapView<BobTag, MemoryKind::Chip> mask {};  ///< `MaskedStamp`: máscara (plano aparte, Chip)
	eng::Box rect {};                              ///< rectángulo en la zona destino (píxeles)
	eng::u8 ashift = 0;                            ///< `Stamp`: desplazamiento fino 0..15
};

/// Los bits de `BLTCON0` (canales, `ASH` y minterms) viven en `blitter_state.hpp`
/// (`kBlitterUseX`/`kBlitterMinterm*`), referencia única compartida con el backend.

/// **Único sitio** que traduce la **intención** (`BlitOp`: zona + rect) al **trabajo de Blitter**
/// (`BlitJob`) — el tipo que describe el `FramePlan` y que el backend ya ejecuta. La intención
/// **no inventa un «juego de registros» paralelo**: reusa el trabajo canónico; el paso a registros
/// lo hace `blitter_job_from`. `Fill` → `ClearRect`; `Stamp` → `OrBlob`; `MaskedStamp` →
/// `MaskedBobCookieCut`. `Stamp`/`MaskedStamp` leen desde la esquina de su zona y desplazan con `ASH`.
[[nodiscard]] inline BlitJob blit_job_from(const BlitOp& op) noexcept {
  const bool inter = op.dst.interleaved();
	const eng::u8 planes = op.dst.plane_count;
	const eng::u32 row = inter ? static_cast<eng::u32>(op.dst.row_bytes) * planes
				   : op.dst.row_bytes;
	const eng::s16 wx = static_cast<eng::s16>(op.rect.x & ~15);
	const eng::s16 x0 = (wx < 0) ? 0 : wx;
	const eng::u16 words = static_cast<eng::u16>((op.rect.w + 15u) / 16u +
						     ((op.rect.x & 15) != 0 ? 1u : 0u));
	const eng::s32 dst_off = static_cast<eng::s32>(static_cast<eng::u32>(op.rect.y) * row +
						       (static_cast<eng::u32>(x0) >> 3u));

	BlitJob j {};
	j.kind = (op.kind == BlitOp::Kind::Fill)
			 ? BlitJobKind::ClearRect
			 : (op.kind == BlitOp::Kind::MaskedStamp ? BlitJobKind::MaskedBobCookieCut
								 : BlitJobKind::OrBlob);
	j.words_per_row = words;
	j.height = inter ? static_cast<eng::u16>(op.rect.h * planes) : op.rect.h;
	j.bitplane_count = inter ? 1u : planes;
	j.source_shift = static_cast<eng::u8>(op.rect.x & 15);
	j.interleaved = inter;
	j.destination = BlitPtr {op.dst.planes, dst_off};
	j.destination_modulo_bytes =
		static_cast<eng::s16>(op.dst.row_bytes - static_cast<eng::u32>(words) * 2u);
	j.destination_plane_stride_bytes = inter ? 0u : op.dst.plane_pointer_step();
	j.minterm = (op.kind == BlitOp::Kind::Fill)
			    ? 0x00u
			    : (op.kind == BlitOp::Kind::MaskedStamp ? 0xcau : 0xfcu);
	if (op.kind != BlitOp::Kind::Fill) {
		j.source = BlitPtr {op.src.planes};
		j.source_modulo_bytes =
			static_cast<eng::s16>(op.src.row_bytes - static_cast<eng::u32>(words) * 2u);
		j.source_plane_stride_bytes =
			inter ? 0u : static_cast<eng::u32>(op.rect.h) * op.src.row_bytes;
	}
	if (op.kind == BlitOp::Kind::MaskedStamp) {
		j.mask = BlitPtr {op.mask.planes};
	}
	return j;
}

/// **Único encoder** de los registros del Blitter: traduce un **trabajo** (`BlitJob`) a los
/// **registros** (`BlitterJob`) que consumen el Copper (`Scheduler::emit_blitter_job`) y el backend.
/// Los dos ejecutores (CPU y Copper) comparten esta codificación; referencia de bits:
/// `hardware/blit.h` (USEx = canales, ASH = bits 12-15, minterm = bits 7-0).
///
/// Cubre los tipos planos **y `Line`** (modo LINE, octante/error). **`LineEor`** comparte el
/// `BLTCON0` de EOR, pero su `FILL_XOR`/base son un **preámbulo de lote** del backend (estado
/// `eor_open`, una vez por racha), no un registro por trabajo → se queda en el backend. **`C2P`**
/// **no** es un `BlitterJob` (13 fases encadenadas) → camino aparte.
[[nodiscard]] inline BlitterJob blitter_job_from(const BlitJob& j) noexcept {
	const eng::u16 shift = static_cast<eng::u16>(j.source_shift);
	const bool clear = j.kind == BlitJobKind::ClearRect;
	const bool masked = j.kind == BlitJobKind::MaskedBobCookieCut ||
			    j.kind == BlitJobKind::MaskedBlobNoSave;
	// Módulo de A: si el trabajo declara un ancho de fila de origen propio, se deriva; si no, el
	// clásico (fuente compacta). Es la MISMA regla que usa el backend (fuente «apretada» vs ancha).
	const eng::s16 src_mod =
		(j.source_words_per_row != 0u)
			? static_cast<eng::s16>((static_cast<eng::s16>(j.source_words_per_row) -
						 static_cast<eng::s16>(j.words_per_row)) * 2)
			: j.source_modulo_bytes;
	const bool copy = j.kind == BlitJobKind::CopyRect || j.kind == BlitJobKind::RestoreRect ||
			  j.kind == BlitJobKind::TileBlockCopy;
	BlitterJob b {};
	if (copy) {
		// Copia: sin desplazamiento el barrel shifter no actúa y la fuente va por C (`D = C`,
		// `$AA`); con desplazamiento va por A (`D = A`, `$F0`) y `BSH`/`DESC` en `BLTCON1`.
		const bool shifted = shift != 0u;
		b.bltcon0 = shifted ? static_cast<eng::u16>((shift << kBlitterAshift) | kBlitterUseA |
							    kBlitterUseD | kBlitterMintermCopyA)
				    : static_cast<eng::u16>(kBlitterUseC | kBlitterUseD |
							    kBlitterMintermCopyC);
		b.bltcon1 = static_cast<eng::u16>((shifted ? (shift << kBlitterAshift) : 0u) |
						  (j.descending ? kBlitterDesc : 0u));
		b.bltalwm = shifted ? static_cast<eng::u16>(0xffffu << shift) : 0xffffu;
		b.bltamod = shifted ? src_mod : 0;
		b.bltbmod = 0;
		b.bltcmod = src_mod;
		b.bltdmod = j.destination_modulo_bytes;
		b.bltapt = shifted ? j.source.words() : nullptr;
		b.bltcpt = shifted ? nullptr : j.source.words();
		b.bltdpt = j.destination.words();
		// BLTSIZE codifica 1024 filas como height=0; el job conserva la altura lógica.
		const eng::u16 encoded_height = j.interleaved && j.height == 1024u ? 0u : j.height;
		b.bltsize = static_cast<eng::u16>((encoded_height << 6u) | j.words_per_row);
		return b;
	}
	if (clear && j.interleaved && j.bitplane_count > 1u) {
		// Clear job with explicit per-plane stride (e.g. Screen::clear_box).
		b.bltcon0 = static_cast<eng::u16>(kBlitterUseD | j.minterm);
		b.bltcon1 = 0u;
		b.bltafwm = 0xffffu;
		b.bltalwm = 0xffffu;
		b.bltdmod = j.destination_modulo_bytes;
		b.bltdpt = j.destination.words();
		b.bltsize = static_cast<eng::u16>((j.height << 6u) | j.words_per_row);
		return b;
	}
	if (j.kind == BlitJobKind::Line || j.kind == BlitJobKind::LineEor) {
		// Modo LÍNEA: A/B aportan pendiente/error (`BLTADAT=0x8000`), C=D=plano. El algoritmo
		// (octante/`dmax`/`dmin`/error) es el de `blitter_line` (AHRM 6, modo LINE).
		eng::s16 x0 = j.line.x0, y0 = j.line.y0, x1 = j.line.x1, y1 = j.line.y1;
		if (y0 > y1) {
			const eng::s16 tx = x0; x0 = x1; x1 = tx;
			const eng::s16 ty = y0; y0 = y1; y1 = ty;
		}
		eng::s16 dmax = static_cast<eng::s16>(x1 - x0);
		eng::s16 dmin = static_cast<eng::s16>(y1 - y0);
		eng::u16 con1 = kBlitterLineMode;
		if (dmax < 0) dmax = static_cast<eng::s16>(-dmax);
		if (dmax >= dmin) {
			con1 = static_cast<eng::u16>(con1 | (x0 >= x1 ? (kBlitterAul | kBlitterSud)
								      : kBlitterSud));
		} else {
			if (x0 >= x1) con1 = static_cast<eng::u16>(con1 | kBlitterSul);
			const eng::s16 t = dmax; dmax = dmin; dmin = t;
		}
		eng::u8* data = reinterpret_cast<eng::u8*>(j.destination.words()) +
				static_cast<eng::u32>(y0) * j.line.row_bytes +
				(static_cast<eng::u32>(x0) >> 3);
		data = reinterpret_cast<eng::u8*>(reinterpret_cast<eng::usize>(data) & ~eng::usize{1});
		dmin = static_cast<eng::s16>(dmin << 1);
		eng::s16 derr = static_cast<eng::s16>(dmin - dmax);
		if (derr < 0) con1 = static_cast<eng::u16>(con1 | kBlitterSignFlag);
		const eng::u16 lo = static_cast<eng::u16>(static_cast<eng::u16>(x0) & 15u);
		const eng::u16 ror = static_cast<eng::u16>((lo >> 4u) | (lo << 12u));
		b.bltcon0 = static_cast<eng::u16>(
			ror | (j.kind == BlitJobKind::LineEor ? kBlitterLineEor : kBlitterLineOr));
		b.bltcon1 = static_cast<eng::u16>(con1 | ror);
		b.bltadat = 0x8000u;
		b.bltbdat = 0xffffu;
		b.bltamod = static_cast<eng::s16>(derr - dmax);
		b.bltbmod = dmin;
		b.bltapt = reinterpret_cast<const void*>(static_cast<eng::s32>(derr)); // error como valor
		b.bltcpt = data;
		b.bltdpt = data;
		b.bltcmod = j.line.row_bytes;
		b.bltdmod = j.line.row_bytes;
		b.bltsize = static_cast<eng::u16>((static_cast<eng::u16>(dmax) << 6) + 66u);
		return b;
	}
	const eng::u16 ash = static_cast<eng::u16>(shift << kBlitterAshift);
	const eng::u16 bsh = static_cast<eng::u16>(shift << kBlitterBshift);
	const eng::u16 shift_bits = masked ? static_cast<eng::u16>(ash | bsh) : ash;
	eng::u16 control = clear ? static_cast<eng::u16>(kBlitterUseD | j.minterm)
				 : static_cast<eng::u16>(kBlitterUseA | kBlitterUseB | kBlitterUseD |
							  j.minterm | shift_bits);
	if (masked) {
		control = static_cast<eng::u16>(control | kBlitterUseC);
	}
	b.bltcon0 = control;
	b.bltcon1 = masked ? bsh : 0u;
	b.bltalwm = (clear || masked) ? 0xffffu : static_cast<eng::u16>(0xffffu << shift);
	b.bltamod = src_mod;
	b.bltbmod = masked ? src_mod : (clear ? 0 : j.destination_modulo_bytes);
	b.bltcmod = masked ? j.destination_modulo_bytes : src_mod;
	b.bltdmod = j.destination_modulo_bytes;
	b.bltapt = masked ? j.mask.words() : j.source.words();
	b.bltbpt = masked ? j.source.words() : j.destination.words();
	b.bltcpt = masked ? j.destination.words() : nullptr;
	b.bltdpt = j.destination.words();
	b.bltsize = static_cast<eng::u16>((j.height << 6u) | j.words_per_row);
	return b;
}

/// Ejecutor de Blitter **por Copper** (Técnica A): la intención (`BlitOp`) se traduce a un
/// `BlitterJob` que el **Copper** emite contra los registros (`Scheduler::emit_blitter_job`), en vez
/// de programarlos la CPU. `ready()` = siempre `true` (lo arranca el Copper en la línea) y el
/// punto de dependencia es el raster. `Sched` = `copper::Scheduler` (o un doble de test).
template <class Sched>
class CopperBlitterExecutor {
public:
	constexpr CopperBlitterExecutor(Sched& sched, eng::u16 top) noexcept
		: m_sched(sched), m_top(top) {}
	[[nodiscard]] constexpr bool ready() const noexcept { return true; }
	void run(const BlitOp& op) noexcept {
		m_sched.emit_blitter_job(m_top, blitter_job_from(blit_job_from(op)));
	}

private:
	Sched& m_sched;
	eng::u16 m_top;
};

/// Ejecutor cuyo destino es un **sumidero de trabajo** (`sink.add(BlitJob)`), no el hardware: la
/// intención (`BlitOp`) se convierte con `blit_job_from` y se **añade al plan**
/// (`FramePlan::add`) en vez de programar registros aquí. `ready()` = siempre `true` (no
/// toca el Blitter); el presupuesto/ejecución los gobierna el plan. Es la forma en que la
/// `BlitQueue` es un **front-end** del `FramePlan` (un solo dueño de la ejecución).
template <class Sink>
class PlanExecutor {
public:
	constexpr explicit PlanExecutor(Sink& sink) noexcept : m_sink(sink) {}
	[[nodiscard]] constexpr bool ready() const noexcept { return true; }
	bool run(const BlitOp& op) noexcept { return m_sink.add(blit_job_from(op)); }

private:
	Sink& m_sink;
};

/// **Cola de intención de blit**: una `IntentQueue` con `Item = BlitOp` y los atajos de intención
/// (`fill`/`stamp`/`masked_stamp`/`all`). **No** bloquea al declarar; `flush` avanza sin esperar;
/// `wait` es el único bloqueo. El ejecutor es **el mismo** contrato que el resto de colas
/// (`ready()`/`run(item)`): una sola forma de encolar, para blit y no-blit.
template <eng::u16 N, class Executor>
	requires eng::QueueExecutor<Executor, BlitOp>
class BlitQueue : public eng::IntentQueue<N, BlitOp, Executor, eng::NoDone> {
public:
	/// **Intención**: rellenar (`D = 0`) un rectángulo del destino (una petición).
	void fill(BitmapView<PlaneTag, MemoryKind::Chip> dst, eng::Box rect) noexcept {
		this->enqueue(BlitOp {BlitOp::Kind::Fill, dst, {}, {}, rect, 0});
	}

	/// **Intención**: OR de un asset sobre el destino (fino con `ashift`), una petición.
	void stamp(BitmapView<BobTag, MemoryKind::Chip> src, BitmapView<PlaneTag, MemoryKind::Chip> dst,
		   eng::Box rect, eng::u8 ashift = 0) noexcept {
		this->enqueue(BlitOp {BlitOp::Kind::Stamp, dst, src, {}, rect, ashift});
	}

	/// Encola una vista usando una lease Chip mantenida por el llamador hasta completar el plan.
	void stamp(BitmapView<BobTag, MemoryKind::Chip> src, BitmapView<PlaneTag, MemoryKind::Chip> dst,
		   eng::Box rect, const eng::res::AssetDmaLease& lease, eng::u8 ashift = 0) noexcept {
		if (lease.valid() && lease.view().kind == MemoryKind::Chip &&
		    lease.view().data.data() == src.planes.data()) {
			this->enqueue(BlitOp {BlitOp::Kind::Stamp, dst, src, {}, rect, ashift});
		}
	}

	/// **Intención**: cookie-cut (`D = (A & B) | (~A & C)`), una petición. `mask` es el plano de
	/// máscara (`1` = tomar la imagen `src`, `0` = conservar el fondo).
	void masked_stamp(BitmapView<BobTag, MemoryKind::Chip> src,
			  BitmapView<BobTag, MemoryKind::Chip> mask,
			  BitmapView<PlaneTag, MemoryKind::Chip> dst, eng::Box rect,
			  eng::u8 ashift = 0) noexcept {
		this->enqueue(BlitOp {BlitOp::Kind::MaskedStamp, dst, src, mask, rect, ashift});
	}

	/// **Intención en array de golpe**: encola muchas de una vez.
	void all(eng::Span<const BlitOp> ops) noexcept {
		for (const BlitOp& op : ops) {
			this->enqueue(op);
		}
	}

	/// Alias de `flush` (avanza sin esperar).
	void pump() noexcept { this->flush(); }
};

} // namespace eng::graphics
