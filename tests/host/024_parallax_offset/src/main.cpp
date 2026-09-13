// ============================================================================
// Test HOST-024: offset de fondo (parallax/fijo) y compensación de split
// ============================================================================
//
// Respalda la parte PURA de la técnica de fondo RoboCod ("soft DPF")
// (engine/include/eng/field/xlimited.hpp). El plano de fondo comparte el scroll
// hardware del FG, así que el contenido del patrón hay que desfasarlo:
//
//   src = bg_x - scroll_x        (posición aparente = src + scroll_x)
//
//   - `parallax_pattern_offset_px(camx, div, period)`: fondo a 1/div.
//   - `fixed_bg_offset_px(camx, period)`:            fondo FIJO (bg_x = 0).
//   - `bg_split_rects(d, display_h, viewport_h, bg_y)`: descompone el blit de fondo
//     en 1 o 2 rectángulos contiguos para compensar el Copper split del corkscrew.
//
//   bash tools/run-host-tests.sh tests/host/024_parallax_offset

#include <cstdio>

#include <eng/field/xlimited.hpp>

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) { std::printf("[FAIL] %s\n", what); ++g_fail; }
}

using eng::s32;
using eng::u16;
using eng::field::parallax_pattern_offset_px;
using eng::field::fixed_bg_offset_px;
using eng::field::bg_split_rects;
using eng::field::bg_shift_for;
using eng::field::bg_window_for;

constexpr u16 kPeriod = 512;   // periodo horizontal del patrón (2 bytes = 16 px word)

} // namespace

int main() {
	// div=1: el fondo va igual que el FG -> sin desfase.
	for (s32 c = 0; c < 1000; c += 7) {
		check(parallax_pattern_offset_px(c, 1, kPeriod) == 0, "div=1 -> offset 0");
	}
	// div=0 se trata como 1 (config sin parallax).
	check(parallax_pattern_offset_px(123, 0, kPeriod) == 0, "div=0 -> offset 0");

	// div=2, paso de cámara 2 px: offset entero EXACTO y fondo a 1/2 sin saltos.
	{
		s32 prev = -1;
		for (s32 c = 0; c < 2 * kPeriod; c += 2) {
			const s32 src = parallax_pattern_offset_px(c, 2, kPeriod);
			check(src >= 0 && src < kPeriod, "div=2: offset en rango [0,period)");
			const s32 apparent = ((src + c) % kPeriod + kPeriod) % kPeriod;
			check(apparent == (c / 2) % kPeriod, "div=2: fondo a mitad de velocidad");
			if (prev >= 0) {
				check(apparent == (prev + 1) % kPeriod, "div=2: avanza 1 px por frame");
			}
			prev = apparent;
		}
	}

	// div=4, paso 4 px: fondo a 1/4.
	for (s32 c = 0; c < 4 * kPeriod; c += 4) {
		const s32 src = parallax_pattern_offset_px(c, 4, kPeriod);
		const s32 apparent = ((src + c) % kPeriod + kPeriod) % kPeriod;
		check(apparent == (c / 4) % kPeriod, "div=4: fondo a 1/4 de velocidad");
	}

	// Reparto al barrel shifter (`bg_shift_for`): la word y el shift deben reconstruir
	// el píxel de origen con la geometría AHRM 6 (ascendente): `destino[0]` lee
	// `q - S == src_x`. No es una identidad: valida la fórmula real del Blitter.
	for (u16 src_x = 0; src_x < kPeriod + 16u; ++src_x) {
		const auto bs = bg_shift_for(src_x);
		check(bs.shift < 16u, "shift en 0..15");
		check((bs.word_bytes & 1u) == 0u, "word alineada a byte par");
		const s32 q = static_cast<s32>(bs.word_bytes / 2u) * 16;   // píxel de la word
		check(q >= src_x, "word cubre src_x");
		check(q - static_cast<s32>(bs.shift) == src_x, "q - shift == src_x (placement)");
	}

	// Fondo FIJO (`fixed_bg_offset_px`, desired_bg=0): src = -camx mod P. La posición
	// aparente en pantalla (src + camx) queda constante => imagen estática.
	for (s32 c = -300; c < 3000; c += 7) {
		const s32 src = fixed_bg_offset_px(c, kPeriod);
		check(src >= 0 && src < kPeriod, "fijo: offset en rango [0,period)");
		check((((src + c) % kPeriod) + kPeriod) % kPeriod == 0,
		      "fijo: cancela TODO el scroll (coarse+fine)");
	}

	// Compensación del Copper split (`bg_split_rects`): 1 rect si la ventana no
	// cruza el final del anillo, 2 si lo cruza. Las mitades muestrean filas de patrón
	// CONTIGUAS y, con bg_y fijo, reconstruyen la fila de pantalla -> fondo estático
	// y SIN costura en el corte.
	constexpr s32 kViewport = 208;
	for (s32 D = 240; D <= 288; D += 16) {
		for (s32 d = 0; d < D; ++d) {
			const s32 bg_y = 0;
			const auto r = bg_split_rects(static_cast<u16>(d), static_cast<u16>(D),
			                              static_cast<u16>(kViewport),
			                              static_cast<u16>(bg_y));
			const bool wraps = (d + kViewport) > D;
			check(r.count == (wraps ? 2u : 1u), "split: count según wrap de la ventana");
			s32 covered = 0;
			for (s32 i = 0; i < r.count; ++i) {
				check(r.dest_row[i] + r.rows[i] <= D, "split: rect dentro del bitmap");
				for (s32 k = 0; k < r.rows[i]; ++k) {
					const s32 pat_row = r.src_y[i] + k;         // fila de patrón que se ve
					const s32 screen_row = covered + k;         // fila de pantalla
					check(pat_row == bg_y + screen_row,
					      "split: fila de patrón == fila de pantalla (fijo)");
				}
				covered += r.rows[i];
			}
			check(covered == kViewport, "split: cubre el viewport sin hueco");
			if (r.count == 2u) {
				check(r.src_y[0] + r.rows[0] == r.src_y[1], "split: source contiguo");
				check(r.rows[0] + r.rows[1] == kViewport, "split: filas suman el viewport");
			}
		}
	}

	// Ventana horizontal del blit (`bg_window_for`): copia [planeaddx-2,
	// planeaddx+fetch) y deja la imagen FIJA; la guarda del barrel shifter cae justo
	// antes de la cámara (no visible). Reduce el blit de 25 a ~21 words/fila.
	constexpr u16 kFetchBytes = 42;   // DDFSTRT=0x30..0xD0 => 21 words (320 px + word extra)
	for (s32 camx = 16; camx <= 80; ++camx) {
		const auto w = bg_window_for(camx, kPeriod, kFetchBytes);
		const s32 pa = ((camx + 15) / 16) * 2;
		check(w.dest_byte_off + w.words * 2 == pa + kFetchBytes, "ventana: cubre el fetch");
		check(w.dest_byte_off <= static_cast<u16>(pa), "ventana: empieza <= planeaddx");
		// Imagen fija: el patrón visible en pantalla es `x`.
		for (s32 x = 0; x < 320; x += 37) {
			const s32 pat = ((static_cast<s32>(w.src_x) + (camx + x) -
			                  static_cast<s32>(w.dest_byte_off) * 8) % kPeriod + kPeriod) % kPeriod;
			check(pat == x % kPeriod, "ventana: imagen fija");
		}
		// La guarda (<=15 px) cae dentro de la word anterior y nunca pasa de camx.
		const s32 guard = (camx - static_cast<s32>(w.dest_byte_off) * 8) & 15;
		check(static_cast<s32>(w.dest_byte_off) * 8 + guard <= camx, "ventana: guarda no visible");
		check(w.words <= kFetchBytes / 2 + 1, "ventana: no excede visible+guarda");
	}

	if (g_fail != 0) { std::printf("%d fallo(s)\n", g_fail); return 1; }
	std::printf("OK: parallax/fijo + compensacion de split (soft DPF) validados.\n");
	return 0;
}
