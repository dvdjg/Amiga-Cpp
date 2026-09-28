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

	// 2b) `blit_regs` deriva los registros de la zona + el rect (no se pasan a mano).
	using eng::graphics::blit_regs;
	{
		const eng::graphics::BlitRegs f = blit_regs(BlitOp {BlitOp::Kind::Fill, dst, {}, {0, 0, 32u, 8u}, 0});
		check(f.words == 2u && f.height == 8u && f.dst_mod == -2 && f.minterm == 0x00u &&
			      f.interleaved && f.dst_plane_stride == 0u,
		      "blit_regs Fill (interleaved)");
		const eng::graphics::BlitRegs s =
			blit_regs(BlitOp {BlitOp::Kind::Stamp, dst, src, {5, 0, 32u, 96u}, 5});
		check(s.words == 3u && s.height == 96u && s.ashift == 5u && s.src_mod == -4 &&
			      s.dst_mod == -4 && s.minterm == 0x00fcu && s.src_plane_stride == 0u,
		      "blit_regs Stamp (interleaved, con shift)");
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
		using eng::graphics::blit_regs;
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
		const eng::graphics::BlitRegs r =
			blit_regs(BlitOp {BlitOp::Kind::Stamp, t, sz, {5, 2, 32u, 16u}, 5});
		check(r.words == j.words_per_row && r.height == j.height &&
			      r.src_mod == j.source_modulo_bytes && r.dst_mod == j.destination_modulo_bytes &&
			      r.ashift == j.source_shift && r.minterm == j.minterm &&
			      r.src == j.source.words && r.dst == j.destination.words,
		      "blit_regs == BlitJob de bob_draw (misma geometria)");
	}

	if (failures == 0) {
		std::printf("OK: BlitQueue (cola por intencion + feeder poll + FIFO) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
