// ============================================================================
// Test HOST-259: decodificacion MFM del disquete (eng/os/floppy.hpp).
// ============================================================================
//
// Construye una pista AmigaDOS sintetica con el MISMO layout que el encoder del emulador
// (WinUAE-DBG/disk.cpp:2185-2260) y comprueba que `floppy_find_sector` recupera cada sector:
// sync, cabecera (format/track/sector), y los 512 bytes de datos (layout dodd/deven).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/259_floppy_mfm

#include <cstdio>
#include <cstdint>

#include <eng/os/floppy.hpp>

using namespace eng;
using namespace eng::os;

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

/// Encoder MFM de un long: 4 words consecutivos (dodd_hi, dodd_lo, deven_hi, deven_lo).
void enc_long(u16* p, u32 v) {
	const u32 deven = v & 0x55555555u;
	const u32 dodd = (v >> 1u) & 0x55555555u;
	p[0] = static_cast<u16>(dodd >> 16u);
	p[1] = static_cast<u16>(dodd);
	p[2] = static_cast<u16>(deven >> 16u);
	p[3] = static_cast<u16>(deven);
}
void enc_dodd(u16* p, u32 v) {
	const u32 d = (v >> 1u) & 0x55555555u;
	p[0] = static_cast<u16>(d >> 16u);
	p[1] = static_cast<u16>(d);
}
void enc_deven(u16* p, u32 v) {
	const u32 d = v & 0x55555555u;
	p[0] = static_cast<u16>(d >> 16u);
	p[1] = static_cast<u16>(d);
}

/// Escribe un sector AmigaDOS en `t` (544 words). `data` = 512 bytes.
void build_sector(u16* t, u8 track, u8 sec, u8 to_gap, const u8* data) {
	t[0] = kMfmSync;
	t[1] = kMfmSync;
	const u32 hdr = (0xffu << 24u) | (static_cast<u32>(track) << 16u) |
			(static_cast<u32>(sec) << 8u) | to_gap;
	enc_long(t + 2u, hdr);
	for (u32 k = 0u; k < 4u; ++k) { // etiqueta de 16 B (a cero)
		enc_dodd(t + 6u + 2u * k, 0u);
		enc_deven(t + 14u + 2u * k, 0u);
	}
	enc_long(t + 22u, 0u); // hck
	enc_long(t + 26u, 0u); // dck
	for (u32 k = 0u; k < 128u; ++k) {
		const u32 v = (static_cast<u32>(data[4u * k]) << 24u) |
			      (static_cast<u32>(data[4u * k + 1u]) << 16u) |
			      (static_cast<u32>(data[4u * k + 2u]) << 8u) | data[4u * k + 3u];
		enc_dodd(t + 30u + 2u * k, v);
		enc_deven(t + 286u + 2u * k, v);
	}
}

/// Pista de 11 sectores con datos reconocibles (sector s -> byte = s*16 + i).
void build_track(u16* track, u8 tr) {
	for (u8 s = 0u; s < 11u; ++s) {
		u8 data[512];
		for (u32 i = 0u; i < 512u; ++i) {
			data[i] = static_cast<u8>((s * 16u + i) & 0xffu);
		}
		build_sector(track + static_cast<u32>(s) * kMfmWordsPerSector, tr, s,
			     static_cast<u8>(11u - s), data);
	}
}

void test_roundtrip() {
	u16 track[11u * kMfmWordsPerSector] {};
	build_track(track, 3u);

	for (u8 s = 0u; s < 11u; ++s) {
		u8 out[512] {};
		FloppySectorHeader hdr {};
		const bool ok = floppy_find_sector(Span<const u16> {track, 11u * kMfmWordsPerSector},
						   s, Span<u8> {out, 512u}, &hdr);
		check(ok, "sector encontrado");
		if (!ok) {
			continue;
		}
		check(hdr.format == 0xffu, "format = 0xFF (AmigaDOS)");
		check(hdr.track == 3u, "track = 3");
		check(hdr.sector == s, "sector correcto");
		check(hdr.to_gap == 11u - s, "to_gap correcto");
		bool data_ok = true;
		for (u32 i = 0u; i < 512u; ++i) {
			if (out[i] != static_cast<u8>((s * 16u + i) & 0xffu)) {
				data_ok = false;
				break;
			}
		}
		check(data_ok, "512 bytes de datos intactos");
	}
}

void test_decode_long() {
	// encode(v) -> (dodd_hi, dodd_lo, deven_hi, deven_lo); decode debe devolver v.
	const u32 vals[] = {0u, 0x55555555u, 0xffffffffu, 0x12345678u, 0xdeadbeefu};
	for (u32 v : vals) {
		const u32 deven = v & 0x55555555u;
		const u32 dodd = (v >> 1u) & 0x55555555u;
		check(mfm_decode_long(static_cast<u16>(dodd >> 16u), static_cast<u16>(dodd),
				      static_cast<u16>(deven >> 16u), static_cast<u16>(deven)) == v,
		      "encode/decode inverso");
	}
}

void test_negativos() {
	u16 track[11u * kMfmWordsPerSector] {};
	build_track(track, 0u);
	u8 out[512] {};

	check(!floppy_find_sector(Span<const u16> {track, 11u * kMfmWordsPerSector}, 11u,
				  Span<u8> {out, 512u}),
	      "sector inexistente -> false");

	u8 small[256] {};
	check(!floppy_find_sector(Span<const u16> {track, 11u * kMfmWordsPerSector}, 0u,
				  Span<u8> {small, 256u}),
	      "dst pequeno -> false");

	// Pista sin sync.
	u16 nosync[64] {};
	check(!floppy_find_sector(Span<const u16> {nosync, 64u}, 0u, Span<u8> {out, 512u}),
	      "sin sync -> false");
}

} // namespace

int main() {
	test_decode_long();
	test_roundtrip();
	test_negativos();

	if (failures == 0) {
		std::printf("OK: decodificacion MFM de pista AmigaDOS validada.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
