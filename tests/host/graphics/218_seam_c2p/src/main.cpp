// ============================================================================
// Test HOST-218: C2P en el seam (Rasterizer::c2p).
// ============================================================================
//
// Valida que `CpuRaster::c2p` (la via CPU del seam) produce el MISMO planar que la
// referencia naive `c2p_1x1_naive`: unifica chunky->planar bajo la interfaz del
// `Rasterizer` sin que el llamador sepa si detras hay CPU o Blitter.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/graphics/218_seam_c2p

#include <cstdio>

#include <eng/core/types/types.hpp>
#include <eng/field/raster.hpp>
#include <eng/graphics/c2p.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

constexpr eng::u32 kW = 16;
constexpr eng::u32 kH = 4;
constexpr eng::u32 kRowBytes = kW / 8u; // 2 bytes/fila/plano
constexpr eng::u32 kPlaneStride = kRowBytes * kH;
constexpr eng::u32 kPlaneBytes = kPlaneStride * 4u;

} // namespace

int main() {
	eng::u8 chunky[kW * kH];
	for (eng::u32 y = 0; y < kH; ++y) {
		for (eng::u32 x = 0; x < kW; ++x) {
			chunky[y * kW + x] = static_cast<eng::u8>((x + y * 3u) & 0x0fu);
		}
	}

	eng::u8 a[kPlaneBytes] {};
	eng::u8 b[kPlaneBytes] {};

	const eng::field::C2pRequest req {
		eng::ChunkyView {chunky, sizeof(chunky)},
		eng::PlaneBytes {a, sizeof(a)},
		kW,
		kH,
		kPlaneStride,
		4u,
	};
	const bool ok = eng::field::kCpuRaster.c2p(req);
	check(ok, "CpuRaster::c2p acepta la peticion");

	eng::graphics::c2p_1x1_naive(kW, kH, 4u, kPlaneStride,
				     eng::ChunkyView {chunky, sizeof(chunky)},
				     eng::PlaneBytes {b, sizeof(b)});

	bool same = true;
	for (eng::u32 i = 0; i < kPlaneBytes; ++i) {
		if (a[i] != b[i]) {
			same = false;
			break;
		}
	}
	check(same, "c2p del seam == c2p_1x1_naive (4 planos)");

	// Pixel conocido: chunky (0,0)=0 -> sin bits; (1,0)=1 -> solo plano 0.
	check((a[0] & 0x40u) != 0u, "pixel (1,0) fija el bit del plano 0");
	check((a[0] & 0x80u) == 0u, "pixel (0,0)=0 no fija bits");

	// 6 planos -> cae a la via naive sin fallar.
	eng::u8 c6[kPlaneStride * 6u] {};
	const bool ok6 = eng::field::kCpuRaster.c2p(eng::field::C2pRequest {
		eng::ChunkyView {chunky, sizeof(chunky)},
		eng::PlaneBytes {c6, sizeof(c6)},
		kW,
		kH,
		kPlaneStride,
		6u,
	});
	check(ok6, "CpuRaster::c2p con 6 planos (via naive)");

	// BlitterRaster + plan + 4 planos -> ENCOLA un BlitJobKind::C2P (no convierte).
	{
		eng::graphics::FramePlan plan {};
		eng::u8 dst[kPlaneBytes] {};
		const bool queued = eng::field::kBlitterRaster.c2p(eng::field::C2pRequest {
			eng::ChunkyView {chunky, sizeof(chunky)},
			eng::PlaneBytes {dst, sizeof(dst)},
			kW,
			kH,
			kPlaneStride,
			4u,
		}, &plan);
		check(queued, "BlitterRaster::c2p encola con plan");
		check(plan.blit_job_count() == 1u &&
			      plan.blit_job(0u).kind == eng::graphics::BlitJobKind::C2P,
		      "job C2P encolado");
		check(plan.blit_job(0u).c2p.bytes == static_cast<eng::u16>(kW * kH / 2u),
		      "c2p_bytes = width*height/2");
		check(plan.blit_job(0u).c2p.planes == dst, "destino planar del job");
	}

	// BlitterRaster sin plan (o !=4 planos) -> cae a CPU.
	{
		eng::u8 dst[kPlaneBytes] {};
		check(eng::field::kBlitterRaster.c2p(eng::field::C2pRequest {
			      eng::ChunkyView {chunky, sizeof(chunky)},
			      eng::PlaneBytes {dst, sizeof(dst)}, kW, kH, kPlaneStride, 4u}),
		      "BlitterRaster::c2p sin plan cae a CPU");
	}

	if (failures == 0) {
		std::printf("OK: seam C2P (Rasterizer::c2p) valida chunky->planar.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
