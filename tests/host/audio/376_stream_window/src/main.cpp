#include <cstdio>

#include "../../../../../host-tools/pack-pcm/streaming.hpp"

struct Source {
	eng::u8 samples[5] {1u, 2u, 3u, 4u, 5u};
	eng::usize read(eng::u64 offset, eng::Span<eng::u8> window) noexcept {
		if (offset >= 5u) return 0u;
		const eng::usize count = (5u - static_cast<eng::usize>(offset) < window.size()) ?
			5u - static_cast<eng::usize>(offset) : window.size();
		for (eng::usize i = 0u; i < count; ++i) window[i] = samples[offset + i];
		return count;
	}
};

struct Encoder {
	eng::usize encode(const pack_pcm::Candidate&, eng::Span<const eng::u8> samples) noexcept {
		return samples.size();
	}
};

int main() {
	const pack_pcm::TrainingConfig config{};
	if (!pack_pcm::fits_budget(config)) return 1;
	Source source; Encoder encoder; eng::u8 scratch[2]{}; pack_pcm::Metrics metrics{};
	if (!pack_pcm::evaluate(source, encoder, pack_pcm::Candidate{}, scratch, metrics)) return 1;
	if (metrics.samples != 5u || metrics.encoded_bytes != 5u) return 1;
	std::printf("OK: evaluación por ventanas sin acumular el audio validada.\n");
	return 0;
}
