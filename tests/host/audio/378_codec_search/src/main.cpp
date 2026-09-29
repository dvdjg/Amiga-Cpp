#include <cstdio>

#include "../../../../../host-tools/pack-pcm/streaming.hpp"

struct Source {
	eng::u8 samples[4] {1u, 2u, 3u, 4u};
	eng::usize read(eng::u64 offset, eng::Span<eng::u8> window) noexcept {
		if (offset >= 4u) return 0u;
		const eng::usize count = (4u - static_cast<eng::usize>(offset) < window.size()) ? 4u - static_cast<eng::usize>(offset) : window.size();
		for (eng::usize i = 0u; i < count; ++i) window[i] = samples[offset + i];
		return count;
	}
};

struct Encoder {
	eng::usize encode(const pack_pcm::Candidate& candidate, eng::Span<const eng::u8> input, eng::Span<eng::u8> output) noexcept {
		const eng::usize n = candidate.codec == 2u ? input.size() : input.size() + 1u;
		if (n > output.size()) return 0u;
		for (eng::usize i = 0u; i < input.size(); ++i) output[i] = input[i];
		if (n > input.size()) output[n - 1u] = 0u;
		return n;
	}
	eng::usize decode(const pack_pcm::Candidate& candidate, eng::Span<const eng::u8> input, eng::Span<eng::u8> output) noexcept {
		const eng::usize n = candidate.codec == 2u ? input.size() : input.size() - 1u;
		if (n > output.size()) return 0u;
		for (eng::usize i = 0u; i < n; ++i) output[i] = input[i];
		return n;
	}
};

int main() {
	Source source; Encoder encoder; eng::u8 scratch[2]{}, encoded[8]{}, rebuilt[2]{};
	const pack_pcm::Candidate candidates[2]{{2u, 2u, 8u}, {3u, 2u, 8u}};
	pack_pcm::Candidate best{}; pack_pcm::Metrics metrics{};
	if (!pack_pcm::search(source, encoder, candidates, scratch, encoded, rebuilt, best, metrics)) return 1;
	if (best.codec != 2u || metrics.samples != 4u || metrics.peak_error != 0u) return 1;
	std::printf("OK: búsqueda de candidatos con reconstrucción validada.\n"); return 0;
}
