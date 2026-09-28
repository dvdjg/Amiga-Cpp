// Test host de eng::graphics::BlitQueue (API de blitter por intencion: cola FIFO + feeder poll).
// El Blitter real se dobla con un FakeBlitter: `blitter_free()` sondea (y "avanza el tiempo" un
// paso por sondeo) y `submit()` programa (queda ocupado unos sondeos). Sin hardware.
#define ENG_SCALAR_RETRO16
#include <eng/graphics/blit_queue.hpp>
#include <eng/graphics/bob.hpp>

#include <cstdio>

using eng::graphics::BlitOp;
using eng::ChipView;

// Test NEGATIVO de tipos (el compilador caza el error, §232): un `ChipView` NO se puede construir
// desde un puntero crudo ni desde una vista agnostica -> pasar pila/Fast al Blitter no compila.
using ChipPlane = eng::ChipView<eng::PlaneTag>;
using PlaneByteView = eng::ByteView<eng::PlaneTag>;

// Test NEGATIVO de tipos (fijo, §232): un `ChipView` (banco Chip en el tipo) NO se puede construir
// desde un puntero crudo, una `Span<u8>` agnostica ni una vista de dominio -> pasar pila/Fast al
// Blitter no compila. Solo nace de una fuente Chip (Block/Bitmap/ChipStorage) o del `from_storage`
// del backend/test.
template <class To, class From>
concept ConstructibleFrom = requires(From f) { To(f); };
static_assert(!ConstructibleFrom<ChipPlane, eng::u8*>);
static_assert(!ConstructibleFrom<ChipPlane, eng::Span<eng::u8>>);
static_assert(!ConstructibleFrom<ChipPlane, PlaneByteView>);

// El **tag** de la zona distingue el papel: destino = plano de playfield (`PlaneTag`), origen =
// asset de BOB (`BobTag`). Intercambiarlos en un `BlitOp` NO compila.
using DstZone = eng::graphics::BitmapView<eng::PlaneTag, eng::MemoryKind::Chip>;
using SrcZone = eng::graphics::BitmapView<eng::BobTag, eng::MemoryKind::Chip>;
static_assert(!ConstructibleFrom<DstZone, SrcZone>);
static_assert(!ConstructibleFrom<SrcZone, DstZone>);

static int failures = 0;
static void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

struct FakeBlitter {
	static constexpr int kBusyPolls = 3;
	int busy = 0;
	int submitted = 0;
	eng::u8 kinds[16] {};
	eng::u8 last_ashift = 0xff;
	// Sondea BBUSY: cada llamada "pasa un sondeo"; ocupado durante kBusyPolls.
	bool blitter_free() {
		if (busy > 0) {
			--busy;
		}
		return busy == 0;
	}
	void submit(const BlitOp& op) {
		if (submitted < 16) {
			kinds[submitted] = static_cast<eng::u8>(op.kind);
		}
		last_ashift = op.ashift;
		++submitted;
		busy = kBusyPolls;
	}
};

using Q = eng::graphics::BlitQueue<8, FakeBlitter>;

// Doble del Scheduler de Copper: registra el `BlitterJob` emitido (sin hardware).
struct FakeCopper {
	eng::u16 last_top = 0xffffu;
	int emitted = 0;
	eng::graphics::BlitterJob last {};
	void emit_blitter_job(eng::u16 top, const eng::graphics::BlitterJob& j) {
		last_top = top;
		last = j;
		++emitted;
	}
};

int main() {
	static eng::u8 dst_buf[8] {};
	static eng::u8 src_buf[8] {};
	// En host no hay Chip RAM: para probar la COLA (no el medio) se usa `from_storage`, el escape
	// documentado del test (§232). En objetivo, la zona viene de un `Block`/`Bitmap` Chip.
	const DstZone dst {
		ChipPlane {eng::Address<eng::MemoryKind::Chip>::from_storage(dst_buf), 8},
		16u, 8u, 2u, 1u, eng::graphics::PlaneLayout::Interleaved};
	const SrcZone src {
		eng::ChipView<eng::BobTag> {eng::Address<eng::MemoryKind::Chip>::from_storage(src_buf), 8},
		16u, 8u, 2u, 1u, eng::graphics::PlaneLayout::Interleaved};
	FakeBlitter blitter;
	Q q;
	q.bind(blitter);

	// 1) Encolar NO ejecuta: es asincrono.
	q.fill(dst, {0, 0, 256u, 768u});
	q.stamp(src, dst, {5, 0, 32u, 96u}, 5);
	check(!q.empty() && blitter.submitted == 0, "encolar no ejecuta (asincrono)");

	// 2) pump avanza solo mientras el Blitter esta libre (1 submit y queda ocupado).
	q.pump();
	check(blitter.submitted == 1 && blitter.kinds[0] == static_cast<eng::u8>(BlitOp::Kind::Fill),
	      "pump ejecuta 1 y respeta FIFO (Fill primero)");
	check(blitter.kinds[0] == 0 && blitter.last_ashift == 0, "Fill no toca ashift");

	// 3) El Blitter se libera con el tiempo; pump sigue sacando de la cola.
	while (!q.empty()) {
		q.pump();
	}
	check(q.empty() && blitter.submitted == 2, "pump drena el resto sin esperar");
	check(blitter.kinds[1] == static_cast<eng::u8>(BlitOp::Kind::Stamp) && blitter.last_ashift == 5,
	      "Stamp conserva el ashift");

	// 2b) `blit_job_from` traduce la intencion al TRABAJO canonico (`BlitJob`); `blitter_job_from`
	//     codifica ese trabajo a los REGISTROS (`BlitterJob`). Una sola ruta, dos capas.
	using eng::graphics::blit_job_from;
	using eng::graphics::blitter_job_from;
	{
		const eng::graphics::BlitJob f =
			blit_job_from(BlitOp {BlitOp::Kind::Fill, dst, {}, {}, {0, 0, 32u, 8u}, 0});
		check(f.kind == eng::graphics::BlitJobKind::ClearRect && f.words_per_row == 2u &&
			      f.height == 8u && f.destination_modulo_bytes == -2 && f.minterm == 0u,
		      "blit_job_from Fill -> ClearRect");
		const eng::graphics::BlitterJob fr = blitter_job_from(f);
		check(fr.bltcon0 == 0x0100u && fr.bltsize == static_cast<eng::u16>((8u << 6) | 2u) &&
			      fr.bltalwm == 0xffffu,
		      "blitter_job_from ClearRect");
		const eng::graphics::BlitJob s =
			blit_job_from(BlitOp {BlitOp::Kind::Stamp, dst, src, {}, {5, 0, 32u, 96u}, 5});
		check(s.kind == eng::graphics::BlitJobKind::OrBlob && s.words_per_row == 3u &&
			      s.height == 96u && s.source_modulo_bytes == -4 &&
			      s.destination_modulo_bytes == -4 && s.source_shift == 5u &&
			      s.minterm == 0xfcu,
		      "blit_job_from Stamp -> OrBlob");
		const eng::graphics::BlitterJob sr = blitter_job_from(s);
		check(sr.bltcon0 == 0x5dfcu && sr.bltsize == static_cast<eng::u16>((96u << 6) | 3u) &&
			      sr.bltbpt == sr.bltdpt,
		      "blitter_job_from OrBlob");
	}

	// 2c) Estampa con mascara (cookie-cut -> MaskedBobCookieCut; $CA, A = mascara, C = D).
	{
		const eng::graphics::BlitJob m = blit_job_from(
			BlitOp {BlitOp::Kind::MaskedStamp, dst, src, src, {0, 0, 32u, 8u}, 0});
		check(m.kind == eng::graphics::BlitJobKind::MaskedBobCookieCut && m.minterm == 0xcau,
		      "blit_job_from MaskedStamp -> MaskedBobCookieCut");
		const eng::graphics::BlitterJob mr = blitter_job_from(m);
		check(mr.bltcon0 == 0x0fcau && mr.bltcpt == mr.bltdpt,
		      "blitter_job_from cookie-cut ($CA)");
		// MaskedBlobNoSave comparte codificacion con MaskedBobCookieCut.
		eng::graphics::BlitJob mns = m;
		mns.kind = eng::graphics::BlitJobKind::MaskedBlobNoSave;
		check(blitter_job_from(mns).bltcon0 == 0x0fcau,
		      "MaskedBlobNoSave usa la misma codificacion ($CA)");
	}

	// 2d) El encoder refleja el backend: con ancho de fila de origen propio (`source_words_per_row`),
	//     el modulo de A se DERIVA (fuente ancha), no se usa `source_modulo_bytes`.
	{
		eng::graphics::BlitJob wj {};
		wj.kind = eng::graphics::BlitJobKind::OrBlob;
		wj.words_per_row = 4u;
		wj.source_words_per_row = 10u;
		wj.source_modulo_bytes = -999; // se ignora al declararse el ancho de fila
		const eng::graphics::BlitterJob e = blitter_job_from(wj);
		check(e.bltamod == static_cast<eng::s16>((10 - 4) * 2),
		      "blitter_job_from deriva el modulo de A (fuente ancha)");
	}

	// 4) wait() vacia la cola (punto de dependencia) y respeta el orden.
	BlitOp ops[3] {};
	ops[0].kind = BlitOp::Kind::Stamp;
	ops[1].kind = BlitOp::Kind::Fill;
	ops[2].kind = BlitOp::Kind::Stamp;
	q.all(eng::Span<const BlitOp> {ops, 3});
	check(!q.empty() && blitter.submitted == 2, "all() encola en bloque sin ejecutar");
	q.wait();
	check(q.empty() && blitter.submitted == 5, "wait() vacia la cola (3 ejecutadas)");
	check(blitter.kinds[2] == static_cast<eng::u8>(BlitOp::Kind::Stamp) &&
		      blitter.kinds[3] == static_cast<eng::u8>(BlitOp::Kind::Fill) &&
		      blitter.kinds[4] == static_cast<eng::u8>(BlitOp::Kind::Stamp),
	      "FIFO con array de golpe");

	// 5) Equivalencia: `blit_regs(BlitOp)` == los campos del `BlitJob` que emite `bob_draw` para
	//    la MISMA geometria (hoja OR intercalada). Cierra la duplicacion: la intencion (zona+rect)
	//    produce los mismos registros que el camino de BOB.
	{
		using eng::graphics::BlitJob;
		using eng::graphics::Bob;
		using eng::graphics::BobDraw;
		using eng::graphics::BobLayout;
		using eng::graphics::BitmapView;
		using eng::graphics::blit_job_from;
		using eng::graphics::bob_draw;
		using eng::graphics::FramePlan;
		using eng::graphics::make_bob_target;
		using eng::graphics::PlaneLayout;
		static eng::u16 sheet[64] {};
		Bob bob {};
		bob.sheet = eng::ChipView<eng::BobTag> {
			eng::Address<eng::MemoryKind::Chip>::from_storage(sheet), sizeof(sheet)};
		bob.width = 32u;
		bob.height = 16u;
		bob.planes = 1u;
		bob.frame_count = 1u;
		bob.frame_stride = 0u;
		bob.sheet_row_bytes = 6u; // 2 palabras + guarda
		bob.layout = BobLayout::Interleaved;
		bob.draw = BobDraw::Or;
		const eng::graphics::BobTarget t =
			make_bob_target(dst.planes, dst.row_bytes, dst.height, dst.plane_count,
					PlaneLayout::Interleaved);
		FramePlan plan;
		plan.clear();
		check(bob_draw(plan, bob, 0u, 5, 2, t), "bob_draw OR intercalado");
		const BlitJob& j = plan.blit_job(0u);
		const BitmapView<eng::BobTag, eng::MemoryKind::Chip> sz {
			bob.sheet, 32u, 16u, 6u, 1u, PlaneLayout::Interleaved};
		const eng::graphics::BlitJob bj =
			blit_job_from(BlitOp {BlitOp::Kind::Stamp, t, sz, {}, {5, 2, 32u, 16u}, 5});
		check(bj.kind == j.kind && bj.words_per_row == j.words_per_row && bj.height == j.height &&
			      bj.source_modulo_bytes == j.source_modulo_bytes &&
			      bj.destination_modulo_bytes == j.destination_modulo_bytes &&
			      bj.source_shift == j.source_shift && bj.minterm == j.minterm &&
			      bj.source.words == j.source.words &&
			      bj.destination.words == j.destination.words,
		      "blit_job_from == BlitJob de bob_draw (mismo TIPO y geometria)");
	}

	// 6) Ejecutor por Copper: `submit()` emite el `BlitterJob` por la lista, no programa registros.
	{
		FakeCopper cu;
		eng::graphics::CopperBlitterExecutor<FakeCopper> ex {cu, 300u};
		eng::graphics::BlitQueue<4, eng::graphics::CopperBlitterExecutor<FakeCopper>> cq;
		cq.bind(ex);
		cq.stamp(src, dst, {5, 0, 32u, 96u}, 5);
		check(ex.blitter_free() && cu.emitted == 0, "copper: encolar no emite");
		cq.wait();
		check(cu.emitted == 1 && cu.last_top == 300u && cu.last.bltcon0 == 0x5dfcu,
		      "copper: wait emite el BlitterJob en su linea");
	}

	if (failures == 0) {
		std::printf("OK: BlitQueue (cola por intencion + feeder poll + FIFO) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
