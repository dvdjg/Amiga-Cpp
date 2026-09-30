#pragma once

/// \file acp1.hpp
/// Parser sin heap para el subconjunto sincronizado ACP1 v1 descrito en
/// `docs/engine/architecture/AUDIO_COMPRESSION.md`.

#include <eng/audio/auzx.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace eng::audio::acp1 {

/// Tamaño de la cabecera ACP1 v1.
inline constexpr eng::usize kHeaderSize = 40u;
/// Tamaño serializado de cada unidad ACP1 v1.
inline constexpr eng::usize kUnitSize = 24u;
/// Tamaño serializado de cada track ACP1 v1.
inline constexpr eng::usize kTrackSize = 8u;
/// Tamaño serializado de cada evento ACP1 v1.
inline constexpr eng::usize kEventSize = 20u;
/// Máximo de tracks: tres destinos Paula y cuatro voces mixer.
inline constexpr eng::u8 kMaxTracks = 7u;

/// Metadatos de un ACP1 v1 validado.
struct Info {
	eng::u32 sample_rate = 0u; ///< Frecuencia común de reproducción, en Hz.
	eng::u32 total_samples = 0u; ///< Duración sincronizada de cada pista, en muestras.
	eng::u16 unit_count = 0u; ///< Número de unidades AUZX validadas.
	eng::u8 track_count = 0u; ///< Número de tracks y eventos sincronizados.
	eng::u32 units_offset = 0u; ///< Offset absoluto de la tabla UnitHeader.
	eng::u32 tracks_offset = 0u; ///< Offset absoluto de la tabla TrackHeader.
	eng::u32 events_offset = 0u; ///< Offset absoluto de la tabla TrackEvent.
	eng::u32 file_size = 0u; ///< Tamaño total del ACP1 v1 validado.
};

/// Vista validada de una unidad ACP1 y su payload AUZX.
struct Unit {
	eng::u32 id = 0u; ///< ID consecutivo de unidad.
	eng::u32 decoded_samples = 0u; ///< Muestras válidas reconstruidas (puede haber padding AUZX).
	eng::u8 gain = 0u; ///< Ganancia de referencia en escala 0..255.
	eng::Span<const eng::u8> payload {}; ///< Archivo AUZX embebido, sin copiar.
};

/// Vista validada de una pista y su único evento v1.
struct Track {
	eng::u8 destination = 0u; ///< Destino 0..2 Paula o 3..6 mixer.
	eng::u32 unit_id = 0u; ///< Unidad reproducida por el evento único v1.
	eng::u32 start_sample = 0u; ///< Inicio del evento en la línea temporal ACP1.
	eng::u32 duration = 0u; ///< Duración sincronizada en muestras.
	eng::u8 gain = 0u; ///< Ganancia del evento en escala 0..255.
};

/// Lee un u16 little-endian después de validar el rango del archivo.
[[nodiscard]] inline eng::u16 rd16(eng::Span<const eng::u8> file, eng::usize offset) noexcept {
	return static_cast<eng::u16>(static_cast<eng::u16>(file[offset]) |
		static_cast<eng::u16>(static_cast<eng::u16>(file[offset + 1u]) << 8u));
}

/// Lee un u32 little-endian después de validar el rango del archivo.
[[nodiscard]] inline eng::u32 rd32(eng::Span<const eng::u8> file, eng::usize offset) noexcept {
	return static_cast<eng::u32>(file[offset]) | (static_cast<eng::u32>(file[offset + 1u]) << 8u) |
		(static_cast<eng::u32>(file[offset + 2u]) << 16u) | (static_cast<eng::u32>(file[offset + 3u]) << 24u);
}

/// Escribe un u16 little-endian en el buffer ACP1 de salida host.
inline void wr16(eng::Span<eng::u8> file, eng::usize offset, eng::u16 value) noexcept {
	file[offset] = static_cast<eng::u8>(value);
	file[offset + 1u] = static_cast<eng::u8>(value >> 8u);
}

/// Escribe un u32 little-endian en el buffer ACP1 de salida host.
inline void wr32(eng::Span<eng::u8> file, eng::usize offset, eng::u32 value) noexcept {
	file[offset] = static_cast<eng::u8>(value);
	file[offset + 1u] = static_cast<eng::u8>(value >> 8u);
	file[offset + 2u] = static_cast<eng::u8>(value >> 16u);
	file[offset + 3u] = static_cast<eng::u8>(value >> 24u);
}

/// Valida la estructura completa, sus tablas y cada AUZX embebido; no reserva memoria.
[[nodiscard]] inline bool parse(eng::Span<const eng::u8> file, Info& out) noexcept {
	if (file.size() < kHeaderSize || file[0] != 'A' || file[1] != 'C' || file[2] != 'P' || file[3] != '1') return false;
	if (rd16(file, 4u) != 1u || rd16(file, 6u) != 0u || file[15] != 0u || rd32(file, 20u) != 0u) return false;
	Info parsed {};
	parsed.sample_rate = rd32(file, 8u);
	parsed.unit_count = rd16(file, 12u);
	parsed.track_count = file[14];
	parsed.total_samples = rd32(file, 16u);
	parsed.units_offset = rd32(file, 24u);
	parsed.tracks_offset = rd32(file, 28u);
	parsed.events_offset = rd32(file, 32u);
	parsed.file_size = rd32(file, 36u);
	if (parsed.sample_rate == 0u || parsed.sample_rate > 65535u || parsed.total_samples == 0u ||
		parsed.track_count == 0u || parsed.track_count > kMaxTracks || parsed.unit_count == 0u ||
		parsed.unit_count > parsed.track_count ||
		parsed.units_offset != kHeaderSize || parsed.file_size != file.size()) return false;
	const eng::usize units_end = parsed.units_offset + static_cast<eng::usize>(parsed.unit_count) * kUnitSize;
	if (units_end > parsed.file_size || parsed.tracks_offset < units_end || parsed.tracks_offset > parsed.file_size ||
		parsed.events_offset < parsed.tracks_offset || parsed.events_offset > parsed.file_size) return false;
	const eng::usize track_bytes = static_cast<eng::usize>(parsed.track_count) * kTrackSize;
	const eng::usize event_bytes = static_cast<eng::usize>(parsed.track_count) * kEventSize;
	if (track_bytes != parsed.events_offset - parsed.tracks_offset ||
		event_bytes != parsed.file_size - parsed.events_offset) return false;
	eng::usize payload_cursor = units_end;
	for (eng::u16 i = 0u; i < parsed.unit_count; ++i) {
		const eng::usize at = parsed.units_offset + static_cast<eng::usize>(i) * kUnitSize;
		const eng::u32 id = rd32(file, at);
		const eng::u32 offset = rd32(file, at + 4u);
		const eng::u32 size = rd32(file, at + 8u);
		const eng::u32 samples = rd32(file, at + 12u);
		if (id != i || offset != payload_cursor || size == 0u || samples != parsed.total_samples ||
			file[at + 16u] != 1u || file[at + 17u] != 0u || file[at + 18u] != 255u || file[at + 19u] != 0u ||
		rd16(file, at + 20u) != 0u || rd16(file, at + 22u) != 0u || offset > parsed.tracks_offset ||
		size > parsed.tracks_offset - offset) return false;
		eng::audio::auzx::Header auzx_header {};
		const eng::Span<const eng::u8> payload {file.data() + offset, size};
		if (!eng::audio::auzx::parse(payload, auzx_header) || auzx_header.sample_rate != parsed.sample_rate ||
			auzx_header.total_samples != samples || auzx_header.chunk_samples == 0u || auzx_header.num_chunks == 0u ||
			(static_cast<eng::u64>(auzx_header.total_samples) + auzx_header.chunk_samples - 1u) /
				auzx_header.chunk_samples != auzx_header.num_chunks) return false;
		for (eng::u16 chunk_index = 0u; chunk_index < auzx_header.num_chunks; ++chunk_index) {
			eng::u32 chunk_offset = 0u, chunk_size = 0u;
			if (!eng::audio::auzx::chunk_extent(auzx_header, payload, chunk_index, chunk_offset, chunk_size) ||
				chunk_size == 0u || chunk_offset < auzx_header.data_offset || chunk_offset > payload.size() ||
				chunk_size > payload.size() - chunk_offset) return false;
		}
		payload_cursor += size;
	}
	if (payload_cursor != parsed.tracks_offset) return false;
	eng::u8 destinations = 0u;
	for (eng::u8 i = 0u; i < parsed.track_count; ++i) {
		const eng::usize track_at = parsed.tracks_offset + static_cast<eng::usize>(i) * kTrackSize;
		const eng::u8 destination = file[track_at];
		if (destination >= kMaxTracks || (destinations & static_cast<eng::u8>(1u << destination)) != 0u ||
			file[track_at + 1u] != 0u || rd16(file, track_at + 2u) != 1u ||
			rd32(file, track_at + 4u) != parsed.events_offset + static_cast<eng::u32>(i) * kEventSize) return false;
		destinations = static_cast<eng::u8>(destinations | static_cast<eng::u8>(1u << destination));
		const eng::usize event_at = parsed.events_offset + static_cast<eng::usize>(i) * kEventSize;
		if (rd32(file, event_at) >= parsed.unit_count || rd32(file, event_at + 4u) != 0u ||
			rd32(file, event_at + 8u) != parsed.total_samples || file[event_at + 12u] != 255u ||
			file[event_at + 13u] != 0u || rd16(file, event_at + 14u) != 0u ||
		rd16(file, event_at + 16u) != 0u || rd16(file, event_at + 18u) != 0u) return false;
	}
	out = parsed;
	return true;
}

/// Devuelve la unidad validada `index`, incluidos los bytes de su AUZX embebido.
[[nodiscard]] inline bool unit(eng::Span<const eng::u8> file, const Info& info, eng::u16 index, Unit& out) noexcept {
	if (index >= info.unit_count) return false;
	const eng::usize at = info.units_offset + static_cast<eng::usize>(index) * kUnitSize;
	const eng::u32 offset = rd32(file, at + 4u);
	const eng::u32 size = rd32(file, at + 8u);
	out.id = rd32(file, at);
	out.decoded_samples = rd32(file, at + 12u);
	out.gain = file[at + 18u];
	out.payload = {file.data() + offset, size};
	return true;
}

/// Devuelve la pista y su evento sincronizado `index` después de parsear el archivo.
[[nodiscard]] inline bool track(eng::Span<const eng::u8> file, const Info& info, eng::u8 index, Track& out) noexcept {
	if (index >= info.track_count) return false;
	const eng::usize track_at = info.tracks_offset + static_cast<eng::usize>(index) * kTrackSize;
	const eng::usize event_at = info.events_offset + static_cast<eng::usize>(index) * kEventSize;
	out.destination = file[track_at];
	out.unit_id = rd32(file, event_at);
	out.start_sample = rd32(file, event_at + 4u);
	out.duration = rd32(file, event_at + 8u);
	out.gain = file[event_at + 12u];
	return true;
}

} // namespace eng::audio::acp1
