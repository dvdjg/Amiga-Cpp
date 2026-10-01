// HOST-394: fuente WAV host por ventanas.

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../../../../../engine/include/eng/audio/pcm_codec.hpp"
#include "../../../../../host-tools/audio-compressor/src/acp1_writer.hpp"
#include "../../../../../host-tools/audio-compressor/include/audio_compressor/io/wav_source.hpp"
#include "../../../../../host-tools/audio-compressor/include/audio_compressor/io/raw_source.hpp"
#include "../../../../../host-tools/audio-compressor/include/audio_compressor/io/wav_stem_source.hpp"
#include "../../../../../host-tools/audio-compressor/include/audio_compressor/dsp/resampler.hpp"
#include "../../../../../host-tools/audio-compressor/include/audio_compressor/playback/acp1_host_player.hpp"
#include "../../../../../host-tools/audio-compressor/include/audio_compressor/formats/auzx_sink.hpp"
#include "../../../../../host-tools/audio-compressor/include/audio_compressor/pipeline/music_pipeline.hpp"
#include "../../../../../host-tools/audio-compressor/src/hpss.hpp"

static void put16(eng::u8* bytes, eng::usize at, eng::u16 value) {
	bytes[at] = static_cast<eng::u8>(value); bytes[at + 1u] = static_cast<eng::u8>(value >> 8u);
}

static void put32(eng::u8* bytes, eng::usize at, eng::u32 value) {
	put16(bytes, at, static_cast<eng::u16>(value)); put16(bytes, at + 2u, static_cast<eng::u16>(value >> 16u));
}

int main() {
	const std::string path = "host394.wav";
	eng::u8 wav[52]{};
	std::memcpy(wav, "RIFF", 4u); put32(wav, 4u, 44u); std::memcpy(wav + 8u, "WAVEfmt ", 8u);
	put32(wav, 16u, 16u); put16(wav, 20u, 1u); put16(wav, 22u, 2u); put32(wav, 24u, 11025u);
	put32(wav, 28u, 22050u); put16(wav, 32u, 2u); put16(wav, 34u, 8u); std::memcpy(wav + 36u, "data", 4u); put32(wav, 40u, 8u);
	wav[44] = 0u; wav[45] = 255u; wav[46] = 64u; wav[47] = 192u; wav[48] = 128u; wav[49] = 128u; wav[50] = 0u; wav[51] = 255u;
	std::ofstream file(path, std::ios::binary); file.write(reinterpret_cast<const char*>(wav), sizeof(wav)); file.close();
	{
		audio_compressor::io::WavSource source;
		if (!source.open(path) || source.sample_rate() != 11025u || source.frames() != 4u) return 1;
		eng::u8 window[2]{};
		if (source.read(1u, {window, 2u}) != 2u || static_cast<eng::s8>(window[0]) != 0 || static_cast<eng::s8>(window[1]) != 0) return 1;
	}
	{
		audio_compressor::io::WavStemSource source;
		if (!source.open(path) || source.channels() != 2u || source.frames() != 4u) return 1;
		eng::u8 channel_window[2]{};
		if (source.read_channel(1u, 1u, {channel_window, 2u}) != 2u || static_cast<eng::s8>(channel_window[0]) != 64 || static_cast<eng::s8>(channel_window[1]) != 0) return 1;
		audio_compressor::pipeline::MusicPlan window_plan {};
		std::vector<std::vector<eng::u8>> window_units;
		const auto encode_window = [](const std::vector<eng::u8>& pcm, std::vector<eng::u8>& payload) {
			payload.assign(40u + pcm.size(), 0u); std::memcpy(payload.data(), "AUZX", 4u); payload[4] = 1u;
			eng::Span<eng::u8> view {payload.data(), payload.size()}; eng::audio::auzx::wr16(view, 6u, 11025u);
			eng::audio::auzx::wr16(view, 8u, 1u); payload[10] = 8u; eng::audio::auzx::wr32(view, 12u, static_cast<eng::u32>(pcm.size()));
			eng::audio::auzx::wr16(view, 16u, static_cast<eng::u16>(pcm.size())); eng::audio::auzx::wr16(view, 18u, 1u);
			eng::audio::auzx::wr32(view, 20u, 32u); eng::audio::auzx::wr32(view, 24u, 40u);
			eng::audio::auzx::wr32(view, 32u, 40u); eng::audio::auzx::wr32(view, 36u, static_cast<eng::u32>(pcm.size()));
			std::memcpy(payload.data() + 40u, pcm.data(), pcm.size()); return true;
		};
		if (!audio_compressor::pipeline::MusicPipeline::build_windowed(source, 2u, true, 255u, encode_window, window_plan, window_units) ||
			window_plan.tracks.size() != 2u || window_plan.tracks[0].size() != 2u || window_plan.tracks[1].size() != 2u || window_units.empty()) return 1;
	}
	eng::u8 input[2] {0x80u, 0xffu}; eng::u8 output[8]{};
	audio_compressor::dsp::LinearResampler resampler {11025u, 22050u};
	const eng::usize first = resampler.process({input, 1u}, {output, 8u}, false);
	const eng::usize second = resampler.process({input + 1u, 1u}, {output, 8u}, true);
	if (first != 0u || second != 4u) return 1;
	const std::string raw_path = "host394.raw";
	const eng::u8 raw_bytes[6] {0x80u, 0x81u, 0x7fu, 0x00u, 0xffu, 0x40u};
	{
		std::ofstream raw_file(raw_path, std::ios::binary);
		raw_file.write(reinterpret_cast<const char*>(raw_bytes), sizeof(raw_bytes));
		raw_file.close();
		audio_compressor::io::RawSource raw;
		if (!raw.open(raw_path, 8000u) || raw.frames() != 6u) return 1;
		eng::u8 raw_window[3]{};
		if (raw.read(2u, {raw_window, 3u}) != 3u || std::memcmp(raw_window, raw_bytes + 2u, 3u) != 0) return 1;
	}
	std::remove(raw_path.c_str());
	const eng::u8 pcm[8] {0x80u, 0x80u, 0x81u, 0x90u, 0x7fu, 0x70u, 0x70u, 0x71u};
	std::vector<eng::u8> encoded(64u), decoded(8u);
	const eng::s32 rle_size = eng::audio::pcm_codec::encode({pcm, 8u}, {encoded.data(), encoded.size()});
	if (rle_size <= 0 || eng::audio::pcm_codec::decode({encoded.data(), static_cast<eng::usize>(rle_size)}, {decoded.data(), decoded.size()},
		static_cast<eng::u8>(eng::audio::pcm_codec::Codec::DeltaRle)) != 8 || std::memcmp(pcm, decoded.data(), 8u) != 0) return 1;
	eng::u8 fib_seed = 0u;
	const eng::s32 fib_size = eng::audio::fib_delta::encode({pcm, 8u}, {encoded.data(), encoded.size()}, fib_seed);
	if (fib_size <= 0 || eng::audio::pcm_codec::decode({encoded.data(), static_cast<eng::usize>(fib_size)}, {decoded.data(), decoded.size()},
		static_cast<eng::u8>(eng::audio::pcm_codec::Codec::FibDelta)) != 8) return 1;
	const eng::s32 ima_size = eng::audio::ima_adpcm::encode({pcm, 8u}, {encoded.data(), encoded.size()});
	if (ima_size <= 0 || eng::audio::pcm_codec::decode({encoded.data(), static_cast<eng::usize>(ima_size)}, {decoded.data(), decoded.size()},
		static_cast<eng::u8>(eng::audio::pcm_codec::Codec::ImaAdpcm)) != 8) return 1;
	std::vector<eng::u8> auzx(48u, 0u);
	eng::Span<eng::u8> auzx_view {auzx.data(), auzx.size()};
	std::memcpy(auzx.data(), "AUZX", 4u); auzx[4] = 1u; auzx[5] = static_cast<eng::u8>(eng::audio::pcm_codec::Codec::None);
	eng::audio::auzx::wr16(auzx_view, 6u, 8000u); eng::audio::auzx::wr16(auzx_view, 8u, 1u); auzx[10] = 8u;
	eng::audio::auzx::wr32(auzx_view, 12u, 8u); eng::audio::auzx::wr16(auzx_view, 16u, 8u); eng::audio::auzx::wr16(auzx_view, 18u, 1u);
	eng::audio::auzx::wr32(auzx_view, 20u, 32u); eng::audio::auzx::wr32(auzx_view, 24u, 40u);
	eng::audio::auzx::wr32(auzx_view, 32u, 40u); eng::audio::auzx::wr32(auzx_view, 36u, 8u);
	std::memcpy(auzx.data() + 40u, pcm, 8u);
	std::vector<std::vector<eng::u8>> payloads {auzx};
	std::vector<std::vector<eng::audio::acp1::Event>> tracks {{{0u, 0u, 8u, 255u}}};
	std::vector<eng::u8> acp1;
	if (!audio_compressor::build_acp1(payloads, 8000u, 8u, acp1, tracks)) return 1;
	audio_compressor::playback::Acp1HostPlayer player;
	if (!player.open({acp1.data(), acp1.size()}) || player.sample_rate() != 8000u || player.total_samples() != 8u) return 1;
	eng::u8 mixed[8]{}; eng::u8 scratch[8]{}; eng::s16 accumulator[8]{};
	if (player.read_window(0u, {mixed, 8u}, {scratch, 8u}, {accumulator, 8u}) != 8 || std::memcmp(mixed, pcm, 8u) != 0) return 1;
	std::vector<eng::u8> long_pcm(512u);
	for (eng::usize i = 0u; i < long_pcm.size(); ++i) long_pcm[i] = static_cast<eng::u8>(static_cast<eng::s8>((i * 13u) & 0x7fu));
	audio_compressor::HpssResult windowed_layers {};
	const auto hpss_begin = std::chrono::steady_clock::now();
	if (!audio_compressor::hpss_windowed_pcm(long_pcm, 256u, 64u, 32u, windowed_layers) ||
		windowed_layers.harmonic.size() != long_pcm.size() || windowed_layers.percussive.size() != long_pcm.size()) return 1;
	const auto hpss_end = std::chrono::steady_clock::now();
	const auto failed_output = std::filesystem::path {"host394-failed.auzx"};
	std::filesystem::remove(failed_output);
	{
		audio_compressor::formats::AuzxSink sink;
		const eng::u8 payload[4] {0u, 1u, 2u, 3u};
		if (!sink.open(failed_output, 8000u, 8u, 4u, 2u, 3u) || !sink.append({payload, 4u})) return 1;
	}
	if (std::filesystem::exists(failed_output) || std::filesystem::exists("host394-failed.auzx.tmp")) return 1;
	std::vector<std::vector<eng::u8>> four_stems(4u, std::vector<eng::u8>(8u));
	for (eng::usize track = 0u; track < four_stems.size(); ++track) std::fill(four_stems[track].begin(), four_stems[track].end(), static_cast<eng::u8>(static_cast<eng::s8>(127 - track * 20u)));
	audio_compressor::pipeline::MusicPlan route_plan {};
	std::vector<std::vector<eng::u8>> route_units;
	if (audio_compressor::pipeline::MusicPipeline::build_plan(four_stems, 8u, 255u, true, route_plan, route_units) ||
		!audio_compressor::pipeline::MusicPipeline::build_plan(four_stems, 8u, 128u, false, route_plan, route_units) ||
		route_units.size() != 4u || static_cast<eng::s8>(route_units[3][0]) > 31) return 1;
	const auto hpss_ms = std::chrono::duration_cast<std::chrono::microseconds>(hpss_end - hpss_begin).count();
	std::remove(path.c_str());
	std::printf("OK: fuentes, codecs, ACP1, HPSS windowed y sink transaccional; HPSS=%lld us.\n", static_cast<long long>(hpss_ms));
	return 0;
}
