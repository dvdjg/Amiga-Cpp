// ============================================================================
// Test HOST-306: M8 — alimentador fichero -> ChunkStream (`FileChunkFeeder`).
// ============================================================================
//
// Valida el puente `eng/os/file_stream.hpp` con una lectura FAKE (síncrona): lanza lecturas
// secuenciales para los buffers vacíos, marca el buffer listo al `on_done`, detecta EOF (lectura
// corta / offset agotado) y no re-lanza una lectura ya en curso. El `ReadFn` se inyecta, igual que
// el backend inyecta `file_read_async`.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/306_os_file_stream

#include <cstdio>
#include <cstring>
#include <vector>

#include <eng/os/file_stream.hpp>

namespace {

int g_fail = 0;
void check(bool ok, const char* m) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", m);
		++g_fail;
	}
}

// Fuente fake: provee `read_async` (como hará el backend envolviendo `file_read_async`).
struct FakeSource {
	std::vector<eng::u8> data;
	bool read_async(eng::u8 /*idx*/, eng::u32 offset, eng::u8* dst, eng::u32 bytes) {
		if (offset >= data.size()) {
			return false;
		}
		std::memcpy(dst, data.data() + offset, bytes);
		return true;
	}
};

} // namespace

int main() {
	std::printf("== HOST-306 os file stream ==\n");

	constexpr eng::u32 kChunk = 8u;
	constexpr eng::u8 kBufs = 2u;

	// Fichero de 3 chunks y medio (28 B): lecturas cortas -> EOF en el ultimo.
	FakeSource source;
	source.data.resize(3u * kChunk + 4u);
	for (eng::u32 i = 0; i < source.data.size(); ++i) {
		source.data[i] = static_cast<eng::u8>(i + 1u);
	}

	eng::u8 buffers[kBufs * kChunk] {};
	eng::os::ChunkStream<kBufs> stream;
	eng::os::FileChunkFeeder<kBufs, FakeSource> feeder;
	feeder.init(stream, eng::Span<eng::u8>(buffers, sizeof(buffers)), kChunk,
		    static_cast<eng::u32>(source.data.size()), source);

	// Arranque: lanza las 2 lecturas (offsets 0 y 8).
	feeder.pump();
	check(feeder.in_flight(0u) && feeder.in_flight(1u), "pump: 2 lecturas en curso");
	check(feeder.next_offset() == 2u * kChunk, "next_offset = 16");
	check(!stream.play_ready(), "sin datos aun");

	// Llegan los chunks 0 y 1; pump no re-lanza el 1 (en curso).
	check(feeder.on_done(0u, kChunk), "on_done(0)");
	feeder.pump();
	check(!feeder.in_flight(0u) && feeder.in_flight(1u), "pump no re-lanza el 1 (en curso)");
	check(feeder.on_done(1u, kChunk), "on_done(1)");
	check(stream.play_ready() && stream.play_index() == 0u, "buffer 0 listo");

	// Los datos coinciden con el fichero: buffer 0 = bytes 1..8, buffer 1 = 9..16.
	check(buffers[0] == 1u && buffers[kChunk] == static_cast<eng::u8>(kChunk + 1u),
	      "contenido de los chunks 0/1 en sus buffers");

	// La reproduccion consume el 0 -> pump lanza el chunk 2 (offset 16) en el buffer 0.
	check(stream.advance(), "advance a 1 (listo)");
	feeder.pump();
	check(feeder.in_flight(0u) && feeder.next_offset() == 3u * kChunk, "pump lanza el chunk 2");
	check(feeder.on_done(0u, kChunk), "on_done(2)");

	// Avanza al 0 (chunk 2), consume, pump lanza el ultimo (offset 24, 4 B -> EOF corto).
	check(stream.advance(), "advance a 0 (listo)");
	feeder.pump();
	check(feeder.on_done(1u, 4u) && stream.eof(), "ultimo chunk corto -> EOF");

	// Consume el chunk 2 (buf0) y el ultimo (buf1); al vaciarse con eof -> finished.
	check(stream.advance(), "advance a 1 (ultimo chunk)");
	(void)stream.advance(); // play 1 -> 0, sin datos: underrun de fin
	check(stream.finished(), "stream finished (eof + vacio)");

	// Underrun: advance sin el siguiente listo.
	{
		eng::os::ChunkStream<2> s2;
		check(!s2.advance() && s2.underrun(), "advance sin datos -> underrun");
	}

	// on_done con idx invalido / sin lectura en curso.
	check(!feeder.on_done(5u, kChunk), "on_done idx invalido -> false");

	if (g_fail == 0) {
		std::printf("OK: feeder fichero->ChunkStream (secuencial, EOF, underrun) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es)\n", g_fail);
	return 1;
}
