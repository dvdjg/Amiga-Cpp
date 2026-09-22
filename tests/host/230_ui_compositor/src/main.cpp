// ============================================================================
// Test HOST-230: GUI G7 — compositor con backing store.
// ============================================================================
//
// Valida `eng/ui/compositor.hpp` y `eng/ui/backing.hpp`: composicion fondo->frente, mover sin
// repintar vecinas, redimensionar que solo marca su ventana y pool que falla limpio. Se leen los
// pixeles de la pantalla del mapeo planar. Ver ROADMAP_GUI.md (G7) y GUI_LIBRARY.md §14.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/230_ui_compositor

#include <cstdio>

#include <eng/core/box.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/ui/compositor.hpp>

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
	alignas(2) eng::u8 screen_mem[kPlaneStride * kPlanes] {};
	alignas(2) eng::u8 mem_a[128u * 4u] {};
	alignas(2) eng::u8 mem_b[128u * 4u] {};

	eng::field::ContiguousPlayfield screen_pf {};
	check(screen_pf.bind_raw(screen_mem, sizeof(screen_mem), kSW, kSH, kPlanes),
	      "bind_raw pantalla");
	eng::field::Surface screen {screen_pf, eng::field::SurfaceRect {0, 0, kSW, kSH}};

	eng::ui::Compositor comp;
	comp.set_screen(screen);
	comp.set_desktop(0u);

	eng::ui::CompWindow* a = comp.add();
	check(a != nullptr, "add A");
	check(a->backing.bind(mem_a, sizeof(mem_a), 32u, 32u, kPlanes), "bind A");
	a->frame = eng::ui::Rect {0, 0, 32u, 32u};
	a->backing.surface.fill_rect(0, 0, 32u, 32u, 3u);

	eng::ui::CompWindow* b = comp.add();
	check(b != nullptr, "add B");
	check(b->backing.bind(mem_b, sizeof(mem_b), 32u, 32u, kPlanes), "bind B");
	b->frame = eng::ui::Rect {16, 0, 32u, 32u};
	b->backing.surface.fill_rect(0, 0, 32u, 32u, 5u);

	// --- composicion inicial (fondo -> frente) ---
	comp.damage_screen(eng::ui::Rect {0, 0, kSW, kSH});
	comp.present();
	check(pixel_at(screen_mem, 10, 10) == 3u, "A visible");
	check(pixel_at(screen_mem, 20, 10) == 5u, "B al frente sobre A");
	check(pixel_at(screen_mem, 50, 10) == 0u, "escritorio fuera");

	// --- equivalencia: present(plan) (blit planar) == present() (CPU) ---
	// Las intersecciones de A ({0,0,32,32}) y B ({16,0,32,32}) estan alineadas a palabra,
	// asi que present(plan) usa `Surface::blit` (CopyRect); con rasterizador CPU el resultado
	// debe ser identico al bucle de pixeles.
	{
		eng::u8 snap[kPlaneStride * kPlanes];
		for (eng::u32 i = 0u; i < sizeof(snap); ++i) {
			snap[i] = screen_mem[i];
		}
		for (eng::u32 i = 0u; i < sizeof(screen_mem); ++i) {
			screen_mem[i] = 0u;
		}
		comp.damage_screen(eng::ui::Rect {0, 0, kSW, kSH});
		eng::graphics::FramePlan plan {};
		comp.present(plan);
		bool same = true;
		for (eng::u32 i = 0u; i < sizeof(snap); ++i) {
			if (screen_mem[i] != snap[i]) {
				same = false;
			}
		}
		check(same, "present(plan) equivalente a present() en CPU");
		check(pixel_at(screen_mem, 20, 10) == 5u, "blit planar: B al frente");
	}

	// --- mover B: dana origen y destino, sin repintar vecinas ---
	a->backing.needs_repaint = false;
	b->backing.needs_repaint = false;
	comp.move_window(*b, 8, 0);
	check(comp.damage_count() == 1u, "move: una region danada (union)");
	check(!a->backing.needs_repaint && !b->backing.needs_repaint,
	      "move no marca content_dirty de nadie");
	comp.present();
	check(pixel_at(screen_mem, 5, 10) == 3u, "mover B deja ver A a la izquierda");
	check(pixel_at(screen_mem, 10, 10) == 5u, "B sigue al frente");
	check(pixel_at(screen_mem, 40, 10) == 0u, "B se movio (ya no cubre 40)");

	// --- resize: solo marca su ventana; falla si no cabe ---
	check(!comp.resize_window(*a, 40u, 40u), "resize mayor que el backing -> false");
	check(comp.resize_window(*a, 16u, 16u), "resize valido -> true");
	check(a->backing.needs_repaint && !b->backing.needs_repaint,
	      "resize marca solo su ventana");

	// --- pool lleno ---
	while (comp.add() != nullptr) {
	}
	check(comp.window_count() == eng::ui::Compositor::kMaxWindows, "pool lleno");
	check(comp.add() == nullptr, "add con pool lleno -> nullptr");

	if (failures == 0) {
		std::printf("OK: GUI G7 (compositor + backing) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
