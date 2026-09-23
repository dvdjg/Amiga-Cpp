// ============================================================================
// Test HOST-132: rotozoom por píxel (eng/graphics/effects/rotozoom.hpp).
// ============================================================================
//
// Es un efecto de matemáticas puras (sin hardware), así que se valida aquí de forma
// determinista contra la verdad de campo:
//   - identidad (ángulo 0, zoom 1) reproduce la textura periódica 1:1;
//   - rotación 180 grados invierte las coordenadas;
//   - zoom 2x duplica el paso por píxel;
//   - la tabla de seno 16.16 es exacta en los cuartos.
// Todas las comprobaciones son aritmética entera (nada de floats en el hot path).
//
//   bash tools/run-host-tests.sh tests/host/132_rotozoom

#include <cstdio>
#include <vector>

#include <eng/api/effects.hpp>
#include <eng/graphics/effects/rotozoom.hpp>

namespace {

constexpr eng::u16 TW = 64;
constexpr eng::u16 TH = 64;

std::vector<eng::u8> make_texture() {
	std::vector<eng::u8> tex(TW * TH);
	for (eng::u32 r = 0; r < TW; ++r) {
		for (eng::u32 c = 0; c < TH; ++c) {
			tex[r * TH + c] = static_cast<eng::u8>((r + c) & 15u);
		}
	}
	return tex;
}

int fail(const char* what, eng::u32 x, eng::u32 y, eng::u8 got, eng::u8 want) {
	std::printf("[FAIL] %s en (%u,%u): got=%u want=%u\n", what, static_cast<unsigned>(x),
		    static_cast<unsigned>(y), static_cast<unsigned>(got), static_cast<unsigned>(want));
	return 1;
}

} // namespace

int main() {
	const std::vector<eng::u8> tex = make_texture();
	constexpr eng::u16 W = 160, H = 128;

	{ // Tabla de seno 16.16 exacta en los cuartos.
		using eng::graphics::rotozoom_detail::kSin16;
		if (kSin16[0] != 0 || kSin16[64] != 65536 || kSin16[128] != 0 || kSin16[192] != -65536) {
			std::printf("[FAIL] kSin16: %d %d %d %d\n", (int)kSin16[0], (int)kSin16[64],
				    (int)kSin16[128], (int)kSin16[192]);
			return 1;
		}
	}

	// --- Identidad: ángulo 0, zoom 1.0, offset = centro -> dst == textura periódica.
	{
		std::vector<eng::u8> dst(W * H, 0xee);
		const eng::graphics::Rotozoom r {
			0,
			65536,
			static_cast<eng::s32>(W / 2) << 16,
			static_cast<eng::s32>(H / 2) << 16,
		};
		eng::graphics::rotozoom_into<TW, TH>(eng::IndexedTexture(tex.data(), tex.size()), r,
						    eng::ChunkyBuffer(dst.data(), dst.size()), W, H);
		for (eng::u32 y = 0; y < H; ++y) {
			for (eng::u32 x = 0; x < W; ++x) {
				const eng::u8 want = tex[(x & (TW - 1u)) * TH + (y & (TH - 1u))];
				if (dst[y * W + x] != want) return fail("identidad", x, y, dst[y * W + x], want);
			}
		}
	}

	// --- Rotación 180 grados: r = -(dx,dy) -> u = cx - dx = 2cx - x.
	{
		std::vector<eng::u8> dst(W * H, 0);
		const eng::graphics::Rotozoom r {
			128,
			65536,
			static_cast<eng::s32>(W / 2) << 16,
			static_cast<eng::s32>(H / 2) << 16,
		};
		eng::graphics::rotozoom_into<TW, TH>(eng::IndexedTexture(tex.data(), tex.size()), r,
						    eng::ChunkyBuffer(dst.data(), dst.size()), W, H);
		for (eng::u32 y = 0; y < H; ++y) {
			for (eng::u32 x = 0; x < W; ++x) {
				const eng::u32 ux = (2u * static_cast<eng::u32>(W / 2) - x) & (TW - 1u);
				const eng::u32 vy = (2u * static_cast<eng::u32>(H / 2) - y) & (TH - 1u);
				const eng::u8 want = tex[ux * TH + vy];
				if (dst[y * W + x] != want) return fail("rot180", x, y, dst[y * W + x], want);
			}
		}
	}

	// --- Zoom 2x: u = cx + 2*(x-cx).
	{
		std::vector<eng::u8> dst(W * H, 0);
		const eng::graphics::Rotozoom r {
			0,
			131072,
			static_cast<eng::s32>(W / 2) << 16,
			static_cast<eng::s32>(H / 2) << 16,
		};
		eng::graphics::rotozoom_into<TW, TH>(eng::IndexedTexture(tex.data(), tex.size()), r,
						    eng::ChunkyBuffer(dst.data(), dst.size()), W, H);
		for (eng::u32 y = 0; y < H; ++y) {
			for (eng::u32 x = 0; x < W; ++x) {
				const eng::u32 tx = (static_cast<eng::u32>(W / 2 + 2 * (static_cast<eng::s32>(x) - static_cast<eng::s32>(W / 2)))) & (TW - 1u);
				const eng::u32 ty = (static_cast<eng::u32>(H / 2 + 2 * (static_cast<eng::s32>(y) - static_cast<eng::s32>(H / 2)))) & (TH - 1u);
				const eng::u8 want = tex[tx * TH + ty];
				if (dst[y * W + x] != want) return fail("zoom2x", x, y, dst[y * W + x], want);
			}
		}
	}

	{ // Envoltorio de API `effects::Rotozoom`: mismo resultado que la funcion directa.
		std::vector<eng::u8> dst_a(W * H, 0);
		std::vector<eng::u8> dst_b(W * H, 0);
		const eng::graphics::Rotozoom r {0, 65536, static_cast<eng::s32>(W / 2) << 16,
						 static_cast<eng::s32>(H / 2) << 16};
		eng::effects::Rotozoom fx;
		fx.configure(r);
		fx.render<TW, TH>(eng::IndexedTexture(tex.data(), tex.size()),
				  eng::ChunkyBuffer(dst_a.data(), dst_a.size()), W, H);
		eng::graphics::rotozoom_into<TW, TH>(eng::IndexedTexture(tex.data(), tex.size()), r,
						    eng::ChunkyBuffer(dst_b.data(), dst_b.size()), W, H);
		if (dst_a != dst_b) {
			std::printf("[FAIL] effects::Rotozoom != rotozoom_into\n");
			return 1;
		}
	}

	std::printf("OK: rotozoom (identidad, rot180, zoom2x, tabla 16.16).\n");
	return 0;
}
