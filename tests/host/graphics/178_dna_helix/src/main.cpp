// ============================================================================
// Test HOST-178: generador de doble helice de `dna3d` (port a `Turns`).
// ============================================================================
//
// Porta `GenCircularDoubleHelix` de `effects/dna3d/dna3d.c` al vocabulario del engine:
// el angulo-índice del original (`SIN(a)=sintab[a&0xfff]`) pasa a `sin(turns(idx))`
// (`Turns`, tabla 4.12 exacta). La referencia usa `kSinTab` directo (el original) y se
// compara punto a punto con el port, para cazar errores de shifts/swap/negacion.
//
//   bash tools/run-host-tests.sh tests/host/graphics/178_dna_helix

#include <eng/core/types/types.hpp>
#include <eng/retro/fixed_trig.hpp>

#include <cstdio>

namespace {

using eng::s16;
using eng::s32;
using eng::u16;

struct Pt {
	s16 x;
	s16 y;
	s16 z;
};

constexpr int kPointsPerTurn = 10;
constexpr int kTurns = 4;
constexpr int kNPoints = kPointsPerTurn * kTurns; // 40
constexpr s16 kRadius = 10240;                    // fx12f(2.5) = 2.5 * 4096

/// `swap16(a*b)` del original: alto de 16 bits del producto de dos `s16` (68000 `swap`).
[[nodiscard]] constexpr s16 swap_hi(s16 a, s16 b) {
	return static_cast<s16>((static_cast<s32>(a) * static_cast<s32>(b)) >> 16);
}

/// Referencia: transcripcion 1:1 del original usando la tabla directa.
void reference_gen(Pt* out, s16 phi_offset) {
	for (int i = 0; i < kNPoints; ++i) {
		const u16 alpha = static_cast<u16>((1638 * i) & 0xFFFF); // 65536/40 = 1638
		const s16 theta = static_cast<s16>(alpha >> 4);
		const s16 phi = static_cast<s16>(alpha / kTurns + phi_offset);
		const u16 ti = static_cast<u16>(theta);
		const u16 pi = static_cast<u16>(phi);
		s16 st = eng::retro::kSinTab[ti & 4095u];
		s16 ct = eng::retro::kSinTab[(ti + 1024u) & 4095u];
		s16 sp = static_cast<s16>(eng::retro::kSinTab[pi & 4095u] >> 1);
		s16 cp = static_cast<s16>(eng::retro::kSinTab[(pi + 1024u) & 4095u] >> 1);
		st = static_cast<s16>(st + st);
		ct = static_cast<s16>(ct + ct);
		out[2 * i + 0] = Pt {swap_hi(static_cast<s16>(kRadius + cp), ct),
				     swap_hi(static_cast<s16>(kRadius + sp), st),
				     static_cast<s16>(sp >> 3)};
		const s16 t = cp;
		cp = static_cast<s16>(-sp);
		sp = t;
		out[2 * i + 1] = Pt {swap_hi(static_cast<s16>(kRadius + cp), ct),
				     swap_hi(static_cast<s16>(kRadius + sp), st),
				     static_cast<s16>(sp >> 3)};
	}
}

/// Port: el mismo algoritmo con el vocabulario tipado (`turns`/`sin`/`cos`).
void ported_gen(Pt* out, s16 phi_offset) {
	using eng::retro::cos;
	using eng::retro::sin;
	using eng::retro::turns;
	for (int i = 0; i < kNPoints; ++i) {
		const u16 alpha = static_cast<u16>((1638 * i) & 0xFFFF);
		const s16 theta = static_cast<s16>(alpha >> 4);
		const s16 phi = static_cast<s16>(alpha / kTurns + phi_offset);
		s16 st = sin(turns(static_cast<u16>(theta))).v;
		s16 ct = cos(turns(static_cast<u16>(theta))).v;
		s16 sp = static_cast<s16>(sin(turns(static_cast<u16>(phi))).v >> 1);
		s16 cp = static_cast<s16>(cos(turns(static_cast<u16>(phi))).v >> 1);
		st = static_cast<s16>(st + st);
		ct = static_cast<s16>(ct + ct);
		out[2 * i + 0] = Pt {swap_hi(static_cast<s16>(kRadius + cp), ct),
				     swap_hi(static_cast<s16>(kRadius + sp), st),
				     static_cast<s16>(sp >> 3)};
		const s16 t = cp;
		cp = static_cast<s16>(-sp);
		sp = t;
		out[2 * i + 1] = Pt {swap_hi(static_cast<s16>(kRadius + cp), ct),
				     swap_hi(static_cast<s16>(kRadius + sp), st),
				     static_cast<s16>(sp >> 3)};
	}
}

} // namespace

int main() {
	int fails = 0;

	// 1) El port coincide con la transcripcion del original para varias fases.
	for (s16 phi = -2048; phi <= 2048; phi += 256) {
		Pt ref[2 * kNPoints] {};
		Pt got[2 * kNPoints] {};
		reference_gen(ref, phi);
		ported_gen(got, phi);
		for (int i = 0; i < 2 * kNPoints; ++i) {
			if (ref[i].x != got[i].x || ref[i].y != got[i].y || ref[i].z != got[i].z) {
				if (fails < 6) {
					std::printf("[FAIL] phi=%d i=%d ref=(%d,%d,%d) got=(%d,%d,%d)\n", phi, i,
						    ref[i].x, ref[i].y, ref[i].z, got[i].x, got[i].y, got[i].z);
				}
				++fails;
			}
		}
	}
	if (fails) {
		std::printf("[FAIL] HOST-178: %d discrepancias port vs original\n", fails);
		return 1;
	}

	// 2) Invariantes de la geometria (la helice cabe en su caja).
	Pt pts[2 * kNPoints] {};
	ported_gen(pts, 0);
	s16 maxz = 0;
	for (int i = 0; i < 2 * kNPoints; ++i) {
		if (pts[i].z > maxz) maxz = pts[i].z;
		const s16 az = static_cast<s16>(pts[i].z < 0 ? -pts[i].z : pts[i].z);
		if (az > maxz) maxz = az; // |z| maximo
	}
	// |z| = |sin_phi| >> 3 <= (4096/2) >> 3 = 256.
	if (maxz <= 0 || maxz > 256) {
		std::printf("[FAIL] |z| fuera de rango: %d\n", maxz);
		return 1;
	}

	std::printf("OK: dna3d GenCircularDoubleHelix portado (Turns) == original; geometria acotada.\n");
	return 0;
}
