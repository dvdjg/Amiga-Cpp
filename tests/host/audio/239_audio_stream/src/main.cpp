// ============================================================================
// Test HOST-239: PcmStream (A5) — doble buffer, underrun y EOF con E/S simulada.
// ============================================================================
//
// Ejercita `eng::audio::PcmStream<2>` uniendo la maquina de estados de buffers (`ChunkStream`) con
// el codec real (`pcm_codec::encode`/`decode`, Delta+RLE). La "E/S" se simula: el test lee el
// chunk de su tabla y lo entrega con `provide`, como haria la tarea de fondo del mini-SO.
// Ver docs/guides/roadmap/ROADMAP_AUDIO.md (A5) y docs/engine/architecture/AUDIO_STREAMING.md §3-5.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/audio/239_audio_stream

#include <cstdio>

#include <eng/audio/pcm_stream.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

constexpr int kChunk = 8;
constexpr int kChunks = 4;

// PCM de 4 chunks de 8 muestras (tramos planos para ejercitar el RLE del codec).
const eng::u8 kPcm[kChunk * kChunks] = {
	0x00, 0x00, 0x00, 0x00, 0x10, 0x20, 0x30, 0x40, //
	0x40, 0x40, 0x40, 0x40, 0x50, 0x60, 0x70, 0x80, //
	0x80, 0x80, 0x80, 0x80, 0x81, 0x82, 0x83, 0x84, //
	0x84, 0x84, 0x84, 0x84, 0x85, 0x86, 0x87, 0x88, //
};

eng::u8 g_comp[kChunks][64] {};
int g_comp_n[kChunks] = {};

void encode_chunks() {
	for (int c = 0; c < kChunks; ++c) {
		g_comp_n[c] = static_cast<int>(eng::audio::pcm_codec::encode(
			eng::Span<const eng::u8>(kPcm + c * kChunk, kChunk),
			eng::Span<eng::u8>(g_comp[c], sizeof(g_comp[c]))));
		check(g_comp_n[c] > 0, "encode chunk");
	}
}

[[nodiscard]] bool pcm_equals(const eng::u8* got, int chunk) {
	for (int i = 0; i < kChunk; ++i) {
		if (got[i] != kPcm[chunk * kChunk + i]) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] eng::Span<const eng::u8> chunk(int c) {
	return eng::Span<const eng::u8>(g_comp[c], static_cast<eng::usize>(g_comp_n[c]));
}

} // namespace

int main() {
	encode_chunks();

	eng::u8 buf0[kChunk] {};
	eng::u8 buf1[kChunk] {};
	eng::Span<eng::u8> bufs[2] = {eng::Span<eng::u8>(buf0, kChunk), eng::Span<eng::u8>(buf1, kChunk)};

	const eng::audio::PcmStream<2>::Config cfg {
		16000u, kChunk, kChunks,
		static_cast<eng::u8>(eng::audio::pcm_codec::Codec::DeltaRle)};

	// --- Flujo normal: doble buffer, sin underrun, hasta EOF. ---
	eng::audio::PcmStream<2> s {};
	s.begin(cfg, bufs);
	check(s.free_mask() == 0b11u, "begin: ambos buffers libres");
	check(s.needs_data(), "begin: hay trabajo");
	check(s.next_chunk() == 0, "begin: next_chunk 0");

	check(s.provide(0, chunk(0)), "provide chunk 0 en buffer 0");
	check(s.provide(1, chunk(1)), "provide chunk 1 en buffer 1");
	check(!s.needs_data(), "con 2 buffers cargados no hay trabajo");
	check(s.play_index() == 0, "suena el buffer 0");
	check(pcm_equals(s.play_pcm(), 0), "buffer 0 == chunk 0 (decodificado)");

	check(s.advance(), "IRQ: buffer 1 listo");
	check(s.play_index() == 1, "suena el buffer 1");
	check(!s.underrun(), "sin underrun");

	check(s.first_free() == 0, "primer libre: 0");
	check(s.next_chunk() == 2, "next_chunk 2");
	check(s.provide(0, chunk(2)), "provide chunk 2 en buffer 0");

	check(s.advance(), "IRQ: buffer 0 listo");
	check(s.play_index() == 0, "suena el buffer 0 (chunk 2)");
	check(pcm_equals(s.play_pcm(), 2), "buffer 0 == chunk 2");

	check(s.first_free() == 1, "primer libre: 1");
	check(s.provide(1, chunk(3)), "provide chunk 3 en buffer 1");
	check(s.eof(), "EOF tras el ultimo chunk");
	check(s.free_mask() == 0u, "EOF: no se piden mas chunks");

	check(s.advance(), "IRQ: buffer 1 listo (chunk 3)");
	check(pcm_equals(s.play_pcm(), 3), "buffer 1 == chunk 3");

	check(!s.advance(), "IRQ al final: no hay siguiente");
	check(s.at_end(), "at_end: fin normal, no underrun real");
	check(s.finished(), "finished: stream agotado");

	// --- Underrun real: la IRQ pide un buffer que nunca llego. ---
	eng::audio::PcmStream<2> u {};
	u.begin(cfg, bufs);
	check(u.provide(0, chunk(0)), "underrun: solo el primer chunk");
	check(!u.advance(), "underrun: la IRQ no encuentra datos");
	check(u.underrun(), "underrun detectado");
	check(!u.eof(), "underrun: aun queda stream");
	check(!u.at_end(), "underrun: no es fin normal");

	// --- provide con un flujo que no da chunk_samples: se rechaza sin tocar el buffer. ---
	const eng::u8 bad[2] = {0x00, 0x00};
	check(!u.provide(1, eng::Span<const eng::u8>(bad, 2)), "provide tamano incorrecto -> false");

	if (failures == 0) {
		std::printf("OK: PcmStream (doble buffer, underrun, EOF) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
