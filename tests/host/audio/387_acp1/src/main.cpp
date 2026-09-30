// HOST-387: parser ACP1 v1/v2, serializador de eventos, decoder, HPSS y deduplicación exacta.

#include <cstdio>
#include <cmath>
#include <vector>

#include <eng/audio/acp1.hpp>
#include <eng/audio/acp1_stream.hpp>
#include <eng/audio/media.hpp>
#include <eng/audio/pcm_codec.hpp>
#include "../../../../../host-tools/audio-compressor/src/acp1_writer.hpp"
#include "../../../../../host-tools/audio-compressor/src/hpss.hpp"

namespace {

int failures = 0;

/// Registra el resultado de una condición del contrato ACP1.
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
	using Event = eng::audio::acp1::Event;
	const std::vector<std::vector<Event>> synchronized_events {{{0u, 0u, 4u, 128u}, {0u, 4u, 4u, 128u}}, {{1u, 0u, 4u, 128u}, {1u, 4u, 4u, 128u}}};
	const bool encoded = audio_compressor::build_acp1(units, 22050u, 8u, file, synchronized_events);
	check(encoded, "encoder escribe ACP1 v2 multipista con eventos");
	eng::audio::acp1::Info info {};
	check(eng::audio::acp1::parse({file.data(), file.size()}, info), "parser acepta ACP1 producido por encoder");
	check(info.sample_rate == 22050u && info.total_samples == 8u && info.unit_count == 2u && info.track_count == 2u,
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
		eng::audio::acp1::Event event{};
		check(eng::audio::acp1::event({file.data(), file.size()}, info, i, 0u, event), "evento de pista disponible");
		check(track.destination == i && track.event_count == 2u && event.unit_id == i &&
			event.start_sample == 0u && event.gain == 128u, "tracks con secuencias de eventos sincronizadas");
		eng::audio::auzx::Header nested {};
		check(unit.id == i && unit.decoded_samples == 4u && unit.gain == 255u &&
			eng::audio::auzx::parse(unit.payload, nested) && nested.sample_rate == info.sample_rate,
			"unidad referencia payload AUZX válido a la misma tasa");
		eng::u8 decoded[8]{}, scratch[4]{};
		check(eng::audio::media::decode_track_window({file.data(), file.size()}, media_info, i, 0u,
			{decoded, 8u}, {scratch, 4u}) == 8, "media decodifica track ACP1 a PCM");
		const eng::u8* expected = i == 0u ? left_pcm : right_pcm;
		for (eng::usize sample = 0u; sample < 4u; ++sample) {
			const eng::u8 gained = static_cast<eng::u8>(static_cast<eng::s8>(expected[sample]) / 2);
			check(decoded[sample] == gained && decoded[sample + 4u] == gained,
				"secuencia ACP1 coloca eventos en su región y deja el resto de la pista en silencio");
		}
	}
	eng::u8 mixed[8]{}, scratch[4]{}; eng::s16 accumulator[8]{};
	check(eng::audio::media::mix_window({file.data(), file.size()}, media_info, 0u,
		{mixed, 8u}, {scratch, 4u}, {accumulator, 8u}) == 8,
		"media mezcla tracks ACP1 en ventana sincronizada");
	for (eng::usize sample = 0u; sample < 4u; ++sample) {
		const eng::s32 expected = static_cast<eng::s8>(left_pcm[sample]) / 2 + static_cast<eng::s8>(right_pcm[sample]) / 2;
		check(static_cast<eng::s8>(mixed[sample]) == expected, "mezcla ACP1 aplica ganancia por evento a cada stem");
	}
	for (eng::usize sample = 4u; sample < 8u; ++sample) {
		const eng::s32 expected = static_cast<eng::s8>(left_pcm[sample - 4u]) / 2 + static_cast<eng::s8>(right_pcm[sample - 4u]) / 2;
		check(static_cast<eng::s8>(mixed[sample]) == expected, "eventos repetidos contribuyen en la segunda mitad de la timeline");
	}
	check(info.version == 2u, "encoder emite ACP1 v2 para secuencias de eventos");
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

/// ACP1 v2 recorre varios eventos no solapados de una pista en una línea temporal común.
void test_event_sequence() {
	const eng::u8 first[] {0x80u, 0x81u, 0x82u, 0x83u};
	const eng::u8 second[] {0x10u, 0x11u, 0x12u, 0x13u};
	std::vector<std::vector<eng::u8>> units {make_auzx(first, 4u, 8000u), make_auzx(second, 4u, 8000u),
		make_auzx(first, 4u, 8000u)};
	using Event = eng::audio::acp1::Event;
	std::vector<std::vector<Event>> events {{{0u, 0u, 4u, 255u}, {1u, 4u, 4u, 255u}, {2u, 8u, 4u, 255u}}};
	std::vector<eng::u8> file;
	check(audio_compressor::build_acp1(units, 8000u, 12u, file, events), "encoder acepta tres eventos secuenciales");
	eng::audio::acp1::Info acp1_info{};
	eng::audio::media::Info media_info{};
	check(eng::audio::acp1::parse({file.data(), file.size()}, acp1_info) && acp1_info.version == 2u &&
		acp1_info.event_count == 3u, "parser valida secuencia ACP1 v2");
	check(eng::audio::media::open({file.data(), file.size()}, media_info), "media abre secuencia ACP1 v2");
	eng::u8 output[12]{}, scratch[4]{};
	check(eng::audio::media::decode_track_window({file.data(), file.size()}, media_info, 0u, 0u,
		{output, 12u}, {scratch, 4u}) == 12, "decoder cubre timeline completa de eventos");
	for (eng::usize i = 0u; i < 4u; ++i) {
		check(output[i] == first[i] && output[i + 4u] == second[i], "decoder conmuta unidad en la frontera temporal");
	}
	eng::u8 gap[10]{};
	std::vector<std::vector<Event>> with_gap {{{0u, 0u, 4u, 255u}, {1u, 6u, 4u, 128u}}};
	check(audio_compressor::build_acp1(units, 8000u, 10u, file, with_gap), "encoder acepta silencio entre eventos");
	check(eng::audio::media::open({file.data(), file.size()}, media_info) &&
		eng::audio::media::decode_track_window({file.data(), file.size()}, media_info, 0u, 0u, {gap, 10u}, {scratch, 4u}) == 10,
		"decoder produce timeline con hueco silencioso");
	check(gap[4] == 0u && gap[5] == 0u, "hueco entre eventos queda en silencio");
	check(static_cast<eng::s8>(gap[6]) == static_cast<eng::s8>(second[0]) / 2, "ganancia del evento se aplica a la unidad");
	eng::u8 pcm0[4]{}, pcm1[4]{}, pcm2[4]{}, scratch_stream[4]{};
	eng::s16 accumulator_stream[4]{};
	eng::Span<eng::u8> buffers[3] {{pcm0, 4u}, {pcm1, 4u}, {pcm2, 4u}};
	eng::audio::Acp1Stream<3> stream{};
	const std::vector<std::vector<Event>> stream_events {{{0u, 0u, 4u, 255u}, {1u, 4u, 4u, 255u}, {2u, 8u, 4u, 255u}}};
	std::vector<eng::u8> stream_file;
	const std::vector<std::vector<eng::u8>> stream_units {make_auzx(first, 4u, 8000u), make_auzx(second, 4u, 8000u), make_auzx(first, 4u, 8000u)};
	check(audio_compressor::build_acp1(stream_units, 8000u, 12u, stream_file, stream_events), "fixture ACP1 para triple buffer");
	eng::audio::media::Info stream_media{};
	check(eng::audio::media::open({stream_file.data(), stream_file.size()}, stream_media), "media abre timeline de stream");
	check(stream.begin({stream_file.data(), stream_file.size()}, stream_media, buffers, {scratch_stream, 4u},
		{accumulator_stream, 4u}, 4u), "stream ACP1 inicia con triple buffer");
	check(stream.refill() == 3u && stream.eof() && !stream.failed(), "productor prepara tres buffers antes de EOF");
	check(static_cast<eng::s8>(stream.play_pcm()[0]) == static_cast<eng::s8>(first[0]), "primer buffer ACP1 contiene la primera región");
	const bool first_swap = stream.advance();
	const bool second_swap = stream.advance();
	const bool end_of_stream = !stream.advance() && stream.at_end();
	check(first_swap && second_swap && end_of_stream && stream.finished() && !stream.failed(),
		"IRQ consume buffers preparados y distingue EOF de underrun");
}

} // namespace

int main() {
	test_round_trip();
	test_rejections();
	test_hpss();
	test_event_sequence();
	if (failures == 0) { std::printf("OK: ACP1 v1/v2, secuencias, mezcla, HPSS y deduplicación validados.\n"); return 0; }
	return 1;
}
