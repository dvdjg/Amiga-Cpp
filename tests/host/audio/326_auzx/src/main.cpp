// ============================================================================
// Test HOST-326: contenedor AUZX (cabecera + indice de chunks).
// ============================================================================
//
// Valida el parser `eng::audio::auzx` (cabecera fija de 32 bytes + indice de chunks de 8).
// Se construye un fichero sintetico con dos chunks, se parsea y se accede a los payloads; y se
// comprueban los rechazos (buffer corto, magic, version, canales, indice fuera de rango).
//
//   bash tools/run-host-tests.sh tests/host/audio/326_auzx

#include <cstdio>

#include <eng/audio/auzx.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

// Construye un AUZX de 2 chunks (payloads {0x11,0x22,0x33} y {0x44,0x55}) en `f`.
eng::usize build(eng::u8* f, eng::usize cap) {
	const eng::usize hdr = eng::audio::auzx::kHeaderSize;
	const eng::usize entries = 2u * eng::audio::auzx::kChunkEntrySize;
	const eng::usize data = hdr + entries;
	const eng::usize total = data + 3u + 2u;
	if (total > cap) {
		return 0u;
	}
	const auto w = [&](eng::usize o, eng::u8 v) { f[o] = v; };
	const auto w16 = [&](eng::usize o, eng::u16 v) { eng::audio::auzx::wr16(eng::Span<eng::u8>(f, cap), o, v); };
	const auto w32 = [&](eng::usize o, eng::u32 v) { eng::audio::auzx::wr32(eng::Span<eng::u8>(f, cap), o, v); };
	w(0, 'A'); w(1, 'U'); w(2, 'Z'); w(3, 'X');
	w(4, 1u);          // version
	w(5, 2u);          // compression = DeltaRle
	w16(6, 8000u);     // sample_rate
	w16(8, 1u);        // channels
	w(10, 8u);         // bits
	w(11, 0u);
	w32(12, 5120u);    // total_samples
	w16(16, 4096u);    // chunk_samples
	w16(18, 2u);       // num_chunks
	w32(20, static_cast<eng::u32>(hdr));  // table_offset
	w32(24, static_cast<eng::u32>(data)); // data_offset
	w32(28, 0u);       // checksum
	// indice
	w32(hdr + 0u, static_cast<eng::u32>(data));
	w32(hdr + 4u, 3u);
	w32(hdr + 8u, static_cast<eng::u32>(data + 3u));
	w32(hdr + 12u, 2u);
	// payloads
	f[data + 0u] = 0x11u;
	f[data + 1u] = 0x22u;
	f[data + 2u] = 0x33u;
	f[data + 3u] = 0x44u;
	f[data + 4u] = 0x55u;
	return total;
}

void test_parse_and_chunks() {
	eng::u8 file[128] {};
	const eng::usize n = build(file, sizeof(file));
	check(n == 32u + 16u + 5u, "auzx: fichero sintetico de 53 bytes");
	eng::audio::auzx::Header h {};
	check(eng::audio::auzx::parse(eng::Span<const eng::u8>(file, n), h), "auzx: parse ok");
	check(h.compression == 2u && h.sample_rate == 8000u && h.channels == 1u && h.num_chunks == 2u,
	      "auzx: campos de la cabecera");
	check(h.total_samples == 5120u && h.chunk_samples == 4096u, "auzx: tamanos");

	eng::u32 sz = 0u;
	const auto c0 = eng::audio::auzx::chunk(eng::Span<const eng::u8>(file, n), h, 0u, sz);
	check(sz == 3u && c0.size() == 3u && c0[0] == 0x11u && c0[2] == 0x33u, "auzx: chunk 0");
	const auto c1 = eng::audio::auzx::chunk(eng::Span<const eng::u8>(file, n), h, 1u, sz);
	check(sz == 2u && c1[1] == 0x55u, "auzx: chunk 1");
	const auto cx = eng::audio::auzx::chunk(eng::Span<const eng::u8>(file, n), h, 9u, sz);
	check(sz == 0u && cx.size() == 0u, "auzx: chunk fuera de rango");
}

void test_rejections() {
	eng::audio::auzx::Header h {};
	check(!eng::audio::auzx::parse(eng::Span<const eng::u8>(nullptr, 0u), h), "auzx: buffer vacio");

	eng::u8 bad[64] {};
	build(bad, sizeof(bad));
	bad[0] = 'X'; // magic roto
	check(!eng::audio::auzx::parse(eng::Span<const eng::u8>(bad, 53u), h), "auzx: magic");

	build(bad, sizeof(bad));
	bad[4] = 9u; // version
	check(!eng::audio::auzx::parse(eng::Span<const eng::u8>(bad, 53u), h), "auzx: version");

	build(bad, sizeof(bad));
	eng::audio::auzx::wr16(eng::Span<eng::u8>(bad, sizeof(bad)), 8u, 2u); // canales
	check(!eng::audio::auzx::parse(eng::Span<const eng::u8>(bad, 53u), h), "auzx: canales");

	eng::u8 trunc[40] {};
	build(trunc, sizeof(trunc)); // 53 > 40: el build corta en data (40) -> indice fuera
	eng::audio::auzx::Header h2 {};
	check(!eng::audio::auzx::parse(eng::Span<const eng::u8>(trunc, 40u), h2),
	      "auzx: indice fuera del fichero");
}

} // namespace

int main() {
	test_parse_and_chunks();
	test_rejections();
	if (failures == 0) {
		std::printf("OK: contenedor AUZX (cabecera, indice de chunks y rechazos).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
