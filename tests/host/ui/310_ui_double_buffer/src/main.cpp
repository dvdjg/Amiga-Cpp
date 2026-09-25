// ============================================================================
// Test HOST-310: GUI / framebuffer — pantalla de doble buffer (flicker-free).
// ============================================================================
//
// Valida `eng/ui/double_buffer.hpp`: `DoubleBufferScreen` compone en el buffer TRASERO mientras
// el delantero no cambia; `flip()` intercambia y llama al publisher (parcheo de display). El
// objetivo es que el display nunca muestre una composicion a medias (sin parpadeo).
// Ver ROADMAP_GUI.md (paso de doble buffer) y GUI_LIBRARY.md §14.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/ui/310_ui_double_buffer

#include <cstdio>

#include <eng/core/types/box.hpp>
#include <eng/ui/compositor.hpp>
#include <eng/ui/double_buffer.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

constexpr eng::u16 kSW = 64u;
constexpr eng::u16 kSH = 32u;
constexpr eng::u16 kRow = static_cast<eng::u16>(((kSW / 8u) + 3u) & ~3u); // 8
constexpr eng::u32 kPlaneStride = static_cast<eng::u32>(kRow) * kSH;     // 256
constexpr eng::u8 kPlanes = 4u;
constexpr eng::u32 kBytes = kPlaneStride * kPlanes;                      // 1024

eng::u8 pixel_at(const eng::u8* base, eng::s16 x, eng::s16 y) {
	eng::u8 c = 0u;
	for (eng::u8 p = 0u; p < kPlanes; ++p) {
		const eng::u16* w = reinterpret_cast<const eng::u16*>(
			base + p * kPlaneStride + static_cast<eng::u32>(y) * kRow +
			static_cast<eng::u32>(x / 16) * 2u);
		const eng::u8 bit = static_cast<eng::u8>((*w >> (15u - (x & 15))) & 1u);
		c = static_cast<eng::u8>(c | static_cast<eng::u8>(bit << p));
	}
	return c;
}

} // namespace

int main() {
	alignas(2) eng::u8 mem_a[kBytes] {};
	alignas(2) eng::u8 mem_b[kBytes] {};
	alignas(2) eng::u8 win_mem[128u * 4u] {};

	eng::ui::DoubleBufferScreen db;
	check(db.bind(mem_a, mem_b, kBytes, kSW, kSH, kPlanes), "bind doble buffer");

	// El publisher debe VIVIR mientras la pantalla lo pueda llamar (`FunctionRef` no propietario).
	int publishes = 0;
	auto publish = [&publishes] { ++publishes; };
	db.set_publisher(publish);

	eng::ui::Compositor comp;
	comp.set_desktop(0u);

	eng::ui::CompWindow* w = comp.add();
	check(w != nullptr, "add ventana");
	check(w->backing.bind(win_mem, sizeof(win_mem), 32u, 32u, kPlanes), "bind backing");
	w->frame = eng::ui::Rect {0, 0, 32u, 32u};
	w->backing.surface.fill_rect(0, 0, 32u, 32u, 3u);

	// 1) Componer en el TRASERO sin flip: el delantero NO cambia.
	eng::field::Surface& front_before = db.front();
	comp.set_screen(db.back());
	comp.damage_screen(eng::ui::Rect {0, 0, kSW, kSH});
	comp.present();
	check(pixel_at(mem_a, 10, 10) == 3u, "trasero compuesto (mem_a)");
	check(pixel_at(mem_b, 10, 10) == 0u, "delantero intacto hasta flip (mem_b)");
	check(publishes == 0, "publisher no llamado sin flip");

	// 2) flip: publica y el delantero pasa a ser el compuesto.
	db.flip();
	check(publishes == 1, "publisher llamado en flip");
	check(&db.front() != &front_before, "flip intercambia buffers");
	check(pixel_at(mem_a, 10, 10) == 3u, "delantero ahora es el compuesto");

	// 3) Segunda composicion: el nuevo trasero (mem_b) se compone; el delantero (mem_a) no cambia.
	w->backing.surface.fill_rect(0, 0, 32u, 32u, 5u);
	comp.set_screen(db.back());
	comp.damage_screen(eng::ui::Rect {0, 0, kSW, kSH});
	comp.present();
	check(pixel_at(mem_b, 10, 10) == 5u, "nuevo trasero compuesto (mem_b)");
	check(pixel_at(mem_a, 10, 10) == 3u, "delantero sigue mostrando el frame anterior");

	db.flip();
	check(pixel_at(mem_b, 10, 10) == 5u, "tras flip, delantero = frame 2");
	check(publishes == 2, "publisher en el segundo flip");

	// 4) `present` compone y publica en un paso.
	eng::ui::Compositor comp2;
	comp2.set_desktop(0u);
	eng::ui::CompWindow* w2 = comp2.add();
	check(w2 != nullptr, "add ventana 2");
	check(w2->backing.bind(win_mem, sizeof(win_mem), 32u, 32u, kPlanes), "bind backing 2");
	w2->frame = eng::ui::Rect {8, 8, 16u, 16u};
	w2->backing.surface.fill_rect(0, 0, 16u, 16u, 7u);
	comp2.damage_screen(eng::ui::Rect {0, 0, kSW, kSH});
	db.present(comp2);
	check(publishes == 3, "present publica");
	check(pixel_at(mem_a, 10, 10) == 7u, "contenido de present visible en el delantero");

	if (failures == 0) {
		std::printf("OK: pantalla de doble buffer (flicker-free) validada.\n");
	} else {
		std::printf("FALLOS: %d\n", failures);
	}
	return failures == 0 ? 0 : 1;
}
