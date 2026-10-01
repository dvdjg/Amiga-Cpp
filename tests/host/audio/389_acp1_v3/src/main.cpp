// HOST-389: writer/parser ACP1 v3 MVP.

#include <cstdio>
#include <vector>

#include <eng/audio/acp1_v3.hpp>
#include "../../../../../host-tools/audio-compressor/src/acp1_v3_writer.hpp"

int main() {
	std::vector<std::vector<eng::u8>> stems {
		{0x80u, 0x90u, 0xa0u, 0xb0u, 0xc0u},
		{0x7fu, 0x70u, 0x60u, 0x50u, 0x40u},
	};
	std::vector<eng::u8> file;
	if (!audio_compressor::build_acp1_v3(stems, 22050u, 4u, file)) {
		std::fprintf(stderr, "writer ACP1 v3 rechazó una obra válida\n"); return 1;
	}
	eng::audio::acp1_v3::Info info{};
	if (!eng::audio::acp1_v3::parse({file.data(), file.size()}, info) || info.track_count != 2u ||
		info.unit_count != 4u || info.segment_count != 4u || info.event_count != 4u || info.timeline_samples != 5u) {
		std::fprintf(stderr, "parser ACP1 v3 no conserva las tablas del MVP\n"); return 1;
	}
	const eng::usize section_entry = eng::audio::acp1_v3::kDirectoryOffset + 4u * eng::audio::acp1_v3::kSectionEntrySize;
	file[section_entry + 4u] = 0u;
	file[section_entry + 5u] = 0u;
	file[section_entry + 6u] = 0u;
	file[section_entry + 7u] = 0u;
	if (eng::audio::acp1_v3::parse({file.data(), file.size()}, info)) {
		std::fprintf(stderr, "parser ACP1 v3 aceptó entry_size inválido\n"); return 1;
	}
	std::printf("OK: ACP1 v3 MVP writer/parser, referencias y rechazo estructural.\n");
	return 0;
}
