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

#include <eng/core/types/domains.hpp>
#include <eng/core/types/memory_kind.hpp>
#include <eng/core/types/ptr.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/types/typed.hpp>
#include <eng/graphics/bitmap_view.hpp>
#include <eng/graphics/blit_job.hpp>
#include <eng/graphics/blitter_state.hpp>
#include <eng/graphics/raster_intent.hpp>

namespace eng::graphics {

/// Rectángulo de la intención, en píxeles respecto de la esquina de la zona destino.
struct BlitRect {
	eng::s16 x = 0;
	eng::s16 y = 0;
	eng::u16 w = 0;
	eng::u16 h = 0;
};

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
	BlitRect rect {};                              ///< rectángulo en la zona destino (píxeles)
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
	j.destination = BlitDest {op.dst.planes, dst_off};
	j.destination_modulo_bytes =
		static_cast<eng::s16>(op.dst.row_bytes - static_cast<eng::u32>(words) * 2u);
	j.destination_plane_stride_bytes = inter ? 0u : op.dst.plane_pointer_step();
	j.minterm = (op.kind == BlitOp::Kind::Fill)
			    ? 0x00u
			    : (op.kind == BlitOp::Kind::MaskedStamp ? 0xcau : 0xfcu);
	if (op.kind != BlitOp::Kind::Fill) {
		j.source = BlitSource {op.src.planes};
		j.source_modulo_bytes =
			static_cast<eng::s16>(op.src.row_bytes - static_cast<eng::u32>(words) * 2u);
		j.source_plane_stride_bytes =
			inter ? 0u : static_cast<eng::u32>(op.rect.h) * op.src.row_bytes;
	}
	if (op.kind == BlitOp::Kind::MaskedStamp) {
		j.mask = BlitSource {op.mask.planes};
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
		b.bltapt = shifted ? j.source.words : nullptr;
		b.bltcpt = shifted ? nullptr : j.source.words;
		b.bltdpt = j.destination.words;
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
		eng::u8* data = reinterpret_cast<eng::u8*>(j.destination.words) +
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
	b.bltcon0 = static_cast<eng::u16>(
		(clear ? (kBlitterUseD | j.minterm)
		       : (shift << kBlitterAshift) |
				(masked ? (kBlitterUseA | kBlitterUseB | kBlitterUseC | kBlitterUseD |
					   j.minterm)
					: (kBlitterUseA | kBlitterUseB | kBlitterUseD | j.minterm))));
	b.bltcon1 = masked ? static_cast<eng::u16>(shift << kBlitterAshift) : 0u;
	b.bltalwm = (clear || masked) ? 0xffffu : static_cast<eng::u16>(0xffffu << shift);
	b.bltamod = src_mod;
	b.bltbmod = masked ? src_mod : (clear ? 0 : j.destination_modulo_bytes);
	b.bltcmod = masked ? j.destination_modulo_bytes : src_mod;
	b.bltdmod = j.destination_modulo_bytes;
	b.bltapt = masked ? j.mask.words : j.source.words;
	b.bltbpt = masked ? j.source.words : j.destination.words;
	b.bltcpt = masked ? j.destination.words : nullptr;
	b.bltdpt = j.destination.words;
	b.bltsize = static_cast<eng::u16>((j.height << 6u) | j.words_per_row);
	return b;
}

/// Ejecutor de Blitter **por Copper** (Técnica A): la intención (`BlitOp`) se traduce a un
/// `BlitterJob` que el **Copper** emite contra los registros (`Scheduler::emit_blitter_job`), en vez
/// de programarlos la CPU. `blitter_free()` = siempre `true` (lo arranca el Copper en la línea) y el
/// punto de dependencia es el raster. `Sched` = `copper::Scheduler` (o un doble de test).
template <class Sched>
class CopperBlitterExecutor {
public:
	constexpr CopperBlitterExecutor(Sched& sched, eng::u16 top) noexcept
		: m_sched(sched), m_top(top) {}
	[[nodiscard]] constexpr bool blitter_free() const noexcept { return true; }
	void submit(const BlitOp& op) noexcept {
		m_sched.emit_blitter_job(m_top, blitter_job_from(blit_job_from(op)));
	}

private:
	Sched& m_sched;
	eng::u16 m_top;
};

/// Ejecutor cuyo destino es un **sumidero de trabajo** (`sink.add(BlitJob)`), no el hardware: la
/// intención (`BlitOp`) se convierte con `blit_job_from` y se **añade al plan**
/// (`FramePlan::add`) en vez de programar registros aquí. `blitter_free()` = siempre `true` (no
/// toca el Blitter); el presupuesto/ejecución los gobierna el plan. Es la forma en que la
/// `BlitQueue` es un **front-end** del `FramePlan` (un solo dueño de la ejecución).
template <class Sink>
class SinkBlitExecutor {
public:
	constexpr explicit SinkBlitExecutor(Sink& sink) noexcept : m_sink(sink) {}
	[[nodiscard]] constexpr bool blitter_free() const noexcept { return true; }
	bool submit(const BlitOp& op) noexcept { return m_sink.add(blit_job_from(op)); }

private:
	Sink& m_sink;
};

/// Contrato del **ejecutor**: sabe si el Blitter está libre (BBUSY) y programa una petición.
template <class E>
concept BlitExecutor = requires(E& e, const BlitOp& op) {
	{ e.blitter_free() }; // devuelve algo convertible a bool
	e.submit(op);
};

/// **Cola FIFO de peticiones** de capacidad fija `N` (sin heap). El desarrollador encola y sigue;
/// `pump()` avanza mientras el Blitter admite trabajo; `wait()` vacía (dependencia explícita).
template <eng::u16 N, BlitExecutor Executor>
class BlitQueue {
	static_assert((N & (N - 1u)) == 0u, "BlitQueue: N potencia de dos");

public:
	/// Liga el ejecutor (el backend Amiga, o un doble de host). No es propietario.
	void bind(Executor& executor) noexcept { m_exec = executor; }

	/// Encola una petición ya formada.
	void enqueue(const BlitOp& op) noexcept {
		// Si está llena, drena un poco antes de encolar (no bloquea si el Blitter va al día).
		if (full()) {
			pump();
		}
		if (full()) {
			wait(); // último recurso: la cola es de capacidad fija
		}
		m_ops[m_head] = op;
		m_head = (m_head + 1u) & (N - 1u);
	}

	/// **Intención**: rellenar (`D = 0`) un rectángulo del destino (una petición).
	void fill(BitmapView<PlaneTag, MemoryKind::Chip> dst, BlitRect rect) noexcept {
		enqueue(BlitOp {BlitOp::Kind::Fill, dst, {}, {}, rect, 0});
	}

	/// **Intención**: OR de un asset sobre el destino (fino con `ashift`), una petición.
	void stamp(BitmapView<BobTag, MemoryKind::Chip> src, BitmapView<PlaneTag, MemoryKind::Chip> dst,
		   BlitRect rect, eng::u8 ashift = 0) noexcept {
		enqueue(BlitOp {BlitOp::Kind::Stamp, dst, src, {}, rect, ashift});
	}

	/// **Intención**: cookie-cut (`D = (A & B) | (~A & C)`), una petición. `mask` es el plano de
	/// máscara (`1` = tomar la imagen `src`, `0` = conservar el fondo).
	void masked_stamp(BitmapView<BobTag, MemoryKind::Chip> src,
			  BitmapView<BobTag, MemoryKind::Chip> mask,
			  BitmapView<PlaneTag, MemoryKind::Chip> dst, BlitRect rect,
			  eng::u8 ashift = 0) noexcept {
		enqueue(BlitOp {BlitOp::Kind::MaskedStamp, dst, src, mask, rect, ashift});
	}

	/// **Intención en array de golpe**: encola muchas de una vez.
	void all(eng::Span<const BlitOp> ops) noexcept {
		for (const BlitOp& op : ops) {
			enqueue(op);
		}
	}

	/// Avanza la cola **sin esperar**: programa mientras el Blitter esté libre.
	void pump() noexcept {
		while (!empty() && m_exec->blitter_free()) {
			m_exec->submit(m_ops[m_tail]);
			m_tail = (m_tail + 1u) & (N - 1u);
		}
	}

	/// Garantiza que se está drenando (un intento de avance; NO vacía).
	void flush() noexcept { pump(); }

	/// **Punto de dependencia**: vacía la cola (espera a que el Blitter admita cada petición).
	void wait() noexcept {
		while (!empty()) {
			if (!m_exec->blitter_free()) {
				continue; // el Blitter está ocupado: se reintenta (poll)
			}
			m_exec->submit(m_ops[m_tail]);
			m_tail = (m_tail + 1u) & (N - 1u);
		}
	}

	/// ¿Cola vacía (nada pendiente de ejecutar)?
	[[nodiscard]] bool empty() const noexcept { return m_head == m_tail; }
	/// ¿Cola llena (una entrada libre menos: el anillo reserva un hueco)?
	[[nodiscard]] bool full() const noexcept {
		return ((m_head + 1u) & (N - 1u)) == m_tail;
	}

private:
	BlitOp m_ops[N] {};
	eng::u16 m_head = 0;
	eng::u16 m_tail = 0;
	eng::Ref<Executor> m_exec {}; ///< ejecutor (no propietario)
};

} // namespace eng::graphics
