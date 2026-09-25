// ============================================================================
// pack-pcm: empaqueta PCM mono 8-bit con signo en un contenedor AUZX.
// ============================================================================
//
// Herramienta de PC que produce ficheros **compatibles con el engine**: usa los mismos
// codificadores (`eng::audio::pcm_codec::encode`) y el mismo layout de contenedor
// (`eng::audio::auzx`) que consume el Amiga, de modo que no hay deriva de formato.
//
// Uso:
//   pack-pcm <in.raw> <out.auzx> [codec] [sample_rate] [chunk_samples]
//     codec: none | rle | fib | ima      (por defecto rle)
//     sample_rate: 8000/11025/16000/22050 (por defecto 8000)
//     chunk_samples: potencia de 2 (por defecto 4096)
//
// Fuente: `tools/audio/prep-sample.ts` (WAV -> PCM mono 8-bit con signo) o cualquier `.raw`
// de 1 byte/muestra. Para Delta+ZX0/ZX0, comprimir aparte con la herramienta de referencia
// `zx0 -f` tras el paso delta (ver AUDIO_STREAMING.md §7.1).
//
// Compilar (host):
//   g++ -std=gnu++23 -Iengine/include host-tools/pack-pcm/pack-pcm.cpp -o pack-pcm
//
// Verificacion: el programa, tras escribir, RELEE el fichero, lo decodifica con
// `pcm_codec::decode` y comprueba el round-trip byte a byte (informa OK/FALLO).

#include <eng/audio/auzx.hpp>
#include <eng/audio/pcm_codec.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

eng::u8 codec_id(const char* name) {
	using eng::audio::pcm_codec::Codec;
	if (std::strcmp(name, "none") == 0) {
		return static_cast<eng::u8>(Codec::None);
	}
	if (std::strcmp(name, "rle") == 0) {
		return static_cast<eng::u8>(Codec::DeltaRle);
	}
	if (std::strcmp(name, "fib") == 0) {
		return static_cast<eng::u8>(Codec::FibDelta);
	}
	if (std::strcmp(name, "ima") == 0) {
		return static_cast<eng::u8>(Codec::ImaAdpcm);
	}
	return 0xffu;
}

} // namespace

int main(int argc, char** argv) {
	if (argc < 3) {
		std::fprintf(stderr,
			     "uso: pack-pcm <in.raw> <out.auzx> [none|rle|fib|ima] [rate] [chunk]\n");
		return 2;
	}
	const char* in_path = argv[1];
	const char* out_path = argv[2];
	const eng::u8 codec = (argc > 3) ? codec_id(argv[3]) : 0u;
	if (argc > 3 && codec == 0xffu) {
		std::fprintf(stderr, "codec desconocido: %s\n", argv[3]);
		return 2;
	}
	const eng::u16 rate = static_cast<eng::u16>((argc > 4) ? std::atoi(argv[4]) : 8000);
	const eng::u16 chunk = static_cast<eng::u16>((argc > 5) ? std::atoi(argv[5]) : 4096);
	const eng::u8 comp = (argc > 3) ? codec : static_cast<eng::u8>(eng::audio::pcm_codec::Codec::DeltaRle);

	// Lee el PCM de entrada.
	std::FILE* fin = std::fopen(in_path, "rb");
	if (fin == nullptr) {
		std::fprintf(stderr, "no puedo abrir %s\n", in_path);
		return 1;
	}
	std::fseek(fin, 0, SEEK_END);
	const long len = std::ftell(fin);
	std::fseek(fin, 0, SEEK_SET);
	if (len <= 0) {
		std::fprintf(stderr, "entrada vacia\n");
		std::fclose(fin);
		return 1;
	}
	std::vector<eng::u8> pcm(static_cast<size_t>(len));
	if (std::fread(pcm.data(), 1u, pcm.size(), fin) != pcm.size()) {
		std::fprintf(stderr, "lectura incompleta\n");
		std::fclose(fin);
		return 1;
	}
	std::fclose(fin);

	// Rellena el ultimo chunk para que TODOS descompriman a `chunk` muestras: es el contrato de
	// `PcmStream` (`provide` exige exactamente `chunk_samples`). El relleno va al final (muestra
	// 0) y `total_samples` lo incluye.
	while ((pcm.size() % static_cast<size_t>(chunk)) != 0u) {
		pcm.push_back(0u);
	}
	const eng::usize total = pcm.size();
	const eng::u16 num_chunks =
	    static_cast<eng::u16>((total + chunk - 1u) / static_cast<eng::usize>(chunk));

	// Codifica cada chunk (buffer temporal grande: peor caso = almacenamiento + overhead).
	std::vector<std::vector<eng::u8>> bodies(num_chunks);
	std::vector<eng::u32> offsets(num_chunks), sizes(num_chunks);
	const eng::usize hdr = eng::audio::auzx::kHeaderSize;
	const eng::usize entries = static_cast<eng::usize>(num_chunks) * eng::audio::auzx::kChunkEntrySize;
	eng::u32 at = static_cast<eng::u32>(hdr + entries);
	eng::usize done = 0u;
	for (eng::u16 c = 0u; c < num_chunks; ++c) {
		const eng::usize n = (total - done) < chunk ? (total - done) : chunk;
		// Techo de salida generoso: None = n; RLE peor caso = n + n/128 + 1; FibDelta = n/2+2;
		// IMA = n/2+4. Se reserva el mayor (+ margen).
		std::vector<eng::u8> enc(n + n / 128u + 16u);
		eng::s32 en = -1;
		if (comp == static_cast<eng::u8>(eng::audio::pcm_codec::Codec::None)) {
			for (eng::usize i = 0u; i < n; ++i) {
				enc[i] = pcm[done + i];
			}
			en = static_cast<eng::s32>(n);
		} else {
			en = eng::audio::pcm_codec::encode(
			    eng::Span<const eng::u8>(pcm.data() + done, n),
			    eng::Span<eng::u8>(enc.data(), enc.size()), comp);
		}
		if (en <= 0) {
			std::fprintf(stderr, "fallo al comprimir el chunk %u\n", c);
			return 1;
		}
		bodies[c].assign(enc.begin(), enc.begin() + en);
		offsets[c] = at;
		sizes[c] = static_cast<eng::u32>(en);
		at += static_cast<eng::u32>(en);
		done += n;
	}

	// Escribe cabecera + indice + payloads.
	std::vector<eng::u8> file(at, 0u);
	auto w8 = [&](eng::usize o, eng::u8 v) { file[o] = v; };
	eng::Span<eng::u8> f {file.data(), file.size()};
	w8(0u, 'A');
	w8(1u, 'U');
	w8(2u, 'Z');
	w8(3u, 'X');
	w8(4u, eng::audio::auzx::kVersion);
	w8(5u, comp);
	eng::audio::auzx::wr16(f, 6u, rate);
	eng::audio::auzx::wr16(f, 8u, 1u);
	w8(10u, 8u);
	w8(11u, 0u);
	eng::audio::auzx::wr32(f, 12u, static_cast<eng::u32>(total));
	eng::audio::auzx::wr16(f, 16u, chunk);
	eng::audio::auzx::wr16(f, 18u, num_chunks);
	eng::audio::auzx::wr32(f, 20u, static_cast<eng::u32>(hdr));
	eng::audio::auzx::wr32(f, 24u, static_cast<eng::u32>(hdr + entries));
	eng::audio::auzx::wr32(f, 28u, 0u);
	for (eng::u16 c = 0u; c < num_chunks; ++c) {
		const eng::usize e = hdr + static_cast<eng::usize>(c) * eng::audio::auzx::kChunkEntrySize;
		eng::audio::auzx::wr32(f, e, offsets[c]);
		eng::audio::auzx::wr32(f, e + 4u, sizes[c]);
		std::memcpy(file.data() + offsets[c], bodies[c].data(), bodies[c].size());
	}
	std::FILE* fo = std::fopen(out_path, "wb");
	if (fo == nullptr || std::fwrite(file.data(), 1u, file.size(), fo) != file.size()) {
		std::fprintf(stderr, "no puedo escribir %s\n", out_path);
		if (fo != nullptr) {
			std::fclose(fo);
		}
		return 1;
	}
	std::fclose(fo);

	// Auto-verificacion: relee, parsea, decodifica y compara con el PCM original.
	eng::audio::auzx::Header h {};
	const eng::Span<const eng::u8> view {file.data(), file.size()};
	if (!eng::audio::auzx::parse(view, h)) {
		std::fprintf(stderr, "AUZX invalido tras escribir\n");
		return 1;
	}
	std::vector<eng::u8> out(total);
	eng::usize got = 0u;
	for (eng::u16 c = 0u; c < h.num_chunks; ++c) {
		eng::u32 sz = 0u;
		const eng::Span<const eng::u8> body = eng::audio::auzx::chunk(view, h, c, sz);
		const eng::usize want = (total - got) < chunk ? (total - got) : chunk;
		const eng::s32 dn = eng::audio::pcm_codec::decode(
		    body, eng::Span<eng::u8>(out.data() + got, want), h.compression);
		if (dn <= 0) {
			std::fprintf(stderr, "fallo al decodificar el chunk %u\n", c);
			return 1;
		}
		got += static_cast<eng::usize>(dn);
	}
	std::size_t diff = 0u;
	for (eng::usize i = 0u; i < total && i < got; ++i) {
		if (out[i] != pcm[i]) {
			++diff;
		}
	}
	std::printf("pack-pcm: %s -> %s | codec=%u rate=%u chunk=%u | %u muestras, %u chunks, %u bytes\n",
		    in_path, out_path, h.compression, h.sample_rate, h.chunk_samples,
		    static_cast<unsigned>(total), h.num_chunks, static_cast<unsigned>(file.size()));
	// Con codecs con perdida (fib/ima) NO se espera round-trip exacto.
	const bool lossy = (h.compression == static_cast<eng::u8>(eng::audio::pcm_codec::Codec::FibDelta) ||
			    h.compression == static_cast<eng::u8>(eng::audio::pcm_codec::Codec::ImaAdpcm));
	if (!lossy && diff != 0u) {
		std::fprintf(stderr, "round-trip NO exacto (%zu bytes)\n", diff);
		return 1;
	}
	std::printf("round-trip: %s (%s)\n", diff == 0u ? "exacto" : "con perdida",
		    lossy ? "codec con perdida" : "sin perdida");
	return 0;
}
