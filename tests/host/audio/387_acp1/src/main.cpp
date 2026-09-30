// HOST-387: parser ACP1 v1 y serializador host de stems AUZX sincronizados.

#include <cstdio>
#include <cmath>
#include <vector>

#include <eng/audio/acp1.hpp>
#include <eng/audio/media.hpp>
#include <eng/audio/pcm_codec.hpp>
#include "../../../../../host-tools/audio-compressor/src/acp1_writer.hpp"
#include "../../../../../host-tools/audio-compressor/src/hpss.hpp"

namespace {

int failures = 0;

/// Registra el resultado de una condición del contrato ACP1 v1.
void check(bool ok, const char* message) {
	if (!ok) { std::printf("FAIL: %s\n", message); ++failures; }
}

/// Construye un AUZX PCM8 mono de un chunk, válido para incrustar como unidad.
std::vector<eng::u8> make_auzx(const eng::u8* samples, eng::u16 count, eng::u16 rate) {
	const eng::u16 chunk_samples = 2u;
	const eng::u16 chunks = static_cast<eng::u16>((count + chunk_samples - 1u) / chunk_samples);
	const eng::usize index_size = static_cast<eng::usize>(chunks) * eng::audio::auzx::kChunkEntrySize;
	const eng::usize data_offset = eng::audio::auzx::kHeaderSize + index_size;
	const eng::usize size = data_offset + count;
	std::vector<eng::u8> bytes(size, 0u);
	eng::Span<eng::u8> out {bytes.data(), bytes.size()};
	bytes[0] = 'A'; bytes[1] = 'U'; bytes[2] = 'Z'; bytes[3] = 'X'; bytes[4] = 1u;
	bytes[5] = static_cast<eng::u8>(eng::audio::pcm_codec::Codec::None);
	eng::audio::auzx::wr16(out, 6u, rate); eng::audio::auzx::wr16(out, 8u, 1u); bytes[10] = 8u;
	eng::audio::auzx::wr32(out, 12u, count); eng::audio::auzx::wr16(out, 16u, chunk_samples);
	eng::audio::auzx::wr16(out, 18u, chunks); eng::audio::auzx::wr32(out, 20u, eng::audio::auzx::kHeaderSize);
	eng::audio::auzx::wr32(out, 24u, static_cast<eng::u32>(data_offset));
	for (eng::u16 i = 0u; i < chunks; ++i) {
		const eng::usize at = eng::audio::auzx::kHeaderSize + static_cast<eng::usize>(i) * eng::audio::auzx::kChunkEntrySize;
		const eng::usize start = static_cast<eng::usize>(i) * chunk_samples;
		const eng::usize length = count - start < chunk_samples ? count - start : chunk_samples;
		eng::audio::auzx::wr32(out, at, static_cast<eng::u32>(data_offset + start));
		eng::audio::auzx::wr32(out, at + 4u, static_cast<eng::u32>(length));
	}
	for (eng::usize i = 0u; i < count; ++i) bytes[data_offset + i] = samples[i];
	return bytes;
}

/// Valida serialización, vistas de pistas/unidades y sincronía de dos stems.
void test_round_trip() {
	const eng::u8 left_pcm[] {0x80u, 0x00u, 0x7fu, 0x20u};
	const eng::u8 right_pcm[] {0x00u, 0x40u, 0xc0u, 0xffu};
	std::vector<std::vector<eng::u8>> units {make_auzx(left_pcm, 4u, 22050u), make_auzx(right_pcm, 4u, 22050u)};
	std::vector<eng::u8> file;
	check(audio_compressor::build_acp1(units, 22050u, 4u, file), "encoder escribe ACP1 v1 multipista");
	eng::audio::acp1::Info info {};
	check(eng::audio::acp1::parse({file.data(), file.size()}, info), "parser acepta ACP1 producido por encoder");
	check(info.sample_rate == 22050u && info.total_samples == 4u && info.unit_count == 2u && info.track_count == 2u,
		"cabecera conserva tasa, duración y número de stems");
	eng::audio::media::Info media_info{};
	check(eng::audio::media::open({file.data(), file.size()}, media_info) &&
		media_info.container == eng::audio::media::Container::Acp1 && media_info.channels == 2u,
		"media reconoce ACP1 y expone número de pistas");
	for (eng::u8 i = 0u; i < 2u; ++i) {
		eng::audio::acp1::Track track {};
		eng::audio::acp1::Unit unit {};
		check(eng::audio::acp1::track({file.data(), file.size()}, info, i, track), "vista de track disponible");
		check(eng::audio::acp1::unit({file.data(), file.size()}, info, i, unit), "vista de unidad disponible");
		check(track.destination == i && track.unit_id == i && track.start_sample == 0u &&
			track.duration == info.total_samples && track.gain == 255u, "tracks con destinos y eventos sincronizados");
		eng::audio::auzx::Header nested {};
		check(unit.id == i && unit.decoded_samples == info.total_samples && unit.gain == 255u &&
			eng::audio::auzx::parse(unit.payload, nested) && nested.sample_rate == info.sample_rate,
			"unidad referencia payload AUZX válido a la misma tasa");
		eng::u8 decoded[4]{}, scratch[4]{};
		check(eng::audio::media::decode_track_window({file.data(), file.size()}, media_info, i, 0u,
			{decoded, 4u}, {scratch, 4u}) == 4, "media decodifica track ACP1 a PCM");
		const eng::u8* expected = i == 0u ? left_pcm : right_pcm;
		for (eng::usize sample = 0u; sample < 4u; ++sample)
			check(decoded[sample] == expected[sample], "track ACP1 conserva muestras round-trip");
	}
	eng::u8 mixed[4]{}, scratch[4]{}; eng::s16 accumulator[4]{};
	check(eng::audio::media::mix_window({file.data(), file.size()}, media_info, 0u,
		{mixed, 4u}, {scratch, 4u}, {accumulator, 4u}) == 4,
		"media mezcla tracks ACP1 en ventana sincronizada");
	for (eng::usize sample = 0u; sample < 4u; ++sample) {
		const eng::u8 expected = static_cast<eng::u8>(static_cast<eng::s8>(static_cast<eng::s8>(left_pcm[sample]) + static_cast<eng::s8>(right_pcm[sample])));
		check(mixed[sample] == expected, "mezcla ACP1 suma stems sin pérdida ni saturación prematura");
	}
}

/// El parser rechaza truncados, offsets, referencias, destinos y tiempos incoherentes.
void test_rejections() {
	const eng::u8 pcm[] {1u, 2u, 3u, 4u};
	std::vector<std::vector<eng::u8>> units {make_auzx(pcm, 4u, 8000u), make_auzx(pcm, 4u, 8000u)};
	std::vector<eng::u8> valid;
	check(audio_compressor::build_acp1(units, 8000u, 4u, valid), "fixture ACP1 válido");
	eng::audio::acp1::Info info {};
	check(!audio_compressor::build_acp1(units, 8000u, 5u, valid), "encoder rechaza unidad más corta que la duración ACP1");
	check(audio_compressor::build_acp1(units, 8000u, 4u, valid), "reconstruye fixture válido tras rechazo");
	check(!eng::audio::acp1::parse({valid.data(), valid.size() - 1u}, info), "rechaza archivo truncado");
	auto bad = valid;
	bad[28] = 0xffu;
	check(!eng::audio::acp1::parse({bad.data(), bad.size()}, info), "rechaza offset de tracks inválido");
	bad = valid;
	const eng::u32 events_offset = eng::audio::acp1::rd32({bad.data(), bad.size()}, 32u);
	bad[events_offset + eng::audio::acp1::kEventSize + 8u] ^= 1u;
	check(!eng::audio::acp1::parse({bad.data(), bad.size()}, info), "rechaza duración de evento incompatible");
	bad = valid;
	const eng::u32 tracks_offset = eng::audio::acp1::rd32({bad.data(), bad.size()}, 28u);
	bad[tracks_offset + eng::audio::acp1::kTrackSize] = bad[tracks_offset];
	check(!eng::audio::acp1::parse({bad.data(), bad.size()}, info), "rechaza destinos duplicados");
	bad = valid;
	const eng::u32 unit_offset = eng::audio::acp1::rd32({bad.data(), bad.size()}, 24u);
	const eng::u32 payload_offset = eng::audio::acp1::rd32({bad.data(), bad.size()}, unit_offset + 4u);
	bad[payload_offset] = 'X';
	check(!eng::audio::acp1::parse({bad.data(), bad.size()}, info), "rechaza unidad con payload AUZX inválido");
	check(!audio_compressor::build_acp1(units, 0u, 4u, valid), "encoder rechaza tasa cero");
	check(!audio_compressor::build_acp1(units, 8000u, 0u, valid), "encoder rechaza duración cero");
	auto mismatched_rate = make_auzx(pcm, 4u, 11025u);
	units[1] = mismatched_rate;
	check(!audio_compressor::build_acp1(units, 8000u, 4u, valid), "encoder rechaza tasa distinta entre stems");
	units[0] = make_auzx(pcm, 4u, 8000u);
	units[1] = make_auzx(pcm, 4u, 8000u);
	check(audio_compressor::build_acp1(units, 8000u, 4u, valid), "encoder admite payloads repetidos");
	check(eng::audio::acp1::rd16({valid.data(), valid.size()}, 12u) == 1u,
		"deduplicación exacta comparte la unidad AUZX del diccionario");
	eng::audio::acp1::Info dedup_info{};
	check(eng::audio::acp1::parse({valid.data(), valid.size()}, dedup_info) && dedup_info.track_count == 2u,
		"tracks distintos referencian una única unidad compartida");
}

/// Comprueba que HPSS conserva energía por capas y rechaza tamaños FFT inválidos.
void test_hpss() {
	std::vector<eng::u8> tone(512u);
	for (eng::usize i = 0u; i < tone.size(); ++i) {
		const int value = static_cast<int>(60.0 * std::sin(2.0 * 3.141592653589793 * 16.0 * static_cast<double>(i) / 128.0));
		tone[i] = static_cast<eng::u8>(static_cast<eng::s8>(value));
	}
	audio_compressor::HpssResult layers {};
	check(audio_compressor::hpss(tone, 128u, layers), "HPSS procesa una señal periódica");
	check(layers.harmonic.size() == tone.size() && layers.percussive.size() == tone.size(), "HPSS conserva longitud");
	eng::u64 harmonic_energy = 0u, percussive_energy = 0u;
	for (eng::usize i = 0u; i < tone.size(); ++i) {
		const eng::s32 h = static_cast<eng::s8>(layers.harmonic[i]);
		const eng::s32 p = static_cast<eng::s8>(layers.percussive[i]);
		harmonic_energy += static_cast<eng::u64>(h * h);
		percussive_energy += static_cast<eng::u64>(p * p);
	}
	check(harmonic_energy > percussive_energy, "HPSS ubica tono sostenido predominantemente en la capa armónica");
	std::vector<eng::u8> transients(512u, 0u);
	for (eng::usize i = 0u; i < transients.size(); i += 64u) transients[i] = 0x7fu;
	check(audio_compressor::hpss(transients, 128u, layers), "HPSS procesa transitorios repetidos");
	harmonic_energy = 0u; percussive_energy = 0u;
	for (eng::usize i = 0u; i < transients.size(); ++i) {
		const eng::s32 h = static_cast<eng::s8>(layers.harmonic[i]);
		const eng::s32 p = static_cast<eng::s8>(layers.percussive[i]);
		harmonic_energy += static_cast<eng::u64>(h * h);
		percussive_energy += static_cast<eng::u64>(p * p);
	}
	check(percussive_energy > harmonic_energy, "HPSS ubica tren de impulsos predominantemente en la capa percusiva");
	check(!audio_compressor::hpss(tone, 100u, layers), "HPSS rechaza tamaño FFT no potencia de dos");
}

} // namespace

int main() {
	test_round_trip();
	test_rejections();
	test_hpss();
	if (failures == 0) { std::printf("OK: ACP1 v1, unidades AUZX y eventos sincronizados validados.\n"); return 0; }
	return 1;
}
