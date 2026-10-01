#pragma once

/// \\file acp1_v3.hpp
/// Parser sin heap del MVP ACP1 v3: Units, Segments, PayloadBytes, Tracks y Events.
///
/// El contrato binario completo y sus secciones opcionales están descritos en
/// `docs/engine/architecture/AUDIO_COMPRESSION.md`. Este parser valida el directorio completo,
/// referencias y payloads del MVP; no intenta reproducir todavía síntesis, envolventes ni cues.

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace eng::audio::acp1_v3 {

inline constexpr eng::usize kHeaderSize = 64u;
inline constexpr eng::usize kSectionEntrySize = 16u;
inline constexpr eng::usize kSectionCount = 17u;
inline constexpr eng::usize kDirectoryOffset = 64u;
inline constexpr eng::usize kDirectorySize = kSectionCount * kSectionEntrySize;
inline constexpr eng::u16 kRequired = 1u;

inline constexpr eng::u16 kUnits = 1u;
inline constexpr eng::u16 kSegments = 2u;
inline constexpr eng::u16 kPayloadBytes = 3u;
inline constexpr eng::u16 kTracks = 4u;
inline constexpr eng::u16 kEvents = 5u;

inline constexpr eng::u32 kUnitSize = 24u;
inline constexpr eng::u32 kSegmentSize = 28u;
inline constexpr eng::u32 kTrackSize = 40u;
inline constexpr eng::u32 kEventSize = 44u;

struct Section {
	eng::u16 id = 0u;
	eng::u16 flags = 0u;
	eng::u32 entry_size = 0u;
	eng::u32 entry_count = 0u;
	eng::u32 offset = 0u;
};

struct Info {
	eng::u16 major = 0u;
	eng::u16 minor = 0u;
	eng::u32 sample_rate = 0u;
	eng::u64 timeline_samples = 0u;
	eng::u8 track_limit = 0u;
	eng::u8 required_paula_voices = 0u;
	eng::u8 required_mixer_voices = 0u;
	eng::u32 file_size = 0u;
	eng::u32 unit_count = 0u;
	eng::u32 segment_count = 0u;
	eng::u32 track_count = 0u;
	eng::u32 event_count = 0u;
	Section sections[kSectionCount] {};
};

[[nodiscard]] inline eng::u16 rd16(eng::Span<const eng::u8> file, eng::usize at) noexcept {
	return static_cast<eng::u16>(file[at]) |
		static_cast<eng::u16>(static_cast<eng::u16>(file[at + 1u]) << 8u);
}

[[nodiscard]] inline eng::u32 rd32(eng::Span<const eng::u8> file, eng::usize at) noexcept {
	return static_cast<eng::u32>(file[at]) |
		(static_cast<eng::u32>(file[at + 1u]) << 8u) |
		(static_cast<eng::u32>(file[at + 2u]) << 16u) |
		(static_cast<eng::u32>(file[at + 3u]) << 24u);
}

[[nodiscard]] inline eng::u64 rd64(eng::Span<const eng::u8> file, eng::usize at) noexcept {
	return static_cast<eng::u64>(rd32(file, at)) |
		(static_cast<eng::u64>(rd32(file, at + 4u)) << 32u);
}

[[nodiscard]] inline bool extent_ok(const Section& section, eng::u32 file_size) noexcept {
	const eng::u64 extent = static_cast<eng::u64>(section.entry_size) * section.entry_count;
	return extent <= file_size && (section.entry_count == 0u
		? section.offset == 0u
		: section.offset >= kHeaderSize + kDirectorySize && section.offset <= file_size - extent);
}

[[nodiscard]] inline bool parse(eng::Span<const eng::u8> file, Info& out) noexcept {
	if (file.size() < kHeaderSize + kDirectorySize || file[0] != 'A' || file[1] != 'C' ||
		file[2] != 'P' || file[3] != '1' || rd16(file, 4u) != 3u || rd16(file, 6u) != 0u ||
		rd16(file, 8u) != kHeaderSize || rd16(file, 10u) != 0u || rd32(file, 32u) != kDirectoryOffset ||
		rd16(file, 24u) != kSectionCount || rd16(file, 42u) != 0u || rd32(file, 36u) != file.size()) return false;
	Info parsed {};
	parsed.major = 3u;
	parsed.minor = 0u;
	parsed.sample_rate = rd32(file, 12u);
	parsed.timeline_samples = rd64(file, 16u);
	parsed.track_limit = file[26u];
	parsed.required_paula_voices = file[27u];
	parsed.required_mixer_voices = file[28u];
	parsed.file_size = rd32(file, 36u);
	if (parsed.sample_rate == 0u || parsed.sample_rate > 65535u || parsed.timeline_samples == 0u ||
		parsed.track_limit == 0u || parsed.track_limit > 7u || parsed.required_paula_voices > 3u ||
		parsed.required_mixer_voices > 4u || rd32(file, 44u) != 0u) return false;
	for (eng::usize i = 0u; i < kSectionCount; ++i) {
		const eng::usize at = kDirectoryOffset + i * kSectionEntrySize;
		Section& section = parsed.sections[i];
		section.id = rd16(file, at);
		section.flags = rd16(file, at + 2u);
		section.entry_size = rd32(file, at + 4u);
		section.entry_count = rd32(file, at + 8u);
		section.offset = rd32(file, at + 12u);
		if (section.id != i + 1u || (section.flags & ~kRequired) != 0u || !extent_ok(section, parsed.file_size)) return false;
	}
	const auto& units = parsed.sections[kUnits - 1u];
	const auto& segments = parsed.sections[kSegments - 1u];
	const auto& payloads = parsed.sections[kPayloadBytes - 1u];
	const auto& tracks = parsed.sections[kTracks - 1u];
	const auto& events = parsed.sections[kEvents - 1u];
	if (units.entry_size != kUnitSize || segments.entry_size != kSegmentSize || tracks.entry_size != kTrackSize ||
		events.entry_size != kEventSize || payloads.entry_size != 1u || (units.flags & kRequired) == 0u ||
		(tracks.flags & kRequired) == 0u || (events.flags & kRequired) == 0u || units.entry_count == 0u ||
		tracks.entry_count == 0u || tracks.entry_count > 7u || events.entry_count == 0u ||
		((segments.entry_count != 0u) != ((segments.flags & kRequired) != 0u)) ||
		((payloads.entry_count != 0u) != ((payloads.flags & kRequired) != 0u))) return false;
	for (eng::usize i = 0u; i < kSectionCount; ++i) {
		const Section& a = parsed.sections[i];
		if (a.entry_count == 0u) continue;
		const eng::u64 a_end = static_cast<eng::u64>(a.offset) + static_cast<eng::u64>(a.entry_size) * a.entry_count;
		for (eng::usize j = i + 1u; j < kSectionCount; ++j) {
			const Section& b = parsed.sections[j];
			if (b.entry_count == 0u) continue;
			const eng::u64 b_end = static_cast<eng::u64>(b.offset) + static_cast<eng::u64>(b.entry_size) * b.entry_count;
			if (!(a_end <= b.offset || b_end <= a.offset)) return false;
		}
	}
	parsed.unit_count = units.entry_count;
	parsed.segment_count = segments.entry_count;
	parsed.track_count = tracks.entry_count;
	parsed.event_count = events.entry_count;
	for (eng::u32 i = 0u; i < parsed.unit_count; ++i) {
		const eng::usize at = units.offset + static_cast<eng::usize>(i) * kUnitSize;
		if (rd32(file, at) != i || file[at + 4u] > 2u || file[at + 5u] != 0u || rd32(file, at + 20u) != 0u) return false;
		const eng::u32 decoded = rd32(file, at + 6u);
		const eng::u32 first = rd32(file, at + 10u);
		const eng::u16 count = rd16(file, at + 14u);
		if ((count == 0u) != (first == 0xffffffffu) || decoded == 0u) return false;
		if (static_cast<eng::u64>(first) + count > parsed.segment_count && count != 0u) return false;
	}
	for (eng::u32 i = 0u; i < parsed.segment_count; ++i) {
		const eng::usize at = segments.offset + static_cast<eng::usize>(i) * kSegmentSize;
		const eng::u32 unit = rd32(file, at);
		const eng::u32 start = rd32(file, at + 4u);
		const eng::u32 count = rd32(file, at + 8u);
		const eng::u32 payload_at = rd32(file, at + 12u);
		const eng::u32 payload_size = rd32(file, at + 16u);
		if (unit >= parsed.unit_count || count == 0u || payload_size == 0u || rd16(file, at + 20u) > 7u ||
			file[at + 23u] != 0u || rd16(file, at + 26u) != 0u || payload_at < payloads.offset || payload_at > payloads.offset + payloads.entry_count ||
			payload_size > payloads.offset + payloads.entry_count - payload_at) return false;
		const eng::usize unit_at = units.offset + static_cast<eng::usize>(unit) * kUnitSize;
		if (start > rd32(file, unit_at + 6u) || count > rd32(file, unit_at + 6u) - start) return false;
	}
	for (eng::u32 i = 0u; i < parsed.event_count; ++i) {
		const eng::usize at = events.offset + static_cast<eng::usize>(i) * kEventSize;
		const eng::u16 track = rd16(file, at);
		const eng::u32 duration = rd32(file, at + 12u);
		const eng::u32 unit = rd32(file, at + 16u);
		if (track >= parsed.track_count || rd16(file, at + 2u) != 0u || duration == 0u || unit >= parsed.unit_count ||
			rd16(file, at + 38u) != 0u || rd32(file, at + 40u) != 0u) return false;
	}
	out = parsed;
	return true;
}

} // namespace eng::audio::acp1_v3
