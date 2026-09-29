// ============================================================================
// Test HOST-268: compositor por `Surface::blit` (ruta del Rasterizer).
// ============================================================================
//
// Valida `Compositor::present_blit`: copia los backings con `Surface::blit` (que en host usa el
// rasterizador CPU, `copy_rect_cpu`) y cae al copiado por pixel cuando el rect no es copiable
// (destino no alineado a palabra). La EQUIVALENCIA se comprueba contra `present()` (por pixel):
// mismo resultado pixel a pixel. En hardware, el mismo seam encola `CopyRect` en el FramePlan
// (Blitter). Ver ROADMAP_GUI.md (G8) y GUI_LIBRARY.md §14.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/ui/268_ui_compositor_blit

#include <cstdio>

#include <eng/core/types/box.hpp>
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

/// Copia la pantalla `mem` a `out`.
void snapshot(const eng::u8* mem, eng::u8* out) {
	for (eng::u32 i = 0u; i < kPlaneStride * kPlanes; ++i) {
		out[i] = mem[i];
	}
}

} // namespace

int main() {
	alignas(2) eng::u8 screen_mem[kPlaneStride * kPlanes] {};
	alignas(2) eng::u8 screen_ref[kPlaneStride * kPlanes] {};
	alignas(2) eng::u8 mem_a[256] {};
	alignas(2) eng::u8 mem_b[256] {};
	alignas(2) eng::u8 mem_c[256] {}; // ventana no alineada (16x16; row_bytes redondeado a 4)

	eng::field::ContiguousPlayfield screen_pf {};
	check(screen_pf.bind_raw(screen_mem, sizeof(screen_mem), kSW, kSH, kPlanes), "bind_raw pantalla");
	eng::field::Surface screen {screen_pf, eng::field::SurfaceRect {0, 0, kSW, kSH}};

	eng::ui::Compositor comp;
	comp.set_screen(screen);
	comp.set_desktop(0u);

	eng::ui::CompWindow* a = comp.add();
	check(a != nullptr && a->backing.bind(mem_a, sizeof(mem_a), 32u, 16u, kPlanes), "bind A");
	a->frame = eng::ui::Rect {0, 0, 32u, 16u};
	a->backing.surface.fill_rect(0, 0, 32u, 16u, 3u);

	eng::ui::CompWindow* b = comp.add();
	check(b != nullptr && b->backing.bind(mem_b, sizeof(mem_b), 32u, 16u, kPlanes), "bind B");
	b->frame = eng::ui::Rect {32, 0, 32u, 16u};
	b->backing.surface.fill_rect(0, 0, 32u, 16u, 5u);

	// --- referencia: present() por pixel ---
	comp.damage_screen(eng::ui::Rect {0, 0, kSW, kSH});
	comp.present();
	check(pixel_at(screen_mem, 10, 8) == 3u, "present: A visible");
	check(pixel_at(screen_mem, 40, 8) == 5u, "present: B visible");
	check(pixel_at(screen_mem, 10, 20) == 0u, "present: escritorio abajo");
	snapshot(screen_mem, screen_ref);

	// --- present_blit: debe dar EXACTAMENTE lo mismo (ventanas alineadas a palabra) ---
	for (eng::u32 i = 0u; i < kPlaneStride * kPlanes; ++i) {
		screen_mem[i] = 0xabu; // envenena para detectar lo no copiado
	}
	eng::graphics::FramePlan plan {};
	comp.damage_screen(eng::ui::Rect {0, 0, kSW, kSH});
	comp.present_blit(plan);
	bool same = true;
	for (eng::u32 i = 0u; i < kPlaneStride * kPlanes; ++i) {
		if (screen_mem[i] != screen_ref[i]) {
			same = false;
		}
	}
	check(same, "present_blit == present (pixel a pixel, ventanas alineadas)");

	// --- ventana NO alineada: cae al copiado por pixel y sigue siendo correcta ---
	{
		eng::ui::CompWindow* c = comp.add();
		check(c != nullptr && c->backing.bind(mem_c, sizeof(mem_c), 16u, 16u, kPlanes), "bind C");
		c->frame = eng::ui::Rect {8, 8, 16u, 16u};
		c->backing.surface.fill_rect(0, 0, 16u, 16u, 7u);

		comp.damage_screen(eng::ui::Rect {0, 0, kSW, kSH});
		comp.present(); // referencia
		// (12,12) cae en C (7); (4,12) cae en A por debajo (3); (4,20) es escritorio (0).
		check(pixel_at(screen_mem, 12, 12) == 7u && pixel_at(screen_mem, 4, 12) == 3u &&
			      pixel_at(screen_mem, 4, 20) == 0u,
		      "present: C (no alineada) sobre A, escritorio abajo");

		for (eng::u32 i = 0u; i < kPlaneStride * kPlanes; ++i) {
			screen_mem[i] = 0u;
		}
		eng::graphics::FramePlan plan2 {};
		comp.damage_screen(eng::ui::Rect {0, 0, kSW, kSH});
		comp.present_blit(plan2);
		check(pixel_at(screen_mem, 12, 12) == 7u, "present_blit: C (no alineada) via CPU");
		check(pixel_at(screen_mem, 4, 12) == 3u, "present_blit: A por debajo de C");
		check(pixel_at(screen_mem, 4, 20) == 0u, "present_blit: escritorio intacto");
	}

	// --- origen con shift, destino alineado: blit con source_shift ---
	// Backing de 32 px (row_bytes = 4) con dos mitades de palabra de color distinto
	// (word 0 -> color 1, word 1 -> color 2). Al dañar una franja dentro de la ventana,
	// el origen puede caer desplazado (`source_shift`); se compara con present() (CPU).
	{
		constexpr eng::u16 dRow = 4u;                 // bytes/fila de un plano (32 px)
		constexpr eng::u32 dPlane = dRow * 16u;       // 64 bytes por plano
		alignas(2) eng::u8 mem_d[dPlane * kPlanes] {};
		eng::ui::CompWindow* d = comp.add();
		check(d != nullptr && d->backing.bind(mem_d, sizeof(mem_d), 32u, 16u, kPlanes), "bind D");
		d->frame = eng::ui::Rect {8, 16, 32u, 16u};
		d->backing.surface.fill_rect(0, 0, 16u, 16u, 1u);
		d->backing.surface.fill_rect(16, 0, 16u, 16u, 2u);

		// Misma funcion de lectura que usa el compositor, con el row del backing.
		auto dpx = [&](eng::s16 x, eng::s16 y) {
			eng::u8 c = 0u;
			for (eng::u8 p = 0u; p < kPlanes; ++p) {
				const eng::u16* w = reinterpret_cast<const eng::u16*>(
					mem_d + p * dPlane + static_cast<eng::u32>(y) * dRow +
					static_cast<eng::u32>(x / 16) * 2u);
				c = static_cast<eng::u8>(c |
					(static_cast<eng::u8>((*w >> (15u - (x & 15))) & 1u) << p));
			}
			return c;
		};
		check(dpx(4, 5) == 1u && dpx(20, 5) == 2u, "D: patron por mitades de palabra");

		// Franja I.x=8,w=16 (destino alineado, origen sx=0): ruta blit. La ventana D
		// empieza en x=8, asi que I.x=8 -> sx=0; I.x=24 -> sx=16 (alineado); I.x=16 -> sx=8
		// (shift). Se daña toda la fila de D donde toca.
		for (eng::u32 i = 0u; i < kPlaneStride * kPlanes; ++i) {
			screen_mem[i] = 0xabu;
		}
		eng::graphics::FramePlan plan3 {};
		comp.damage_screen(eng::ui::Rect {8, 16, 16u, 16u});
		comp.present_blit(plan3);
		check(pixel_at(screen_mem, 12, 21) == 1u,
		      "present_blit: franja x=8 (sx=0, destino alineado)");

		// Franja I.x=16,w=16 (destino alineado) con origen sx=8: el blit debe desplazar
		// el origen (sx=8 -> mitad de la palabra 0 del backing, color 1). x=24 cae en la
		// palabra 1 del backing (color 2).
		for (eng::u32 i = 0u; i < kPlaneStride * kPlanes; ++i) {
			screen_mem[i] = 0xabu;
		}
		eng::graphics::FramePlan plan4 {};
		comp.damage_screen(eng::ui::Rect {16, 16, 16u, 16u});
		comp.present_blit(plan4);
		check(pixel_at(screen_mem, 16, 21) == 1u && pixel_at(screen_mem, 24, 21) == 2u,
		      "present_blit: franja x=16 con shift (sx=8)");

		// Franja I.x=24 (no alineada) -> bucle de pixeles; sigue correcta (color 2).
		for (eng::u32 i = 0u; i < kPlaneStride * kPlanes; ++i) {
			screen_mem[i] = 0xabu;
		}
		eng::graphics::FramePlan plan5 {};
		comp.damage_screen(eng::ui::Rect {24, 16, 16u, 16u});
		comp.present_blit(plan5);
		check(pixel_at(screen_mem, 24, 21) == 2u, "present_blit: franja x=24 via CPU");
	}

	if (failures == 0) {
		std::printf("OK: compositor por blit (equivalencia con el copiado por pixel) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
