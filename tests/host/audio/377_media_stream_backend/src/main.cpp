#include <cstdio>

#include <eng/audio/media_stream_backend.hpp>
#include <eng/audio/stream_planner.hpp>

struct Source {
	bool started = false;
	bool describe(eng::audio::AudioStreamId, eng::audio::media::Info& info) noexcept {
		info.channels = 1u; info.bits = 8u; info.chunk_samples = 1024u; return true;
	}
	bool start_stream(const eng::audio::StreamIntent&, const eng::audio::media::Info&) noexcept {
		started = true; return true;
	}
};

int main() {
	Source source;
	eng::audio::MediaStreamBackend<Source> backend(source);
	const eng::audio::StreamIntent intent{eng::audio::AudioStreamId{3u}, 64u, 0xffu, 2u, 1024u, false};
	if (!backend.start_stream(intent) || !source.started) return 1;
	const eng::audio::StreamIntent wrong{eng::audio::AudioStreamId{3u}, 64u, 0xffu, 2u, 2048u, false};
	if (backend.start_stream(wrong)) return 1;
	std::printf("OK: backend de stream basado en media::Info validado.\n"); return 0;
}
