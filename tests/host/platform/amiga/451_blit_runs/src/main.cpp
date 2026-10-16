// Test host de la **clasificación de rachas de estado fijo** del `FramePlan`
// (`eng::graphics::blob_run_from`/`blob_run_same`, usadas por `execute_frame_plan` y por el
// feeder async) y de la **equivalencia de registros** entre el camino de racha (`BlobBatch`) y
// el encoder canónico (`blitter_job_from`) para cada operación clasificable (cookie-cut, clear,
// OR). Estos son los invariantes que hacen segura la optimización: una racha `[from,to)` debe
// escribir, por objeto, los MISMOS registros efectivos que el camino genérico.
//
// Cubre además la barrera de debug del encoder (`blit_op_valid`) y el rechazo de geometría no
// codificable en `BLTSIZE` por el `FramePlan`.
#define ENG_SCALAR_RETRO16
#include <eng/graphics/blit_queue.hpp>
#include <eng/graphics/blitter_state.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/platform/amiga/blob_batch.hpp>

#include <cstdio>

using eng::amiga::BlobBatch;
using eng::amiga::BlobOp;
using eng::amiga::HwRegs;
using eng::graphics::BlitJob;
using eng::graphics::BlitJobKind;
using eng::graphics::BlitPtr;
using eng::graphics::BlobRun;
using eng::graphics::FramePlan;

static int failures = 0;
static void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

/// Puntero de prueba: dirección fija en Chip (frontera declarada del test host).
static BlitPtr chip_ptr(eng::uintptr addr) {
	return BlitPtr {eng::Address<eng::MemoryKind::Chip> {addr}};
}

// Offsets (en palabras) de los registros que programa el lote (mock de `HwRegs`).
static constexpr eng::u16 kDmacon = 0x096u / 2u;
static constexpr eng::u16 kBltcon0 = 0x040u / 2u;
static constexpr eng::u16 kBltafwm = 0x044u / 2u;
static constexpr eng::u16 kBltcpt = 0x048u / 2u;
static constexpr eng::u16 kBltbpt = 0x04cu / 2u;
static constexpr eng::u16 kBltapt = 0x050u / 2u;
static constexpr eng::u16 kBltdpt = 0x054u / 2u;
static constexpr eng::u16 kBltsize = 0x058u / 2u;
static constexpr eng::u16 kBltcmod = 0x060u / 2u;
static constexpr eng::u16 kBltamod = 0x064u / 2u;

static volatile eng::u16 regs[0x100] {};

static eng::u32 rd32(eng::u16 word) {
	// Los pares empaquetados se escriben con una única escritura nativa de 32 bits; en el mock
	// de host se leen nativas (`blob_batch.hpp` `write_long`).
	return *reinterpret_cast<volatile eng::u32*>(&regs[word]);
}

// --- Constructores de trabajos representativos (geometrías de la 213) ------------------------

/// Cookie-cut interleaved de un canal (`$CA`): el contrato del par `[imagen][máscara]`.
static BlitJob masked_job(eng::u8 shift) {
	BlitJob j {};
	j.kind = BlitJobKind::MaskedBobCookieCut;
	j.mask = chip_ptr(0x00c10000u);
	j.source = chip_ptr(0x00c10004u);
	j.destination = chip_ptr(0x00c20000u);
	j.words_per_row = 2u;
	j.height = 80u; // 16 filas lógicas × 5 planos
	j.source_modulo_bytes = 4;
	j.destination_modulo_bytes = 36;
	j.bitplane_count = 1u;
	j.source_shift = shift;
	j.minterm = eng::graphics::kBlitterMintermCookieCut;
	j.interleaved = true;
	return j;
}

static BlitJob clear_job() {
	BlitJob j {};
	j.kind = BlitJobKind::ClearRect;
	j.destination = chip_ptr(0x00c20000u);
	j.words_per_row = 20u;
	j.height = 280u; // 56 filas × 5 planos
	j.destination_modulo_bytes = 0;
	j.bitplane_count = 1u;
	j.minterm = 0x00u;
	j.interleaved = true;
	return j;
}

static BlitJob or_job(eng::u8 shift) {
	BlitJob j {};
	j.kind = BlitJobKind::OrBlob;
	j.source = chip_ptr(0x00c30000u);
	j.destination = chip_ptr(0x00c20000u);
	j.words_per_row = 3u;
	j.height = 96u;
	j.source_modulo_bytes = -6;
	j.destination_modulo_bytes = 34;
	j.bitplane_count = 1u;
	j.source_shift = shift;
	j.minterm = eng::graphics::kBlitterMintermAOrB;
	j.interleaved = true;
	return j;
}

/// Copia planar multi-plano (restaurador del carril de la 121): `planes` planos contiguos con
/// strides explícitos, fuente con ancho de fila propio (módulo 38) y sin desplazamiento.
static BlitJob copy_job(eng::u8 planes) {
	BlitJob j {};
	j.kind = BlitJobKind::CopyRect;
	j.source = chip_ptr(0x00c40000u);
	j.destination = chip_ptr(0x00c50000u);
	j.words_per_row = 20u;
	j.height = 16u;
	j.source_modulo_bytes = 38;
	j.destination_modulo_bytes = 36;
	j.bitplane_count = planes;
	j.source_plane_stride_bytes = 40u * 16u;
	j.destination_plane_stride_bytes = 40u * 16u;
	return j;
}

int main() {
	const HwRegs cregs = HwRegs::for_test(regs);

	// --- 1) Clasificación: acepta lo homogéneo de un canal, rechaza el resto ------------------
	{
		BlobRun run {};
		check(eng::graphics::blob_run_from(masked_job(5u), run) && run.op == BlobOp::CookieCut &&
			      run.words == 2u && run.height == 80u && run.amod == 4 && run.bmod == 4 &&
			      run.cmod == 36 && run.dmod == 36,
		      "masked $CA interleaved -> racha CookieCut con módulos del par");
		check(eng::graphics::blob_run_from(clear_job(), run) && run.op == BlobOp::Clear &&
			      run.amod == 0 && run.bmod == 0 && run.cmod == 0 && run.dmod == 0,
		      "clear interleaved -> racha Clear (solo D)");
		check(eng::graphics::blob_run_from(or_job(0u), run) && run.op == BlobOp::Or &&
			      run.amod == -6 && run.bmod == 34 && run.cmod == 0 && run.dmod == 34,
		      "OR interleaved -> racha Or (A objeto, B=D destino)");

		BlitJob j = masked_job(0u);
		j.descending = true;
		check(!eng::graphics::blob_run_from(j, run), "descendente NO es racha");
		j = masked_job(0u);
		j.interleaved = false;
		check(!eng::graphics::blob_run_from(j, run), "multi-plano (planar) NO es racha");
		j = masked_job(0u);
		j.bitplane_count = 2u;
		check(!eng::graphics::blob_run_from(j, run), "bitplane_count > 1 NO es racha");
		j = masked_job(0u);
		j.source_words_per_row = 10u;
		check(!eng::graphics::blob_run_from(j, run), "fuente con ancho propio NO es racha");
		j = masked_job(0u);
		j.minterm = 0xf0u;
		check(!eng::graphics::blob_run_from(j, run), "minterm ajeno NO es racha");
		j = masked_job(0u);
		j.height = 1024u;
		check(!eng::graphics::blob_run_from(j, run), "height 1024 fuera del lote (BLTSIZE lo codifica 0)");
		j = clear_job();
		j.destination_plane_stride_bytes = 40u;
		check(!eng::graphics::blob_run_from(j, run), "clear con salto entre planos NO es racha");
		j = or_job(0u);
		j.kind = BlitJobKind::LogicBlit;
		check(!eng::graphics::blob_run_from(j, run), "LogicBlit NO es racha");

		// Copias multi-plano sin desplazamiento (`D = C`): racha con CMOD derivado como el
		// encoder (fuente compacta o con ancho de fila propio).
		check(eng::graphics::blob_run_from(copy_job(3u), run) && run.op == BlobOp::Copy &&
			      run.words == 20u && run.height == 16u && run.amod == 0 && run.bmod == 0 &&
			      run.cmod == 38 && run.dmod == 36,
		      "copia planar multi-plano sin shift -> racha Copy (C = origen, D = destino)");
		j = copy_job(3u);
		j.source_shift = 3u;
		check(!eng::graphics::blob_run_from(j, run), "copia con shift NO es racha (barrel por A)");
		j = copy_job(3u);
		j.descending = true;
		check(!eng::graphics::blob_run_from(j, run), "copia descendente NO es racha");
		j = copy_job(3u);
		j.source_words_per_row = 24u;
		check(eng::graphics::blob_run_from(j, run) && run.cmod == static_cast<eng::s16>((24 - 20) * 2),
		      "copia con ancho de fila propio deriva CMOD como el encoder");

		// `blob_run_same`: el estado fijo ignora punteros y shift; cualquier módulo lo rompe.
		check(eng::graphics::blob_run_same(masked_job(0u), masked_job(7u)),
		      "mismo estado con shift distinto -> misma racha");
		BlitJob other = masked_job(0u);
		other.destination_modulo_bytes = 34;
		check(!eng::graphics::blob_run_same(masked_job(0u), other),
		      "módulo de destino distinto -> otra racha");
		check(!eng::graphics::blob_run_same(masked_job(0u), or_job(0u)),
		      "cookie-cut y OR no comparten racha");
		BlitJob nosave = masked_job(0u);
		nosave.kind = BlitJobKind::MaskedBlobNoSave;
		check(eng::graphics::blob_run_same(masked_job(0u), nosave),
		      "MaskedBlobNoSave comparte racha con MaskedBobCookieCut (mismos registros)");

		// `blob_run_continues`: el candidato contra el lote ya calculado del ancla (una sola
		// clasificación por candidato: es la consulta del bucle de detección del backend).
		BlobRun anchor {};
		check(eng::graphics::blob_run_from(masked_job(0u), anchor) &&
			      eng::graphics::blob_run_continues(masked_job(9u), anchor) &&
			      !eng::graphics::blob_run_continues(other, anchor) &&
			      !eng::graphics::blob_run_continues(clear_job(), anchor),
		      "blob_run_continues: sigue el mismo lote; módulos u operación distintos no");
		BlobRun copy_anchor {};
		check(eng::graphics::blob_run_from(copy_job(3u), copy_anchor) &&
			      eng::graphics::blob_run_continues(copy_job(1u), copy_anchor) &&
			      !eng::graphics::blob_run_continues(masked_job(0u), copy_anchor),
		      "blob_run_continues: copias de distinto nº de planos comparten racha (por objeto)");
	}

	// --- 2) Equivalencia de registros: racha (BlobBatch) == encoder canónico -------------------
	// Por cada operación: el lote + `one` deben dejar los MISMOS registros EFECTIVOS que
	// `blitter_job_from(job)` (el camino genérico). Se comparan los canales que la operación usa
	// (C está deshabilitado en OR/clear: su módulo no es efectivo).
	{
		const auto compare_common = [](const eng::graphics::BlitterJob& b, const char* what) {
			// CON0/CON1 y AFWM/ALWM van empaquetados en una sola escritura de 32 bits (en el
			// target, big-endian). El mock del host es little-endian, así que se compara el
			// VALOR del par (igual que HOST-241), no las palabras sueltas.
			check(rd32(kBltcon0) ==
				      ((static_cast<eng::u32>(b.bltcon0) << 16u) |
				       static_cast<eng::u32>(b.bltcon1)),
			      what);
			check(rd32(kBltafwm) ==
				      ((static_cast<eng::u32>(b.bltafwm) << 16u) |
				       static_cast<eng::u32>(b.bltalwm)),
			      what);
		};
		const auto ptr32 = [](const void* p) {
			return static_cast<eng::u32>(reinterpret_cast<eng::uintptr>(p));
		};
		const auto compare_size = [](const eng::graphics::BlitterJob& b) {
			check(regs[kBltsize] == b.bltsize, "BLTSIZE de la racha == encoder");
		};

		// Cookie-cut: A = máscara, B = imagen, C = D = destino. ASH (CON0) y BSH (CON1).
		const eng::u8 masked_shifts[] = {0u, 5u, 15u};
		for (eng::u8 shift : masked_shifts) {
			BlobBatch batch;
			BlobRun run {};
			const BlitJob j = masked_job(shift);
			check(eng::graphics::blob_run_from(j, run), "masked clasifica");
			batch.begin(cregs, run.op, run.words, run.height, run.amod, run.bmod, run.cmod,
				    run.dmod);
			// Ventana fina por objeto, igual que el ejecutor (sync/async) y el encoder:
			// recorta pre-shift los bits bajos de la máscara (AHRM 6; HOST-439 T6b).
			batch.set_window_masks(0xffffu, static_cast<eng::u16>(0xffffu << shift));
			batch.one(j.mask, j.source, j.destination, shift);
			const eng::graphics::BlitterJob b = eng::graphics::blitter_job_from(j);
			compare_common(b, "cookie-cut: CON0/CON1/AFWM/ALWM de la racha == encoder");
			check(rd32(kBltapt) == ptr32(b.bltapt), "cookie-cut: BLTAPT = máscara");
			check(rd32(kBltbpt) == ptr32(b.bltbpt), "cookie-cut: BLTBPT = imagen");
			check(rd32(kBltcpt) == ptr32(b.bltcpt) && rd32(kBltdpt) == ptr32(b.bltdpt),
			      "cookie-cut: BLTCPT/BLTDPT = destino");
			check(rd32(kBltamod) ==
				      ((static_cast<eng::u32>(static_cast<eng::u16>(b.bltamod)) << 16u) |
				       static_cast<eng::u16>(b.bltdmod)),
			      "cookie-cut: AMOD/DMOD de la racha == encoder");
			check(rd32(kBltcmod) ==
				      ((static_cast<eng::u32>(static_cast<eng::u16>(b.bltcmod)) << 16u) |
				       static_cast<eng::u16>(b.bltbmod)),
			      "cookie-cut: CMOD/BMOD de la racha == encoder");
			compare_size(b);
		}

		// Clear: solo D (A/B/C deshabilitados).
		{
			BlobBatch batch;
			BlobRun run {};
			const BlitJob j = clear_job();
			check(eng::graphics::blob_run_from(j, run), "clear clasifica");
			batch.begin(cregs, run.op, run.words, run.height, run.amod, run.bmod, run.cmod, run.dmod);
			batch.one(BlitPtr {}, BlitPtr {}, j.destination, 0u);
			const eng::graphics::BlitterJob b = eng::graphics::blitter_job_from(j);
			compare_common(b, "clear: CON0/CON1/AFWM/ALWM de la racha == encoder");
			check(rd32(kBltdpt) == ptr32(b.bltdpt), "clear: BLTDPT = destino");
			check(rd32(kBltamod) ==
				      ((static_cast<eng::u32>(static_cast<eng::u16>(b.bltamod)) << 16u) |
				       static_cast<eng::u16>(b.bltdmod)),
			      "clear: AMOD/DMOD de la racha == encoder");
			compare_size(b);
		}

		// OR: A = objeto, B = D = destino (C deshabilitado). Objeto de 3 palabras con guarda
		// (`source_modulo_bytes = -6`): el barrido completo de `shift` (0..15) blinda la
		// equivalencia de la ventana fina `BLTALWM = 0xffff << shift` con el encoder.
		for (eng::u8 shift = 0u; shift < 16u; ++shift) {
			BlobBatch batch;
			BlobRun run {};
			const BlitJob j = or_job(shift);
			check(eng::graphics::blob_run_from(j, run), "OR clasifica");
			batch.begin(cregs, run.op, run.words, run.height, run.amod, run.bmod, run.cmod, run.dmod);
			// El ejecutor de la racha OR fija la ventana fina por objeto (igual que el encoder);
			// sin ella `BLTALWM` quedaría a 0xffff y no sería equivalente al camino genérico.
			batch.set_window_masks(0xffffu, static_cast<eng::u16>(0xffffu << shift));
			batch.one(j.source, j.destination, j.destination, shift);
			const eng::graphics::BlitterJob b = eng::graphics::blitter_job_from(j);
			compare_common(b, "OR: CON0/CON1 de la racha == encoder");
			check(rd32(kBltapt) == ptr32(b.bltapt), "OR: BLTAPT = objeto");
			check(rd32(kBltbpt) == ptr32(b.bltbpt) && rd32(kBltdpt) == ptr32(b.bltdpt),
			      "OR: BLTBPT/BLTDPT = destino");
			// Canal C deshabilitado en OR: solo A/B/D son efectivos.
			check(rd32(kBltamod) ==
				      ((static_cast<eng::u32>(static_cast<eng::u16>(b.bltamod)) << 16u) |
				       static_cast<eng::u16>(b.bltdmod)),
			      "OR: AMOD/DMOD de la racha == encoder");
			compare_size(b);
		}

		// Copia multi-plano: `C = origen`, `D = destino`; `one_planes` recorre los planos con los
		// strides del job (el mismo bucle que `submit_blit_job` en el camino genérico).
		{
			BlobBatch batch;
			BlobRun run {};
			const BlitJob j = copy_job(3u);
			check(eng::graphics::blob_run_from(j, run), "copia clasifica");
			batch.begin(cregs, run.op, run.words, run.height, run.amod, run.bmod, run.cmod,
				    run.dmod);
			const eng::graphics::BlitterJob b = eng::graphics::blitter_job_from(j);
			compare_common(b, "copia: CON0/CON1/AFWM/ALWM de la racha == encoder");
			check(rd32(kBltamod) ==
				      ((static_cast<eng::u32>(static_cast<eng::u16>(b.bltamod)) << 16u) |
				       static_cast<eng::u16>(b.bltdmod)),
			      "copia: AMOD/DMOD de la racha == encoder");
			check(rd32(kBltcmod) ==
				      ((static_cast<eng::u32>(static_cast<eng::u16>(b.bltcmod)) << 16u) |
				       static_cast<eng::u16>(b.bltbmod)),
			      "copia: CMOD/BMOD de la racha == encoder");
			batch.one_planes(j.source, j.destination, j.bitplane_count,
					 j.source_plane_stride_bytes, j.destination_plane_stride_bytes);
			compare_size(b);
			// `one_planes` deja en los registros el ÚLTIMO plano: base + 2·stride (640 B).
			check(rd32(kBltcpt) == ptr32(b.bltcpt) + 2u * 640u &&
				      rd32(kBltdpt) == ptr32(b.bltdpt) + 2u * 640u,
			      "copia: los planos avanzan con los strides del job (plano 2 en registros)");
		}

		// Un segundo objeto de la racha solo reescribe punteros/shift/BLTSIZE (estado intacto).
		{
			BlobBatch batch;
			batch.begin(cregs, BlobOp::CookieCut, 2u, 80u, 4, 4, 36, 36);
			const eng::u32 mods_before = rd32(kBltamod);
			const eng::u32 cmods_before = rd32(kBltcmod);
			const eng::u32 con_before = rd32(kBltcon0);
			BlitJob j2 = masked_job(3u);
			j2.mask = chip_ptr(0x00c11000u);
			j2.source = chip_ptr(0x00c11004u);
			j2.destination = chip_ptr(0x00c21000u);
			batch.one(j2.mask, j2.source, j2.destination, j2.source_shift);
			check(rd32(kBltamod) == mods_before && rd32(kBltcmod) == cmods_before,
			      "segundo objeto: módulos intactos");
			check(rd32(kBltcon0) != con_before, "segundo objeto: CON0 se reescribe con su shift");
			check(rd32(kBltapt) == 0x00c11000u && rd32(kBltbpt) == 0x00c11004u &&
				      rd32(kBltdpt) == 0x00c21000u,
			      "segundo objeto: punteros re-apuntados");
			check(regs[kBltsize] == static_cast<eng::u16>((80u << 6u) | 2u), "segundo objeto: BLTSIZE");
		}
	}

	// --- 3) La intención (`BlitOp`) del camino real clasifica como racha -----------------------
	{
		static eng::u8 dst_buf[4096] {};
		static eng::u8 src_buf[256] {};
		using DstZone = eng::graphics::BitmapView<eng::PlaneTag, eng::MemoryKind::Chip>;
		using SrcZone = eng::graphics::BitmapView<eng::BobTag, eng::MemoryKind::Chip>;
		const DstZone dst {
			eng::ChipView<eng::PlaneTag> {
				eng::Address<eng::MemoryKind::Chip>::from_storage(dst_buf), sizeof(dst_buf)},
			320u, 256u, 40u, 5u, eng::graphics::PlaneLayout::Interleaved};
		const SrcZone src {
			eng::ChipView<eng::BobTag> {
				eng::Address<eng::MemoryKind::Chip>::from_storage(src_buf), sizeof(src_buf)},
			32u, 16u, 4u, 1u, eng::graphics::PlaneLayout::Interleaved};
		// El cookie-cut de la fachada (`Screen::sprite` -> `MaskedBobCookieCut` interleaved).
		const eng::graphics::BlitJob j = eng::graphics::blit_job_from(
			eng::graphics::BlitOp {eng::graphics::BlitOp::Kind::MaskedStamp, dst, src, src,
					       {16, 200, 32u, 16u}, 3u});
		BlobRun run {};
		check(eng::graphics::blob_run_from(j, run) && run.op == BlobOp::CookieCut,
		      "el job de MaskedStamp (32x16, 5 planos interleaved) es racha CookieCut");

		// La validación de la intención: geometría codificable.
		check(eng::graphics::blit_op_valid(eng::graphics::BlitOp {
			      eng::graphics::BlitOp::Kind::MaskedStamp, dst, src, src, {16, 200, 32u, 16u}, 3u}),
		      "blit_op_valid: MaskedStamp con src y mask");
		check(!eng::graphics::blit_op_valid(eng::graphics::BlitOp {
			      eng::graphics::BlitOp::Kind::MaskedStamp, dst, src, {}, {16, 200, 32u, 16u}, 3u}),
		      "blit_op_valid: MaskedStamp sin máscara -> inválido");
		check(!eng::graphics::blit_op_valid(eng::graphics::BlitOp {
			      eng::graphics::BlitOp::Kind::Stamp, dst, {}, {}, {0, 200, 32u, 16u}, 0u}),
		      "blit_op_valid: Stamp sin fuente -> inválido");
		check(!eng::graphics::blit_op_valid(eng::graphics::BlitOp {
			      eng::graphics::BlitOp::Kind::Fill, dst, {}, {}, {0, 0, 0u, 16u}, 0u}),
		      "blit_op_valid: rect vacío -> inválido");
		check(!eng::graphics::blit_op_valid(eng::graphics::BlitOp {
			      eng::graphics::BlitOp::Kind::Fill, dst, {}, {}, {0, -4, 32u, 16u}, 0u}),
		      "blit_op_valid: y negativo -> inválido (offset u32)");
		check(!eng::graphics::blit_op_valid(eng::graphics::BlitOp {
			      eng::graphics::BlitOp::Kind::Fill, dst, {}, {}, {0, 0, 1024u, 16u}, 0u}),
		      "blit_op_valid: 1024 px de ancho (64 palabras) -> inválido (BLTSIZE lo codifica 0)");
		check(!eng::graphics::blit_op_valid(eng::graphics::BlitOp {
			      eng::graphics::BlitOp::Kind::Fill, dst, {}, {}, {0, 0, 32u, 16u}, 16u}),
		      "blit_op_valid: ashift 16 -> inválido");
	}

	// --- 4) El plan rechaza (no-ok) la geometría que no cabe en BLTSIZE ------------------------
	{
		FramePlan plan;
		plan.clear();
		BlitJob j = masked_job(0u);
		j.words_per_row = 64u; // 64 se codifica como 0 en BLTSIZE: el encoder no lo maneja
		check(!plan.add(j) && !plan.ok(), "FramePlan rechaza words_per_row == 64");
		plan.clear();
		j = masked_job(0u);
		j.height = 2048u; // > 1024 filas: no cabe en el campo de 10 bits
		check(!plan.add(j) && !plan.ok(), "FramePlan rechaza height > 1024");
		plan.clear();
		check(plan.add(masked_job(0u)) && plan.ok(), "FramePlan acepta la racha válida");
		check(plan.blit_job_count() == 1u && plan.add(clear_job()) && plan.add(or_job(2u)) &&
			      plan.blit_job_count() == 3u && plan.ok(),
		      "FramePlan acepta clear y OR racha");
	}

	if (failures == 0) {
		std::printf("OK: blit_runs (clasificador de rachas + equivalencia de registros + validación).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
