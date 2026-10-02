#pragma once

/// Separación host experimental por prototipos espectrales tiempo-frecuencia.

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <vector>

#include <eng/core/types/types.hpp>

namespace audio_compressor::dsp {

struct SpectralPrototypeOptions {
	eng::u16 fft_size = 256u;
	eng::u16 hop_samples = 64u;
	eng::u8 max_prototypes = 3u;
	eng::s16 max_shift_bins = 12;
	eng::u8 seed_candidates = 8u;
	eng::u64 max_dictionary_bytes = 0u;
	double min_activation_ratio = 0.02;
	double stop_residual_ratio = 0.0; ///< Cero conserva exactamente max_prototypes; positivo permite parar por coste/residual.
};

struct SpectralPrototype {
	std::vector<double> magnitude;
	std::vector<double> phase;
	std::vector<double> activation;
	std::vector<eng::s16> shift_bins;
	std::vector<eng::u8> pcm;
	double selection_score = 0.0;
};

struct SpectralSeparationMetrics {
	double magnitude_mse = 0.0;
	double magnitude_snr_db = 0.0;
	double residual_ratio = 1.0;
};

struct SpectralSeparationResult {
	eng::u16 fft_size = 0u;
	eng::u16 hop_samples = 0u;
	eng::u32 sample_rate = 0u;
	std::vector<SpectralPrototype> prototypes;
	std::vector<std::vector<eng::u8>> reconstructed_tracks;
	std::vector<eng::u8> reconstructed_pcm;
	SpectralSeparationMetrics metrics {};
	eng::u64 estimated_bytes = 0u;
	eng::u8 peak_concurrent_prototypes = 0u;
	eng::u32 dominant_period_frames = 0u;
	double periodicity_score = 0.0;
	bool requires_octamed = false;
};

namespace spectral_detail {

inline void fft(std::vector<std::complex<double>>& values, bool inverse) {
	const eng::usize count = values.size();
	for (eng::usize i = 1u, j = 0u; i < count; ++i) {
		eng::usize bit = count >> 1u;
		for (; (j & bit) != 0u; bit >>= 1u) j ^= bit;
		j ^= bit;
		if (i < j) std::swap(values[i], values[j]);
	}
	for (eng::usize length = 2u; length <= count; length <<= 1u) {
		const double angle = (inverse ? 2.0 : -2.0) * 3.14159265358979323846 / length;
		const std::complex<double> step {std::cos(angle), std::sin(angle)};
		for (eng::usize start = 0u; start < count; start += length) {
			std::complex<double> factor {1.0, 0.0};
			for (eng::usize i = 0u; i < length / 2u; ++i) {
				const auto even = values[start + i];
				const auto odd = factor * values[start + i + length / 2u];
				values[start + i] = even + odd;
				values[start + i + length / 2u] = even - odd;
				factor *= step;
			}
		}
	}
	if (inverse) for (auto& value : values) value /= static_cast<double>(count);
}

inline bool power_of_two(eng::u16 value) noexcept { return value != 0u && (value & (value - 1u)) == 0u; }

inline double dot_shifted(const std::vector<double>& frame, const std::vector<double>& prototype, eng::s16 shift) {
	const eng::s32 bins = static_cast<eng::s32>(prototype.size());
	double result = 0.0;
	for (eng::s32 bin = 0; bin < bins; ++bin) {
		const eng::s32 source = bin + shift;
		if (source >= 0 && source < static_cast<eng::s32>(frame.size())) result += frame[static_cast<eng::usize>(source)] * prototype[static_cast<eng::usize>(bin)];
	}
	return result;
}

inline double energy(const std::vector<double>& frame) {
	double result = 0.0;
	for (const double value : frame) result += value * value;
	return result;
}

} // namespace spectral_detail

/// Extrae prototipos de un plano STFT. Cada prototipo conserva espectro, activación y desplazamiento por frame.
[[nodiscard]] inline bool separate_spectral_prototypes(const std::vector<eng::u8>& pcm, eng::u32 sample_rate,
	const SpectralPrototypeOptions& options, SpectralSeparationResult& output) {
	using namespace spectral_detail;
	if (pcm.empty() || sample_rate == 0u || !power_of_two(options.fft_size) || options.fft_size < 64u ||
		options.hop_samples == 0u || options.hop_samples > options.fft_size || options.max_prototypes == 0u || options.max_shift_bins < 0 || options.seed_candidates == 0u || options.min_activation_ratio < 0.0) return false;
	const eng::usize bins = options.fft_size / 2u + 1u;
	const eng::usize frame_count = (pcm.size() + options.hop_samples - 1u) / options.hop_samples;
	std::vector<std::vector<double>> magnitudes(frame_count, std::vector<double>(bins, 0.0));
	std::vector<std::vector<double>> phases(frame_count, std::vector<double>(bins, 0.0));
	std::vector<double> window(options.fft_size, 0.0);
	for (eng::usize i = 0u; i < options.fft_size; ++i) window[i] = 0.5 - 0.5 * std::cos(6.28318530717958647692 * i / options.fft_size);
	for (eng::usize frame = 0u; frame < frame_count; ++frame) {
		std::vector<std::complex<double>> spectrum(options.fft_size, {0.0, 0.0});
		const eng::usize start = frame * options.hop_samples;
		for (eng::usize i = 0u; i < options.fft_size && start + i < pcm.size(); ++i) spectrum[i] = static_cast<double>(static_cast<eng::s8>(pcm[start + i])) * window[i];
		fft(spectrum, false);
		for (eng::usize bin = 0u; bin < bins; ++bin) {
			magnitudes[frame][bin] = std::abs(spectrum[bin]);
			phases[frame][bin] = std::arg(spectrum[bin]);
		}
	}
	std::vector<std::vector<double>> residual = magnitudes;
	const double total_energy = [&] { double value = 0.0; for (const auto& frame : magnitudes) value += energy(frame); return value; }();
	if (total_energy <= std::numeric_limits<double>::epsilon()) return false;
	output = {};
	output.fft_size = options.fft_size; output.hop_samples = options.hop_samples; output.sample_rate = sample_rate;
	for (eng::u8 selection = 0u; selection < options.max_prototypes; ++selection) {
		std::vector<eng::usize> seeds(frame_count);
		for (eng::usize frame = 0u; frame < frame_count; ++frame) seeds[frame] = frame;
		const eng::usize seed_count = std::min<eng::usize>(options.seed_candidates, frame_count);
		std::partial_sort(seeds.begin(), seeds.begin() + seed_count, seeds.end(), [&](eng::usize left, eng::usize right) { return energy(residual[left]) > energy(residual[right]); });
		SpectralPrototype best_prototype {};
		double best_score = 0.0;
		for (eng::usize seed_index = 0u; seed_index < seed_count; ++seed_index) {
			const eng::usize seed = seeds[seed_index];
			const double seed_energy = energy(residual[seed]);
			if (seed_energy <= 1.0e-12) continue;
			SpectralPrototype candidate {};
			candidate.magnitude = residual[seed]; candidate.phase = phases[seed];
			const double norm = std::sqrt(std::max(1.0e-12, seed_energy));
			for (double& value : candidate.magnitude) value /= norm;
			candidate.activation.assign(frame_count, 0.0); candidate.shift_bins.assign(frame_count, 0);
			double explained = 0.0; eng::usize active_frames = 0u;
			for (eng::usize frame = 0u; frame < frame_count; ++frame) {
				double best = 0.0; eng::s16 best_shift = 0;
				for (eng::s16 shift = static_cast<eng::s16>(-options.max_shift_bins); shift <= options.max_shift_bins; ++shift) {
					const double value = dot_shifted(residual[frame], candidate.magnitude, shift);
					if (value > best) { best = value; best_shift = shift; }
				}
				if (best <= norm * options.min_activation_ratio) { best = 0.0; best_shift = 0; }
				candidate.activation[frame] = best; candidate.shift_bins[frame] = best_shift;
				if (best > norm * options.min_activation_ratio) ++active_frames;
				explained += best * best;
			}
			const double cost = static_cast<double>(options.fft_size) + active_frames * 8.0 + 32.0;
			candidate.selection_score = cost > 0.0 ? explained / cost : 0.0;
			if (candidate.selection_score > best_score) { best_score = candidate.selection_score; best_prototype = std::move(candidate); }
		}
		if (best_score <= 0.0) break;
		for (eng::usize frame = 0u; frame < frame_count; ++frame) for (eng::usize bin = 0u; bin < bins; ++bin) {
			const eng::s32 source = static_cast<eng::s32>(bin) - best_prototype.shift_bins[frame];
			if (source >= 0 && source < static_cast<eng::s32>(bins)) residual[frame][bin] = std::max(0.0, residual[frame][bin] - best_prototype.activation[frame] * best_prototype.magnitude[static_cast<eng::usize>(source)]);
		}
		const eng::u64 current_bytes = 32u + static_cast<eng::u64>(output.prototypes.size()) * (options.fft_size + frame_count * 8u);
		if (options.max_dictionary_bytes != 0u && current_bytes + options.fft_size + frame_count * 8u > options.max_dictionary_bytes) break;
		output.prototypes.push_back(std::move(best_prototype));
		if (options.stop_residual_ratio > 0.0) {
			double remaining = 0.0;
			for (const auto& frame : residual) remaining += energy(frame);
			if (remaining / total_energy <= options.stop_residual_ratio) break;
		}
	}
	if (output.prototypes.empty()) return false;
	double source_energy = 0.0, error_energy = 0.0, residual_energy = 0.0;
	std::vector<std::vector<double>> reconstruction(frame_count, std::vector<double>(bins, 0.0));
	for (eng::usize frame = 0u; frame < frame_count; ++frame) {
		for (const auto& prototype : output.prototypes) for (eng::usize bin = 0u; bin < bins; ++bin) {
			const eng::s32 source = static_cast<eng::s32>(bin) - prototype.shift_bins[frame];
			if (source >= 0 && source < static_cast<eng::s32>(bins)) reconstruction[frame][bin] += prototype.activation[frame] * prototype.magnitude[static_cast<eng::usize>(source)];
		}
		for (eng::usize bin = 0u; bin < bins; ++bin) { source_energy += magnitudes[frame][bin] * magnitudes[frame][bin]; const double error = magnitudes[frame][bin] - reconstruction[frame][bin]; error_energy += error * error; residual_energy += residual[frame][bin] * residual[frame][bin]; }
	}
	output.metrics.magnitude_mse = error_energy / (frame_count * bins);
	output.metrics.magnitude_snr_db = error_energy <= 1.0e-12 ? 99.0 : 10.0 * std::log10(source_energy / error_energy);
	output.metrics.residual_ratio = source_energy <= 1.0e-12 ? 1.0 : residual_energy / source_energy;
	auto render_component = [&](const SpectralPrototype* selected, std::vector<eng::u8>& destination) {
		destination.assign(pcm.size(), 128u);
		std::vector<double> norm(pcm.size(), 0.0), samples(pcm.size(), 0.0);
		for (eng::usize frame = 0u; frame < frame_count; ++frame) {
			std::vector<std::complex<double>> spectrum(options.fft_size, {0.0, 0.0});
			for (eng::usize bin = 0u; bin < bins; ++bin) {
				double magnitude = 0.0;
				if (selected == nullptr) magnitude = reconstruction[frame][bin];
				else {
					const eng::s32 source = static_cast<eng::s32>(bin) - selected->shift_bins[frame];
					if (source >= 0 && source < static_cast<eng::s32>(bins)) magnitude = selected->activation[frame] * selected->magnitude[static_cast<eng::usize>(source)];
				}
				spectrum[bin] = std::polar(magnitude, phases[frame][bin]);
			}
			for (eng::usize bin = 1u; bin + 1u < bins; ++bin) spectrum[options.fft_size - bin] = std::conj(spectrum[bin]);
			fft(spectrum, true);
			const eng::usize start = frame * options.hop_samples;
			for (eng::usize i = 0u; i < options.fft_size && start + i < pcm.size(); ++i) { samples[start + i] += spectrum[i].real() * window[i]; norm[start + i] += window[i] * window[i]; }
		}
		for (eng::usize i = 0u; i < pcm.size(); ++i) { const double value = norm[i] > 1.0e-9 ? samples[i] / norm[i] : 0.0; destination[i] = static_cast<eng::u8>(static_cast<eng::s8>(std::clamp(static_cast<int>(std::lround(value)), -128, 127))); }
	};
	output.reconstructed_pcm.clear(); render_component(nullptr, output.reconstructed_pcm);
	output.reconstructed_tracks.clear();
	for (auto& prototype : output.prototypes) {
		std::vector<std::complex<double>> spectrum(options.fft_size, {0.0, 0.0});
		for (eng::usize bin = 0u; bin < bins; ++bin) spectrum[bin] = std::polar(prototype.magnitude[bin] * 127.0, prototype.phase[bin]);
		for (eng::usize bin = 1u; bin + 1u < bins; ++bin) spectrum[options.fft_size - bin] = std::conj(spectrum[bin]);
		fft(spectrum, true);
		double peak = 1.0; for (eng::usize i = 0u; i < options.fft_size; ++i) peak = std::max(peak, std::abs(spectrum[i].real()));
		prototype.pcm.resize(options.fft_size, 128u);
		for (eng::usize i = 0u; i < options.fft_size; ++i) prototype.pcm[i] = static_cast<eng::u8>(static_cast<eng::s8>(std::clamp(static_cast<int>(std::lround(spectrum[i].real() * 127.0 / peak)), -128, 127)));
		output.reconstructed_tracks.emplace_back(); render_component(&prototype, output.reconstructed_tracks.back());
	}
	output.peak_concurrent_prototypes = static_cast<eng::u8>(std::min<eng::usize>(output.prototypes.size(), 255u));
	for (const auto& prototype : output.prototypes) {
		const eng::usize limit = std::min<eng::usize>(prototype.activation.size() / 2u, 512u);
		double mean = 0.0; for (const double value : prototype.activation) mean += value; mean /= std::max<eng::usize>(1u, prototype.activation.size());
		for (eng::usize period = 2u; period <= limit; ++period) {
			double dot = 0.0, left_energy = 0.0, right_energy = 0.0;
			for (eng::usize frame = period; frame < prototype.activation.size(); ++frame) { const double left = prototype.activation[frame] - mean; const double right = prototype.activation[frame - period] - mean; dot += left * right; left_energy += left * left; right_energy += right * right; }
			const double score = left_energy <= 1.0e-12 || right_energy <= 1.0e-12 ? 0.0 : dot / std::sqrt(left_energy * right_energy);
			if (score > output.periodicity_score) { output.periodicity_score = score; output.dominant_period_frames = static_cast<eng::u32>(period); }
		}
	}
	output.requires_octamed = output.prototypes.size() > 7u;
	output.estimated_bytes = 32u;
	for (const auto& prototype : output.prototypes) output.estimated_bytes += prototype.pcm.size() + prototype.activation.size() * 6u + prototype.shift_bins.size() * 2u;
	return true;
}

} // namespace audio_compressor::dsp
