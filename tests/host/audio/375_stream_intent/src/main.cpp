#include <cstdio>
#include <cstring>

#include <eng/audio/stream_intent.hpp>

struct Backend {
	bool accepted = false;
	bool start_stream(const eng::audio::StreamIntent& intent) noexcept {
		accepted = eng::audio::valid(intent);
		return accepted;
	}
};

int main() {
	Backend backend;
	eng::audio::StreamExecutor<4u, Backend> executor(backend);
	const eng::audio::StreamIntent intent{eng::audio::AudioStreamId{7u}, 64u, 0xffu, 3u, 4096u, true};
	if (!executor.ready() || !executor.run(intent) || !backend.accepted) return 1;
	const eng::audio::StreamIntent invalid{eng::audio::AudioStreamId{}, 65u, 0xffu, 1u, 0u, false};
	if (executor.run(invalid)) return 1;
	std::printf("OK: intención de streaming validada.\n");
	return 0;
}
