// ============================================================================
// pack_book: empaqueta un libro de aperturas en el formato binario del motor.
// ============================================================================
//
// Herramienta host que usa el propio engine (header-only) para convertir un fichero
// de texto en un blob de entradas `BookEntry` (12 B, ordenadas por clave Zobrist),
// listo para servirse por bloques con `FileBlockSource` (PC) o el trackloader (Amiga).
//
// Formato de entrada (una línea por jugada):
//
//   FEN ; uci ; score ; nombre-opcional
//
// Ejemplo:
//   rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1 ; e2e4 ; 20 ; Apertura
//
// La clave Zobrist y la jugada se resuelven con las reglas reales del engine, así
// que el binario es consistente con `probe_book`. `score` va en centipeones.
//
// Uso:
//   pack_book <entrada.txt> <salida.bin>

#include <cstdio>

#include <eng/board/knowledge/book.hpp>
#include <eng/board/rules/chess/notation.hpp>
#include <eng/board/rules/chess/rules.hpp>
#include <eng/core/util/binary.hpp>
#include <eng/core/util/string_view.hpp>
#include <eng/core/util/text.hpp>

using namespace eng;
using namespace eng::board;
using namespace eng::board::chess;
using eng::util::ByteWriter;
using eng::util::StringView;

namespace {

constexpr eng::usize kMaxLines = 4096u;
constexpr eng::usize kInputBytes = 1u << 20; // 1 MB de texto de entrada

} // namespace

int main(int argc, char** argv) {
	if (argc < 3) {
		std::printf("uso: pack_book <entrada.txt> <salida.bin>\n");
		return 2;
	}

	std::FILE* input = std::fopen(argv[1], "rb");
	if (input == nullptr) {
		std::printf("pack_book: no se puede abrir '%s'\n", argv[1]);
		return 1;
	}
	static char text[kInputBytes];
	const eng::usize bytes = std::fread(text, 1u, kInputBytes - 1u, input);
	std::fclose(input);

	static BookEntry entries[kMaxLines];
	eng::u32 count = 0u;
	eng::u32 skipped = 0u;

	StringView rest {text, bytes};
	while (!rest.empty() && count < kMaxLines) {
		StringView line = eng::util::trim(eng::util::split_next(rest, '\n'));
		if (line.empty() || line[0] == '#') {
			continue;
		}
		StringView fields = line;
		const StringView fen = eng::util::trim(eng::util::split_next(fields, ';'));
		const StringView uci = eng::util::trim(eng::util::split_next(fields, ';'));
		const StringView score_text = eng::util::trim(eng::util::split_next(fields, ';'));

		s32 score = 0;
		(void)eng::util::parse_s32(score_text, score);

		Position pos;
		if (!set_from_fen(pos, fen)) {
			std::printf("pack_book: FEN invalida: %.*s\n", (int)fen.size(), fen.data());
			++skipped;
			continue;
		}
		MoveList legal;
		generate_legal(pos, legal);
		Move found = kNoMove;
		for (eng::usize i = 0u; i < legal.size(); ++i) {
			char buffer[8];
			const eng::usize n = to_uci(legal[i], eng::Span<char> {buffer, sizeof(buffer)});
			if (StringView {buffer, n} == uci) {
				found = legal[i];
				break;
			}
		}
		if (move_none(found)) {
			std::printf("pack_book: jugada '%s' no legal en la posicion\n", uci.data());
			++skipped;
			continue;
		}
		entries[count] = BookEntry {compute_key(pos), found, static_cast<s16>(score), 0u};
		++count;
	}

	BookBuilder builder {eng::Span<BookEntry> {entries, kMaxLines}};
	for (eng::u32 i = 0u; i < count; ++i) {
		(void)builder.add(entries[i].key, entries[i].move, entries[i].score, entries[i].name_id);
	}
	builder.finalize();

	static eng::u8 blob[kMaxLines * sizeof(BookEntry)];
	ByteWriter writer {eng::Span<u8> {blob, sizeof(blob)}};
	for (eng::u32 i = 0u; i < count; ++i) {
		if (!write_book_entry(writer, builder.entries()[i])) {
			std::printf("pack_book: no cabe una entrada mas\n");
			return 1;
		}
	}

	std::FILE* output = std::fopen(argv[2], "wb");
	if (output == nullptr) {
		std::printf("pack_book: no se puede crear '%s'\n", argv[2]);
		return 1;
	}
	const eng::usize written = std::fwrite(blob, 1u, writer.position(), output);
	std::fclose(output);

	std::printf("pack_book: %u entradas, %u omitidas, %u bytes -> %s\n", count, skipped,
	            static_cast<unsigned>(written), argv[2]);
	return (written == writer.position()) ? 0 : 1;
}
