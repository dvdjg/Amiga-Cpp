// ============================================================================
// Test HOST-068: `DoubleBufferScrollPlayfield` (2 bitmaps + flip + hardware_view)
// ============================================================================
//
// Valida en host (sin Amiga) que la superficie de scroll con doble buffer de bitmap:
//
//   1) Reserva DOS bitmaps distintos y expone su geometria (row_bytes/planes).
//   2) `front_index()` arranca en 0 y `flip()` alterna 0<->1.
//   3) `hardware_view()` describe SIEMPRE el buffer delantero (base de bitplanes,
//      `plane_bytes`, `display_height`).
//   4) La camara (`cam_x`/`cam_y`, `BigBufferScroll`) mueve el mapping y respeta el
//      clamp del rango (min/max fijados por `begin`).
//   5) Rechaza un mundo mas pequeno que el viewport.
//
// Es el respaldo determinista que le faltaba a la demo 122 (ver F0.4 de
// `docs/guides/roadmap/NORMALIZACION_REPO.md`).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/field/068_double_buffer_scroll

#include <cstdio>

#include <eng/core/types/types.hpp>
#include <eng/field/double_buffer_playfield.hpp>
#include <eng/memory/arena.hpp>

namespace {

using eng::MemoryKind;
using eng::MemorySystem;
using eng::LinearArena;
using eng::field::DoubleBufferScrollConfig;
using eng::field::DoubleBufferScrollPlayfield;

alignas(16) eng::u8 g_chip[512 * 1024];

MemorySystem make_memory() {
	MemorySystem mem;
	mem.chip = LinearArena {g_chip, sizeof(g_chip), MemoryKind::Chip};
	return mem;
}

} // namespace

int main() {
	MemorySystem mem = make_memory();

	constexpr eng::u16 kWorldW = 512;
	constexpr eng::u16 kWorldH = 512;
	constexpr eng::u16 kViewW = 320;
	constexpr eng::u16 kViewH = 256;
	constexpr eng::u8 kPlanes = 4;

	DoubleBufferScrollConfig cfg {};
	cfg.world_w = kWorldW;
	cfg.world_h = kWorldH;
	cfg.view_w = kViewW;
	cfg.view_h = kViewH;
	cfg.planes = kPlanes;
	cfg.fetch_bytes = 42;

	// El display posee los DOS bitmaps; la superficie solo los liga.
	eng::gfx::BitmapConfig bc {};
	bc.width = kWorldW;
	bc.height = kWorldH;
	bc.planes = kPlanes;
	bc.layout = eng::gfx::PlaneLayout::Interleaved;
	eng::gfx::Bitmap b0;
	eng::gfx::Bitmap b1;
	if (!b0.init(mem, bc) || !b1.init(mem, bc)) {
		std::printf("[FAIL] init de los bitmaps\n");
		return 1;
	}

	DoubleBufferScrollPlayfield pf;
	if (!pf.bind(cfg, b0, b1)) {
		std::printf("[FAIL] bind() fallo con un mundo valido\n");
		return 1;
	}

	// --- 1) Geometria y DOS buffers distintos -----------------------------------
	const eng::u16 row_bytes = static_cast<eng::u16>(kWorldW / 8u);
	if (pf.row_bytes() != row_bytes) {
		std::printf("[FAIL] row_bytes=%u (esperado %u)\n", (unsigned)pf.row_bytes(), (unsigned)row_bytes);
		return 1;
	}
	if (pf.buffer_bytes(0) == pf.buffer_bytes(1) || pf.buffer_bytes(0) == nullptr) {
		std::printf("[FAIL] los dos buffers deben existir y ser distintos\n");
		return 1;
	}

	// --- 2) front inicial 0 y flip alterna --------------------------------------
	if (pf.front_index() != 0u) {
		std::printf("[FAIL] front inicial=%u (esperado 0)\n", (unsigned)pf.front_index());
		return 1;
	}
	pf.flip();
	if (pf.front_index() != 1u) {
		std::printf("[FAIL] tras un flip front=%u (esperado 1)\n", (unsigned)pf.front_index());
		return 1;
	}
	pf.flip();
	if (pf.front_index() != 0u) {
		std::printf("[FAIL] tras dos flips front=%u (esperado 0)\n", (unsigned)pf.front_index());
		return 1;
	}

	// --- 3) hardware_view describe el buffer DELANTERO --------------------------
	// En layout interleaved el display usa `total_bytes` como paso (no el tamaño de
	// un plano suelto): total = row_bytes * height * planes.
	const eng::u32 bitmap_bytes = static_cast<eng::u32>(row_bytes) * kWorldH * kPlanes;
	{
		const eng::field::PlayfieldHardwareView v = pf.hardware_view();
		if (v.bitplanes != pf.buffer_bytes(pf.front_index())) {
			std::printf("[FAIL] hardware_view no apunta al buffer delantero\n");
			return 1;
		}
		if (v.plane_bytes != bitmap_bytes || v.planes != kPlanes) {
			std::printf("[FAIL] view.plane_bytes=%u planes=%u\n", (unsigned)v.plane_bytes,
				    (unsigned)v.planes);
			return 1;
		}
		if (v.bitmap_bytes_per_row != row_bytes || v.display_height != kWorldH) {
			std::printf("[FAIL] view: row=%u height=%u\n", (unsigned)v.bitmap_bytes_per_row,
				    (unsigned)v.display_height);
			return 1;
		}
		if (v.viewport_w != kViewW || v.viewport_h != kViewH) {
			std::printf("[FAIL] view: viewport %ux%u\n", (unsigned)v.viewport_w,
				    (unsigned)v.viewport_h);
			return 1;
		}
	}
	// El flip cambia la base que ve el display.
	pf.flip();
	{
		const eng::field::PlayfieldHardwareView v = pf.hardware_view();
		if (v.bitplanes != pf.buffer_bytes(1)) {
			std::printf("[FAIL] tras flip, hardware_view no sigue al nuevo delantero\n");
			return 1;
		}
	}
	pf.flip();

	// --- 4) Camara: mueve el mapping y respeta el clamp -------------------------
	{
		const eng::field::PlayfieldHardwareView v0 = pf.hardware_view();
		if (v0.videoposx != pf.cam_x().position || v0.mapposx != pf.cam_x().position) {
			std::printf("[FAIL] el mapping no refleja la posicion de camara\n");
			return 1;
		}
		pf.cam_x().step(16);
		const eng::field::PlayfieldHardwareView v1 = pf.hardware_view();
		if (v1.videoposx != pf.cam_x().position || v1.videoposx == v0.videoposx) {
			std::printf("[FAIL] step(16) no movio el mapping\n");
			return 1;
		}
		// Forzar el limite superior: debe quedar clampado a world-view.
		pf.cam_x().step(static_cast<eng::s32>(kWorldW) * 4);
		if (pf.cam_x().position != static_cast<eng::s32>(kWorldW - kViewW)) {
			std::printf("[FAIL] cam_x no clampa al maximo (pos=%d, esperado %d)\n",
				    (int)pf.cam_x().position, (int)(kWorldW - kViewW));
			return 1;
		}
	}

	// --- 5) Rechaza mundo mas pequeno que el viewport ---------------------------
	{
		DoubleBufferScrollPlayfield bad;
		DoubleBufferScrollConfig bad_cfg {};
		bad_cfg.world_w = 100;
		bad_cfg.world_h = 100;
		bad_cfg.view_w = kViewW;
		bad_cfg.view_h = kViewH;
		bad_cfg.planes = kPlanes;
		if (bad.bind(bad_cfg, b0, b1)) {
			std::printf("[FAIL] bind() acepto un mundo menor que el viewport\n");
			return 1;
		}
	}

	std::printf("OK: DoubleBufferScrollPlayfield (2 buffers, flip y hardware_view del delantero).\n");
	return 0;
}
