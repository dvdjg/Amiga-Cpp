// ============================================================================
// Test HOST-123: consumidor real de bitstream y dynamic_bitset
// ============================================================================
//
// No prueba las APIs en aislamiento (eso es HOST-121/122), sino para que se usan:
//
//   1) `bitstream`: empaquetar un nivel (cabecera + id de tile de 4 bits + flag de
//      solido de 1 bit por celda) y leerlo de vuelta, comprobando el 100 %.
//   2) `dynamic_bitset`: set de tiles sucios de un mapa grande (4096 tiles) en una
//      arena, marcando/limpiando regiones.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/123_packed_level

#include <cstdio>

#include <eng/core/util/bitstream.hpp>
#include <eng/core/util/dynamic_bitset.hpp>

namespace {

using eng::u8;
using eng::u16;
using eng::u32;
using eng::usize;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

// ---------------------------------------------------------------------------
// 1) Nivel empaquetado con BitWriter/BitReader.
// ---------------------------------------------------------------------------
constexpr usize kW = 8;
constexpr usize kH = 8;
constexpr usize kN = kW * kH;

struct Level {
	u8 w = 0;
	u8 h = 0;
	u8 tiles[kN] {};
	u8 solid[kN] {};
};

/// Formato: 6 bits ancho + 6 bits alto + N x (4 bits id + 1 bit solido).
[[nodiscard]] usize pack(const Level& lv, u8* out, usize cap) {
	eng::util::BitWriter bw {eng::Span<u8> {out, cap}};
	if (!bw.write(lv.w, 6u) || !bw.write(lv.h, 6u)) {
		return 0u;
	}
	for (usize i = 0; i < kN; ++i) {
		if (!bw.write(lv.tiles[i], 4u) || !bw.write_bool(lv.solid[i] != 0u)) {
			return 0u;
		}
	}
	return bw.byte_count();
}

[[nodiscard]] bool unpack(const u8* data, usize nbytes, Level& lv) {
	eng::util::BitReader br {eng::Span<const u8> {data, nbytes}};
	u32 w = 0;
	u32 h = 0;
	if (!br.read(6u, w) || !br.read(6u, h)) {
		return false;
	}
	lv.w = static_cast<u8>(w);
	lv.h = static_cast<u8>(h);
	for (usize i = 0; i < kN; ++i) {
		u32 tile = 0;
		bool solid = false;
		if (!br.read(4u, tile) || !br.read_bool(solid)) {
			return false;
		}
		lv.tiles[i] = static_cast<u8>(tile);
		lv.solid[i] = solid ? 1u : 0u;
	}
	return true;
}

void test_packed_level() {
	Level src;
	src.w = static_cast<u8>(kW);
	src.h = static_cast<u8>(kH);
	for (usize i = 0; i < kN; ++i) {
		src.tiles[i] = static_cast<u8>(i % 16u); // 4 bits
		src.solid[i] = static_cast<u8>((i % 3u) == 0u) ? 1u : 0u;
	}

	u8 buf[64] {};
	const usize nbytes = pack(src, buf, sizeof(buf));
	check(nbytes != 0u, "nivel: empaqueta");
	// 12 + 64*5 = 332 bits -> 42 bytes; el formato ingenuo (id+flag por byte) seria 128.
	check(nbytes == 42u, "nivel: tamano empaquetado (42 B)");
	check(nbytes < 48u, "nivel: mejor que almacenar un byte por celda");

	Level dst;
	check(unpack(buf, nbytes, dst), "nivel: desempaqueta");
	check(dst.w == src.w && dst.h == src.h, "nivel: cabecera intacta");
	bool same = true;
	for (usize i = 0; i < kN; ++i) {
		if (dst.tiles[i] != src.tiles[i] || dst.solid[i] != src.solid[i]) {
			same = false;
		}
	}
	check(same, "nivel: round-trip 100%");
}

// ---------------------------------------------------------------------------
// 2) Set de tiles sucios de un mapa grande con DynamicBitSet.
// ---------------------------------------------------------------------------
void test_dirty_tiles() {
	constexpr usize kTiles = 4096;
	eng::util::InlineAlloc<512> arena; // 512 B = 4096 bits
	eng::util::DynamicBitSet<eng::util::InlineAlloc<512>> dirty {arena};
	check(dirty.init(kTiles), "sucio: reserva 4096 bits");
	check(dirty.none(), "sucio: arranca limpio");

	// Solo se marcan las celdas que cambian (un cambio real no toca el resto).
	const usize changed[5] = {0u, 100u, 1234u, 2048u, 4095u};
	for (usize i = 0; i < 5u; ++i) {
		dirty.set(changed[i]);
	}
	check(dirty.count() == 5u, "sucio: solo las 5 celdas cambiadas");
	check(dirty.test(4095u) && !dirty.test(1u), "sucio: test por celda");

	// Limpiar una region.
	for (usize i = 2048u; i < 2052u; ++i) {
		dirty.reset(i);
	}
	check(dirty.count() == 4u, "sucio: la region limpiada deja de estar sucia");
	check(dirty.any(), "sucio: quedan celdas pendientes");
}

} // namespace

int main() {
	std::printf("BitsConsumer:\n");
	test_packed_level();
	test_dirty_tiles();

	if (g_fail == 0u) {
		std::printf("OK: consumidores (nivel empaquetado y set de tiles sucios)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
