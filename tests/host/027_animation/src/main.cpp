// ============================================================================
// Test HOST-027: animaciones deterministas (Frame/Animation) independientes del render
// ============================================================================
//
// Valida `eng::graphics::Animation`: avance por TIEMPO DE JUEGO fijo (ticks), bucle o
// one-shot, y estado determinista. La representación (sprite/BLIT/CPU/playfield) NO
// participa: el contenido es el mismo.

#include <cstdio>

#include <eng/graphics/animation.hpp>

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) { std::printf("[FAIL] %s\n", what); ++g_fail; }
}

} // namespace

int main() {
	// Frames con duraciones 2, 3, 1 ticks.
	eng::graphics::Frame frames[3] {
		{ 0, 0, 16, 16, 2, 0 },
		{ 16, 0, 16, 16, 3, 7 },
		{ 32, 0, 16, 16, 1, 0 },
	};
	eng::graphics::Animation anim {};
	anim.frames = eng::Span<const eng::graphics::Frame>(frames, 3);
	anim.loop = true;
	eng::graphics::Animation::State s {};
	anim.reset(s);

	check(anim.current(s).x == 0 && s.index == 0 && s.elapsed == 0, "reset -> frame 0");
	check(!anim.advance(s, 1) && s.index == 0 && s.elapsed == 1, "1 tick mantiene frame");
	check(anim.advance(s, 1) && s.index == 1 && s.elapsed == 0, "2 ticks -> frame 1");
	check(anim.advance(s, 3) && s.index == 2 && s.elapsed == 0, "duración 3 -> frame 2");
	check(anim.advance(s, 1) && s.index == 0, "one-shot de frame 2 con loop vuelve a 0");

	// Determinismo: mismo estado inicial y misma secuencia -> mismo resultado.
	eng::graphics::Animation::State sa {}, sb {};
	anim.reset(sa); anim.reset(sb);
	for (int i = 0; i < 50; ++i) { anim.advance(sa, 1); anim.advance(sb, 1); }
	check(sa.index == sb.index && sa.elapsed == sb.elapsed, "avance determinista");

	// Un solo avance de muchos ticks == muchos de 1 tick (salvo corte final).
	eng::graphics::Animation::State s1 {}, s2 {};
	anim.reset(s1); anim.reset(s2);
	anim.advance(s1, 37);
	for (int i = 0; i < 37; ++i) anim.advance(s2, 1);
	check(s1.index == s2.index && s1.elapsed == s2.elapsed, "tick grande == ticks de 1");

	// One-shot termina en el último frame y no avanza más.
	anim.loop = false;
	eng::graphics::Animation::State s3 {};
	anim.reset(s3);
	anim.advance(s3, 1000);
	check(s3.finished && s3.index == 2, "one-shot termina en el último frame");
	const eng::u16 idx = s3.index;
	check(!anim.advance(s3, 10) && s3.index == idx, "terminada no avanza");

	// Animación vacía: nunca avanza.
	eng::graphics::Animation empty {};
	eng::graphics::Animation::State s4 {};
	check(!empty.advance(s4, 5) && s4.index == 0, "vacía no avanza");

	if (g_fail != 0) { std::printf("%d fallo(s)\n", g_fail); return 1; }
	std::printf("OK: Animation determinista (loop/one-shot/tiempo de juego) validada.\n");
	return 0;
}
