// ============================================================================
// Test HOST-452: sonda de píxel del playfield (`eng::playfield::playfield_probe`)
// ============================================================================
//
// Cubre la utilidad de **verificación de render** extraída del auto-chequeo de la demo
// 121: leer el índice de color del píxel VISIBLE de un playfield corkscrew a partir de
// su `PlayfieldHardwareView` (la ventana arranca en `display_offset`; el resto del
// anillo es guarda) y la re-lectura de confirmación contra lecturas transitorias.
//
//   bash tools/run-host-tests.sh tests/host/graphics/452_playfield_probe

#include <cstdio>
#include <cstring>

#include <eng/field/playfield_probe.hpp>

using eng::u8;
using eng::u16;
using eng::u32;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

// ---------------------------------------------------------------------------
//  Vista sintética: anillo intercalado [línea][plano][byte de fila]
// ---------------------------------------------------------------------------

constexpr u16 kPlanes = 3u;
constexpr u16 kBpr = 4u;         // 4 bytes por fila y plano (32 px; la ventana es 16)
constexpr u16 kHeight = 8u;      // anillo de 8 líneas (corkscrew pequeño)
constexpr u16 kOffset = 5u;      // la ventana visible empieza en la línea 5
constexpr u16 kViewW = 16u;
constexpr u16 kViewH = 4u;

u8 g_bitmap[kHeight * kPlanes * kBpr] {};

[[nodiscard]] u8* line(u16 ring_line, u16 plane) {
	return &g_bitmap[(u32(ring_line) * kPlanes + u32(plane)) * kBpr];
}

eng::playfield::PlayfieldHardwareView make_view() {
	eng::playfield::PlayfieldHardwareView v {};
	// Frontera explícita: el array local declara el almacenamiento y el test lo usa como
	// banco Chip (no hay API DMA en juego; solo se leen direcciones).
	v.bitplanes = eng::Address<eng::MemoryKind::Chip>::from_storage(g_bitmap);
	v.bitmap_bytes_per_row = kBpr;
	v.planes = u8(kPlanes);
	v.bitmap_height = kHeight;
	v.display_height = kHeight;
	v.display_offset = kOffset;
	v.viewport_w = kViewW;
	v.viewport_h = kViewH;
	return v;
}

// ---------------------------------------------------------------------------
//  1. Lectura de píxel: bit MSB, planos y ventana (display_offset + wrap)
// ---------------------------------------------------------------------------

void test_read_visible_pixel() {
	std::memset(g_bitmap, 0, sizeof(g_bitmap));
	const eng::playfield::PlayfieldHardwareView view = make_view();

	// Píxel visible (0,0) vive en la línea del anillo 5; se pinta 0b101 (planos 0 y 2)
	// con el píxel 0 en el bit 15 de la palabra (MSB = izquierda).
	line(u16(kOffset), 0u)[0] = 0x80u;
	line(u16(kOffset), 2u)[0] = 0x80u;
	check(eng::playfield::read_visible_pixel(view, 0u, 0u) == 0b101u,
	      "pixel (0,0) -> linea del anillo display_offset con planos 0+2");

	// El bit 7 del píxel 7 (byte 0, bit 0) pertenece al plano 1.
	line(u16(kOffset), 1u)[0] = 0x01u;
	check(eng::playfield::read_visible_pixel(view, 7u, 0u) == 0b010u,
	      "pixel (7,0) -> bit bajo del byte 0 (MSB=izquierda)");

	// Píxel 8 = byte 1, bit 7 del byte (MSB); píxel 15 = byte 1, bit 0 (LSB del byte).
	line(u16(kOffset), 0u)[1] = 0x81u;
	check(eng::playfield::read_visible_pixel(view, 8u, 0u) == 0b001u,
	      "pixel (8,0) -> byte 1 bit 7");
	check(eng::playfield::read_visible_pixel(view, 15u, 0u) == 0b001u,
	      "pixel (15,0) -> byte 1 bit 0 (LSB del byte)");

	// Wrap del anillo: sy=3 -> ring (5+3)%8 = 0 (la guarda no se ve; el anillo envuelve).
	// Píxel (2,3) -> línea 0, plano 2, bit 13 de la palabra 0.
	line(0u, 2u)[0] = 0x20u;
	check(eng::playfield::read_visible_pixel(view, 2u, 3u) == 0b100u,
	      "wrap: sy=3 -> linea 0 del anillo (guarda) con el plano correcto");
	// Y fuera del viewport no se lee aunque el anillo tenga datos (x=25 no es columna de la
	// fila: el guard de viewport_w corta antes de calcular direccion).
	line(7u, 1u)[3] = 0x40u;
	check(eng::playfield::read_visible_pixel(view, 25u, 2u) == 0u,
	      "fuera de viewport_w -> 0 aunque el anillo envuelva");
}

// ---------------------------------------------------------------------------
//  2. Límites: viewport, buffer nulo, planos/altura cero
// ---------------------------------------------------------------------------

void test_bounds() {
	std::memset(g_bitmap, 0, sizeof(g_bitmap));
	const eng::playfield::PlayfieldHardwareView view = make_view();
	check(eng::playfield::read_visible_pixel(view, kViewW, 0u) == 0u, "sx == viewport_w -> 0");
	check(eng::playfield::read_visible_pixel(view, 0u, kViewH) == 0u, "sy == viewport_h -> 0");

	eng::playfield::PlayfieldHardwareView nullv = make_view();
	nullv.bitplanes = {};
	check(eng::playfield::read_visible_pixel(nullv, 0u, 0u) == 0u, "bitplanes nulo -> 0");

	eng::playfield::PlayfieldHardwareView zerov = make_view();
	zerov.planes = 0u;
	check(eng::playfield::read_visible_pixel(zerov, 0u, 0u) == 0u, "planes=0 -> 0");
	zerov = make_view();
	zerov.display_height = 0u;
	check(eng::playfield::read_visible_pixel(zerov, 0u, 0u) == 0u, "display_height=0 -> 0");
}

// ---------------------------------------------------------------------------
//  3. Re-lectura de confirmación (read_confirm)
// ---------------------------------------------------------------------------

void test_read_confirm() {
	// --- caso bueno: confirma a la primera y NO paga más lecturas.
	{
		u32 calls = 0u;
		const u8 v = eng::playfield::read_confirm(
			[&] {
				++calls;
				return u8(31u);
			},
			u8(31u), u8(3u));
		check(v == 31u && calls == 1u, "confirm: primer valor correcto -> 1 lectura");
	}
	// --- transitorio: se confirma dentro de `passes` (el patrón del auto-chequeo).
	{
		u32 calls = 0u;
		const u8 seq[3] = {30u, 30u, 31u};
		const u8 v = eng::playfield::read_confirm(
			[&] { return seq[calls < 3u ? calls++ : 2u]; }, u8(31u), u8(3u));
		check(v == 31u && calls == 3u, "confirm: transitorio 30/30/31 -> confirma en 3");
	}
	// --- fallo real: se agota `passes` y devuelve el último leído.
	{
		u32 calls = 0u;
		const u8 v = eng::playfield::read_confirm(
			[&] {
				++calls;
				return u8(30u);
			},
			u8(31u), u8(3u));
		check(v == 30u && calls == 3u, "confirm: siempre 30 -> ultimo valor tras 3 lecturas");
	}
	// --- passes=1 = sin confirmación (una sola lectura).
	{
		u32 calls = 0u;
		const u8 v = eng::playfield::read_confirm(
			[&] {
				++calls;
				return u8(30u);
			},
			u8(31u), u8(1u));
		check(v == 30u && calls == 1u, "confirm: passes=1 -> una lectura");
	}
}

// ---------------------------------------------------------------------------
//  4. Atajo sobre la vista (read_visible_pixel_confirm)
// ---------------------------------------------------------------------------

void test_wrapper() {
	std::memset(g_bitmap, 0, sizeof(g_bitmap));
	const eng::playfield::PlayfieldHardwareView view = make_view();
	// Píxel (3,1) -> ring 6, plano 0..2 = 0b100 (solo plano 2), píxel 3 -> bit 12.
	line(6u, 2u)[0] = 0x10u;
	check(eng::playfield::read_visible_pixel_confirm(view, 3u, 1u, u8(0b100u)) == 0b100u,
	      "wrapper: valor correcto confirmado");
	check(eng::playfield::read_visible_pixel_confirm(view, 3u, 1u, u8(31u)) == 0b100u,
	      "wrapper: valor no esperado se devuelve tal cual (tras confirmar)");
}

} // namespace

int main() {
	test_read_visible_pixel();
	test_bounds();
	test_read_confirm();
	test_wrapper();
	if (g_fail == 0) {
		std::printf("OK: sonda de playfield (lectura, ventana/wrap, limites y confirmacion) "
			    "validada.\n");
		return 0;
	}
	std::printf("[FAIL] %d comprobaciones\n", g_fail);
	return 1;
}
