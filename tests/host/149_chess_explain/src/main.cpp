// ============================================================================
// Test HOST-149: explicacion en lenguaje natural (NLG por plantillas)
// ============================================================================
//
// TUTORIAL. El motor "explica" una posicion sin ningun modelo de lenguaje: extrae
// los rasgos que ya calcula la evaluacion y elige 2-4 frases de un pack ES/EN.
//
//   rasgos + evaluacion  ->  reglas de prioridad  ->  plantillas  ->  parrafo
//
// Reglas actuales (en este orden): jaque, ventaja material, dama prematura,
// retraso de desarrollo y rey en el centro. El tono (neutro/enfatico) se elige por
// la magnitud del rasgo; los conectores ("Ademas," / "Por otro lado,") enlazan las
// frases. El coste es de pocos kB y es viable en un A500 ampliado.
//
// Se ejecuta con:
//   bash tools/run-host-tests.sh tests/host/149_chess_explain

#include <cstdio>
#include <cstring>

#include <eng/board/explain/explain.hpp>
#include <eng/board/rules/chess/fen.hpp>

namespace {

using namespace eng::board;
using namespace eng::board::chess;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

Position position_from(const char* fen) {
	Position pos;
	(void)set_from_fen(pos, fen);
	return pos;
}

bool contains(const char* text, const char* needle) { return std::strstr(text, needle) != nullptr; }

/// Envuelve `explain` para el test (descarta la longitud devuelta).
void say(const Position& pos, Language language, char* out, eng::usize cap) {
	(void)explain(pos, ExplainOptions {language, Tone::Neutral, 3u}, eng::Span<char> {out, cap});
}

} // namespace

int main() {
	std::printf("Ajedrez: explicacion:\n");

	char text[256];

	// 1) Dama prematura (1.e4 e5 2.Qh5): en ES aparece "dama" y en EN "queen".
	{
		const Position pos =
		    position_from("rnbqkbnr/pppp1ppp/8/4p2Q/4P3/8/PPPP1PPP/RNB1KBNR b KQkq - 1 3");
		say(pos, Language::Spanish, text, sizeof(text));
		check(contains(text, "dama"), "ES: menciona la dama prematura");
		say(pos, Language::English, text, sizeof(text));
		check(contains(text, "queen"), "EN: mentions the premature queen");
	}

	// 2) Ventaja material (K+R vs K): "ventaja material" / "material advantage".
	{
		const Position pos = position_from("8/8/8/4k3/8/8/4K3/7R w - - 0 1");
		say(pos, Language::Spanish, text, sizeof(text));
		check(contains(text, "ventaja material"), "ES: detecta la ventaja material");
		say(pos, Language::English, text, sizeof(text));
		check(contains(text, "material advantage"), "EN: detects the material advantage");
	}

	// 3) Jaque inmediato (mate del loco): "jaque" / "check".
	{
		const Position pos =
		    position_from("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
		say(pos, Language::Spanish, text, sizeof(text));
		check(contains(text, "jaque"), "ES: detecta el jaque");
		say(pos, Language::English, text, sizeof(text));
		check(contains(text, "check"), "EN: detects the check");
	}

	// 4) Tono enfatico por gran desequilibrio (K+Q+Q vs K): lleva "!".
	{
		const Position pos = position_from("8/8/8/4k3/8/8/4K3/6QQ w - - 0 1");
		say(pos, Language::Spanish, text, sizeof(text));
		check(contains(text, "!"), "tono: el gran desequilibrio usa exclamacion");
	}

	// 5) Truncado seguro: nunca desborda el buffer.
	{
		const Position pos = position_from("8/8/8/4k3/8/8/4K3/6QQ w - - 0 1");
		char small[8];
		for (int i = 0; i < 8; ++i) {
			small[i] = 'x';
		}
		say(pos, Language::Spanish, small, sizeof(small));
		check(small[sizeof(small) - 1u] == '\0', "truncado: NUL al final");
		check(std::strlen(small) <= sizeof(small) - 1u, "truncado: no desborda");
	}

	if (g_fail == 0u) {
		std::printf("OK: NLG (dama prematura, material, jaque, tono y truncado) ES/EN\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
