#include <cstdio>
#include <cstring>

#include "../../../support/software_blitter.hpp"
#include <eng/graphics/bob.hpp>

using namespace eng;
using eng::graphics::Bob;
using eng::graphics::BobLayout;
using eng::graphics::BobDraw;
using eng::graphics::BobTarget;
using eng::graphics::FramePlan;
using eng::graphics::make_bob_target;
using eng::graphics::blitter_job_from;

constexpr u16 kRowBytes = 16u;
constexpr u16 kHeight = 64u;
constexpr u8 kPlanes = 4u;

alignas(2) u8 g_bitmap[kRowBytes * kHeight * kPlanes] {};
alignas(2) u8 g_sheet[16384] {};
alignas(2) u8 g_mask[8192] {};

template <class Tag = BobTag, class T, usize N>
ChipView<Tag> chip_view(const T (&a)[N]) {
	return ChipView<Tag> {Address<MemoryKind::Chip>::from_storage(a), static_cast<usize>(N) * sizeof(T)};
}

int main() {
	const u16 w = 12u, h = 8u;
	const u16 rw = (w + 15u) / 16u + 1u;
	const u16 wpp = rw * h;
	std::printf("rw=%u wpp=%u\n", rw, wpp);
	// patrón: borde=3, damero 1/2
	auto pat = [&](u16 cx, u16 cy) -> u8 {
		if (cx == 0 || cx + 1 == w || cy == 0 || cy + 1 == h) return 3u;
		return ((cx + cy) & 1) ? 1u : 2u;
	};
	auto be_or = [&](u32 word_index, u16 bits) {
		u8* p = g_sheet + word_index * 2u;
		const u16 cur = static_cast<u16>((static_cast<u16>(p[0]) << 8u) | p[1]);
		const u16 next = static_cast<u16>(cur | bits);
		p[0] = static_cast<u8>(next >> 8u);
		p[1] = static_cast<u8>(next & 0xffu);
	};
	for (u8 p = 0; p < 2u; ++p) {
		for (u16 row = 0; row < h; ++row) {
			for (u16 cx = 0; cx < w; ++cx) {
				if ((pat(cx, row) & (1u << p)) == 0u) continue;
				const u16 wi = cx / 16u, bit = 15u - (cx % 16u);
				be_or(p * wpp + row * rw + wi, (1u << bit));
			}
		}
	}
	auto be_read = [&](u32 word_index) {
		const u8* p = g_sheet + word_index * 2u;
		return static_cast<u16>((static_cast<u16>(p[0]) << 8u) | p[1]);
	};
	// máscara: anillo del borde (1 plano)
	{
		auto be_or_m = [&](u32 word_index, u16 bits) {
			u8* p = g_mask + word_index * 2u;
			const u16 cur = static_cast<u16>((static_cast<u16>(p[0]) << 8u) | p[1]);
			const u16 next = static_cast<u16>(cur | bits);
			p[0] = static_cast<u8>(next >> 8u);
			p[1] = static_cast<u8>(next & 0xffu);
		};
		for (u16 row = 0; row < h; ++row) {
			for (u16 cx = 0; cx < w; ++cx) {
				const bool ring = cx == 0 || cx + 1 == w || row == 0 || row + 1 == h;
				if (!ring) continue;
				const u16 wi = cx / 16u, bit = 15u - (cx % 16u);
				be_or_m(row * rw + wi, (1u << bit));
			}
		}
	}
	std::printf("sheet p0 row0 = %04x p1 row0 = %04x\n", be_read(0u), be_read(wpp));

	Bob b {};
	b.sheet = chip_view(g_sheet);
	b.width = w;
	b.height = h;
	b.planes = 2u;
	b.layout = BobLayout::Planar;
	b.draw = BobDraw::Or;

	auto t = make_bob_target(chip_view<PlaneTag>(g_bitmap), kRowBytes, kHeight, kPlanes, BobLayout::Planar);

	FramePlan plan {};
	const bool ok = bob_draw(plan, b, 0u, 16, 4, t);
	std::printf("bob_draw=%d jobs=%u\n", ok ? 1 : 0, plan.blit_job_count());
	const auto& j = plan.blit_job(0);
	std::printf("kind=%u words=%u height=%u shift=%u planes=%u inter=%d\n",
		    (unsigned)j.kind, j.words_per_row, j.height, j.source_shift, j.bitplane_count,
		    j.interleaved ? 1 : 0);
	std::printf("src=%p dst=%p minterm=%02x\n", (const void*)j.source.words(), (void*)j.destination.words(),
		    j.minterm);
	std::printf("src_mod=%d dst_mod=%d src_step=%u dst_step=%u\n", j.source_modulo_bytes,
		    j.destination_modulo_bytes, j.source_plane_stride_bytes, j.destination_plane_stride_bytes);

	const auto r = blitter_job_from(j);
	std::printf("bltcon0=%04x bltcon1=%04x afwm=%04x alwm=%04x amod=%d bmod=%d cmod=%d dmod=%d size=%04x\n",
		    r.bltcon0, r.bltcon1, r.bltafwm, r.bltalwm, r.bltamod, r.bltbmod, r.bltcmod, r.bltdmod,
		    r.bltsize);
	std::printf("apt=%p bpt=%p cpt=%p dpt=%p\n", r.bltapt, r.bltbpt, r.bltcpt, r.bltdpt);

	eng::test::SoftBitmap bm {g_bitmap, kRowBytes, kHeight, kPlanes, false, 0u};
	const auto res = eng::test::execute_plan(plan, bm);
	std::printf("exec jobs=%u unsupported=%d oob=%d\n", res.jobs_executed, res.unsupported ? 1 : 0,
		    res.out_of_bounds ? 1 : 0);
	for (u16 row = 3; row < 13u; ++row) {
		const u8* p0 = bm.row_ptr(0u, row);
		const u8* p1 = bm.row_ptr(1u, row);
		std::printf("row %2u: p0=%02x %02x p1=%02x %02x\n", row, p0[2], p0[3], p1[2], p1[3]);
	}
	// caso interleaved
	{
		b.layout = BobLayout::Interleaved;
		FramePlan p2 {};
		const bool ok2 = bob_draw(p2, b, 0u, 21, 4, t);
		std::printf("interleaved bob_draw=%d jobs=%u ok_plan=%d\n", ok2 ? 1 : 0,
			    p2.blit_job_count(), p2.ok() ? 1 : 0);
		if (p2.blit_job_count()) {
			const auto& jj = p2.blit_job(0);
			std::printf("  kind=%u words=%u h=%u planes=%u inter=%d src_step=%u dst_step=%u shift=%u src=%p\n",
				    (unsigned)jj.kind, jj.words_per_row, jj.height, jj.bitplane_count,
				    jj.interleaved ? 1 : 0, jj.source_plane_stride_bytes,
				    jj.destination_plane_stride_bytes, jj.source_shift,
				    (const void*)jj.source.words());
		}
	}
	// caso masked planar
	{
		b.layout = BobLayout::Planar;
		b.draw = BobDraw::CookieCut;
		b.mask = chip_view(g_mask);
		FramePlan p3 {};
		const bool ok3 = bob_draw(p3, b, 0u, 16, 4, t);
		std::printf("masked bob_draw=%d jobs=%u ok=%d\n", ok3 ? 1 : 0, p3.blit_job_count(),
			    p3.ok() ? 1 : 0);
		if (p3.blit_job_count()) {
			const auto& jj = p3.blit_job(0);
			std::printf("  kind=%u words=%u h=%u planes=%u mask=%p src=%p minterm=%02x\n",
				    (unsigned)jj.kind, jj.words_per_row, jj.height, jj.bitplane_count,
				    (const void*)jj.mask.words(), (const void*)jj.source.words(),
				    jj.minterm);
			const auto rr = blitter_job_from(jj);
			std::printf("  con0=%04x apt=%p bpt=%p cpt=%p dpt=%p amod=%d bmod=%d cmod=%d dmod=%d\n",
				    rr.bltcon0, rr.bltapt, rr.bltbpt, rr.bltcpt, rr.bltdpt, rr.bltamod,
				    rr.bltbmod, rr.bltcmod, rr.bltdmod);
			std::memset(g_bitmap, 0, sizeof(g_bitmap));
			// prefill color 1 en 16..27
			eng::test::SoftBitmap bm2 {g_bitmap, kRowBytes, kHeight, kPlanes, false, 0u};
			for (u16 y = 4; y < 12; ++y) {
				bm2.row_ptr(0u, y)[2] = 0xff; bm2.row_ptr(0u, y)[3] = 0xff;
			}
			const auto res3 = eng::test::execute_plan(p3, bm2);
			std::printf("  exec oob=%d\n", res3.out_of_bounds ? 1 : 0);
			for (u16 row = 4; row < 12u; ++row) {
				const u8* p0 = bm2.row_ptr(0u, row);
				const u8* p1 = bm2.row_ptr(1u, row);
				std::printf("  row %2u: p0=%02x %02x p1=%02x %02x\n", row, p0[2], p0[3], p1[2],
					    p1[3]);
			}
		}
	}
	return 0;
}
