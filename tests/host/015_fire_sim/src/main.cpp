// Test host del FUEGO de fire-rgb (modelo C fiel de MainLoop/RandomizeBottom/fastrand).
// Sirve para localizar si el bug esta en la simulacion o en el C2P, sin WinUAE.
#include <cstdint>
#include <cstdio>
#include <cstring>

static constexpr int W = 80;
static constexpr int H = 64;

#include "dualtab.c" // static uint32_t dualtab[256]
#include "../../../../demos/amiga/080_fire_rgb/src/data/dualtab.hpp" // fire_rgb::kDualTab

alignas(4) static int16_t fire[W * H];
alignas(4) static uint8_t chunky[20480];

// fastrand: a = m[0]; b = swap(m[1]); m[1] += a; m[0] += b; return a.
static int fastrand() {
	static uint32_t m[2] = {0x3E50B28Cu, 0xD461A7F9u};
	const uint32_t a = m[0];
	uint32_t b = m[1];
	b = (b << 16) | (b >> 16);
	m[1] = m[1] + a;
	m[0] = m[0] + b;
	return static_cast<int>(a);
}

static void randomize_bottom() {
	int r;
	int16_t* bufPtr = &fire[W * H - 1];
	for (int i = 1; i <= W * 2; i += 5) {
		r = fastrand();
		*bufPtr-- = static_cast<int16_t>((r & 0x3F) * 4); r >>= 6;
		*bufPtr-- = static_cast<int16_t>((r & 0x3F) * 4); r >>= 6;
		*bufPtr-- = static_cast<int16_t>((r & 0x3F) * 4); r >>= 6;
		*bufPtr-- = static_cast<int16_t>((r & 0x3F) * 4); r >>= 6;
		*bufPtr-- = static_cast<int16_t>((r & 0x3F) * 4);
	}
}

// Modelo big-endian explicito: palabra en el short `s` = (fire[s]<<16)|fire[s+1]
// (como en m68k). Los punteros avanzan 2 shorts por iteracion (1 longword).
static uint32_t LDW(int s) {
	return (static_cast<uint32_t>(static_cast<uint16_t>(fire[s])) << 16) |
	       static_cast<uint16_t>(fire[s + 1]);
}
static void STW(int s, uint32_t v) {
	fire[s] = static_cast<int16_t>(v >> 16);
	fire[s + 1] = static_cast<int16_t>(v & 0xFFFFu);
}

static void main_loop() {
	uint16_t* chunkyPtr = reinterpret_cast<uint16_t*>(chunky);
	int sA = 0;
	int sB = W - 1;
	int sC = W;
	int sD = W + 1;
	int sE = W * 2;

	for (int i = 0; i < (W * H - 2 * W) / 8; ++i) {
		for (int n = 0; n < 4; ++n) {
			const uint32_t vl = LDW(sE) + LDW(sB) + LDW(sD) + LDW(sC);
			// DIAG: indice = media de los 4 vecinos (suma>>2). El original indexa
			// con (int16)vl (la suma); probamos la media por si acota a 0..255.
			const uint32_t lo = dualtab[(static_cast<uint16_t>(vl) >> 2) & 0xFFu];
			const uint32_t hi = dualtab[(static_cast<uint16_t>(vl >> 16) >> 2) & 0xFFu];
			*chunkyPtr++ = static_cast<uint16_t>(hi);
			*chunkyPtr++ = static_cast<uint16_t>(lo);
			const uint32_t hifb = (hi & 0xFFFF0000u) | ((lo >> 16) & 0xFFFFu);
			STW(sA, hifb);
			sA += 2; sB += 2; sC += 2; sD += 2; sE += 2;
		}
	}
}

int main() {
	std::memset(fire, 0, sizeof(fire));
	std::memset(chunky, 0, sizeof(chunky));

	for (int f = 0; f < 400; ++f) {
		randomize_bottom();
		main_loop();
	}

	long sum = 0;
	int mn = 99999, mx = -1, nz = 0;
	for (int i = 0; i < W * H; ++i) {
		const int v = fire[i];
		sum += v;
		if (v < mn) mn = v;
		if (v > mx) mx = v;
		if (v != 0) ++nz;
	}
	std::printf("fire: sum=%ld min=%d max=%d nz=%d/%d\n", sum, mn, mx, nz, W * H);

	// Ascii (arriba = indice bajo). El fuego debe estar caliente ABAJO.
	std::printf("fire (arriba->abajo):\n");
	for (int r = 0; r < H; r += 2) {
		for (int c = 0; c < W; ++c) {
			const int v = fire[r * W + c] < 0 ? 0 : fire[r * W + c];
			const char* ramp = " .:-=+*#%@";
			int idx = v / 28;
			if (idx > 9) idx = 9;
			std::putchar(ramp[idx]);
		}
		std::putchar('\n');
	}
	// Chunky (C2P input): primeras palabras.
	std::printf("chunky[0..7]:");
	for (int i = 0; i < 8; ++i) std::printf(" %04x", reinterpret_cast<uint16_t*>(chunky)[i]);
	std::putchar('\n');

	const int top_hot = fire[0] + fire[1] + fire[W];
	const int bottom_hot = fire[W * H - 1] + fire[W * H - 2] + fire[W * H - W - 1];
	const bool ok = mx > 0 && nz > W && bottom_hot > top_hot;
	std::printf("%s: fuego %s (bottom_hot=%d top_hot=%d)\n", ok ? "OK" : "FAIL",
		    ok ? "generado (abajo caliente)" : "NO valido", bottom_hot, top_hot);

	// La tabla C++23 constexpr debe ser identica a la generada por gen-dualtab.py.
	int tab_bad = 0;
	for (int i = 0; i < 256; ++i) {
		if (fire_rgb::kDualTab.v[i] != dualtab[i]) ++tab_bad;
	}
	std::printf("%s: dualtab C++23 constexpr vs original (%d diferencias)\n",
		    tab_bad == 0 ? "OK" : "FAIL", tab_bad);
	return (ok && tab_bad == 0) ? 0 : 1;
}
