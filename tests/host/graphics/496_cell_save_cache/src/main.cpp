// ============================================================================
// Test HOST-496: MapCellSaveCache (save-under por celda viva del surface).
// ============================================================================
//
// Valida sin emulador:
//
//   1) `configure` reserva UN bloque Chip del tamaño `Capacity × cell_bytes`
//      (no una reserva por celda, que fragmenta el BlockPool).
//   2) `capture` emite UN `CopyRect` para todos los planos, con el
//      direccionamiento del anillo (plano 0, paso entre planos = row_bytes).
//   3) La captura se conserva: `capture` repetido no emite otro job.
//   4) `restore` emite UN `RestoreRect` inverso y deja `painted=false` pero
//      conserva la captura.
//   5) `release` libera la entrada sin restaurar.
//   6) Una celda mayor que el stride configurado se rechaza sin tocar el plan.
//   7) Sin slot libre (todas las entradas pintadas) la captura falla.
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/graphics/496_cell_save_cache

#include <cstdio>
#include <cstring>

#include <eng/core/types/types.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/map_sprite.hpp>
#include <eng/memory/mem_bank.hpp>

namespace {

using eng::u8;
using eng::u16;
using eng::u32;
using eng::graphics::BlitJobKind;
using eng::graphics::BobTarget;
using eng::graphics::FramePlan;
using eng::graphics::MapCellSaveCache;

constexpr u16 kRowBytes = 44u;
constexpr u16 kHeight = 288u;
constexpr u8 kPlanes = 6u;
constexpr u16 kCell = 16u;
constexpr u32 kCellBytes = static_cast<u32>(kCell) * kCell * kPlanes / 8u; // 192
constexpr u16 kCapacity = 4u;

alignas(16) eng::u8 g_target[kRowBytes * kHeight * kPlanes] {};
alignas(16) eng::u8 g_pool[kCapacity * kCellBytes] {};

int g_failures = 0;

void check(bool condition, const char* what) {
	if (!condition) {
		std::printf("FALLO: %s\n", what);
		++g_failures;
	}
}

BobTarget make_target() {
	return eng::graphics::make_bob_target(
		eng::MemView<eng::PlaneTag, eng::MemoryKind::Chip> {
			eng::Address<eng::MemoryKind::Chip>::from_storage(g_target),
			sizeof(g_target)},
		kRowBytes, kHeight, kPlanes, eng::graphics::BobLayout::Interleaved);
}

const eng::u8* target_cell(u16 x, u16 y, u8 plane = 0u) {
	return g_target + (static_cast<u32>(y) * kPlanes + plane) * kRowBytes + (x >> 3u);
}

void test_capture_and_restore() {
	eng::MemBank<eng::MemoryKind::Chip> bank {};
	bank.configure(g_pool, sizeof(g_pool));
	MapCellSaveCache<kCapacity> cache {};
	cache.configure(bank, kCellBytes);
	const BobTarget target = make_target();
	FramePlan plan {};
	plan.set_target_bounds(target.bound());

	check(cache.bytes_held() == static_cast<u32>(kCapacity) * kCellBytes,
	      "configure: un solo bloque Capacity × cell_bytes");
	check(cache.used() == 0u, "configure: sin entradas");

	// Captura de la celda (16,16).
	check(cache.capture(plan, target, 0u, 16u, 16u, kCell, kCell),
	      "capture: true");
	check(cache.saved(0u) && cache.painted(0u), "capture: capturada y pintada");
	check(cache.used() == 1u, "capture: una entrada");
	check(plan.blit_job_count() == 1u, "capture: UN job para los 6 planos");
	const eng::graphics::BlitJob& cap = plan.blit_job(0u);
	check(cap.kind == BlitJobKind::CopyRect, "capture: CopyRect");
	check(cap.bitplane_count == kPlanes, "capture: bitplane_count = planos");
	check(cap.words_per_row == 1u && cap.height == kCell,
	      "capture: 1 palabra × 16 filas");
	check(reinterpret_cast<const eng::u8*>(cap.destination.cwords()) == g_pool,
	      "capture: destino = arena");
	check(reinterpret_cast<const eng::u8*>(cap.source.cwords()) == target_cell(16u, 16u),
	      "capture: fuente = celda del anillo (plano 0)");
	check(cap.source_plane_stride_bytes == kRowBytes,
	      "capture: paso entre planos del anillo");
	check(cap.destination_plane_stride_bytes == kCell * 2u,
	      "capture: paso entre planos del buffer");
	check(cap.source_modulo_bytes ==
		      eng::graphics::mod16(static_cast<eng::s32>(kRowBytes) * kPlanes - 2),
	      "capture: módulo de fila del anillo");
	check(cap.destination_modulo_bytes == 0,
	      "capture: buffer compacto");

	// Repetir la captura no re-emite: el fondo ya está guardado.
	plan.clear();
	check(cache.capture(plan, target, 0u, 16u, 16u, kCell, kCell),
	      "capture repetido: true sin trabajo");
	check(plan.blit_job_count() == 0u, "capture repetido: 0 jobs");

	// Restore: inverso y conserva la captura.
	check(cache.restore(plan, target, 0u), "restore: true");
	check(cache.saved(0u) && !cache.painted(0u), "restore: conserva captura");
	check(plan.blit_job_count() == 1u, "restore: UN job");
	const eng::graphics::BlitJob& res = plan.blit_job(0u);
	check(res.kind == BlitJobKind::RestoreRect, "restore: RestoreRect");
	check(reinterpret_cast<const eng::u8*>(res.source.cwords()) == g_pool,
	      "restore: fuente = arena");
	check(reinterpret_cast<const eng::u8*>(res.destination.cwords()) ==
		      target_cell(16u, 16u),
	      "restore: destino = celda del anillo");
	check(res.source_plane_stride_bytes == kCell * 2u,
	      "restore: paso entre planos del buffer");
	check(res.destination_plane_stride_bytes == kRowBytes,
	      "restore: paso entre planos del anillo");

	// Volver a pintar tras el restore: marca painted sin recapturar.
	plan.clear();
	check(cache.capture(plan, target, 0u, 16u, 16u, kCell, kCell),
	      "capture tras restore: true");
	check(cache.painted(0u), "capture tras restore: painted");
	check(plan.blit_job_count() == 0u, "capture tras restore: 0 jobs");

	// Release sin restaurar (celda fuera de ventana).
	cache.release(0u);
	check(cache.used() == 0u && !cache.saved(0u), "release: entrada libre");

	// Celda mayor que el stride configurado: rechazo sin tocar el plan.
	plan.clear();
	check(!cache.capture(plan, target, 1u, 32u, 32u, 32u, 32u),
	      "celda 32 en arena de 16: rechazada");
	check(plan.blit_job_count() == 0u, "celda 32: no emite jobs");
	check(cache.failures() != 0u, "celda 32: cuenta el fallo");

	// Capacidad: 4 entradas pintadas; la quinta no cabe.
	for (u16 k = 0u; k < kCapacity; ++k) {
		check(cache.capture(plan, target, static_cast<u16>(10u + k), 16u,
				    static_cast<u16>(16u + k), kCell, kCell),
		      "capacidad: captura dentro");
	}
	plan.clear();
	check(!cache.capture(plan, target, 9u, 16u, 32u, kCell, kCell),
	      "capacidad: quinta captura rechazada");
	check(plan.blit_job_count() == 0u, "capacidad: no emite jobs");
}

} // namespace

int main() {
	std::memset(g_target, 0x5a, sizeof(g_target));
	std::memset(g_pool, 0x00, sizeof(g_pool));
	test_capture_and_restore();
	if (g_failures != 0) {
		std::printf("HOST-496: %d fallos\n", g_failures);
		return 1;
	}
	std::printf("HOST-496: save-under de celdas OK\n");
	return 0;
}
