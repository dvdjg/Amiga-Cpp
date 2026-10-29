// ============================================================================
// Test HOST-502: MapCellSaveCache (save-under por celda viva del surface).
// ============================================================================
//
// Valida sin emulador:
//
//   1) `configure` reserva UN bloque Chip del tamaño `Capacity × cell_bytes`
//      (no una reserva por celda, que fragmenta el BlockPool).
//   2) `capture` emite UN `CopyRect`. Con destino interleaved el buffer se
//      guarda en el mismo orden `[fila][plano][palabra]` y el blit recorre
//      `h*planos` filas con `bitplane_count=1` (un solo BLTSIZE); con destino
//      planar se conserva el camino multi-plano con strides por plano.
//   3) La captura se conserva: `capture` repetido no emite otro job.
//   4) `restore` emite UN `RestoreRect` inverso y deja `painted=false` pero
//      conserva la captura.
//   5) `release` libera la entrada sin restaurar.
//   6) Una celda mayor que el stride configurado se rechaza sin tocar el plan.
//   7) Sin slot libre (todas las entradas pintadas) la captura falla.
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/graphics/502_cell_save_cache

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
using eng::graphics::MapCellSaveCacheSet;

constexpr u16 kRowBytes = 44u;
constexpr u16 kHeight = 288u;
constexpr u8 kPlanes = 6u;
constexpr u16 kCell = 16u;
constexpr u32 kCellBytes = static_cast<u32>(kCell) * kCell * kPlanes / 8u; // 192
constexpr u16 kCapacity = 4u;
constexpr u16 kLargeCapacity = 2u;
constexpr u32 kLargeCellBytes = static_cast<u32>(32u) * 32u * kPlanes / 8u; // 768

alignas(16) eng::u8 g_target[kRowBytes * kHeight * kPlanes] {};
alignas(16) eng::u8 g_pool[kCapacity * kCellBytes] {};
alignas(16) eng::u8 g_set_pool[kCapacity * kCellBytes + kLargeCapacity * kLargeCellBytes] {};

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
	check(cap.interleaved, "capture: blit interleaved de una pasada");
	check(cap.bitplane_count == 1u && cap.words_per_row == 1u &&
		      cap.height == kCell * kPlanes,
	      "capture: 1 palabra × (16 filas × 6 planos) en un BLTSIZE");
	check(reinterpret_cast<const eng::u8*>(cap.destination.cwords()) == g_pool,
	      "capture: destino = arena");
	check(reinterpret_cast<const eng::u8*>(cap.source.cwords()) == target_cell(16u, 16u),
	      "capture: fuente = celda del anillo (plano 0)");
	check(cap.source_plane_stride_bytes == 0u &&
		      cap.destination_plane_stride_bytes == 0u,
	      "capture: sin strides por plano (una pasada)");
	check(cap.source_modulo_bytes ==
		      eng::graphics::mod16(static_cast<eng::s32>(kRowBytes) - 2),
	      "capture: módulo de fila-plano del anillo");
	check(cap.destination_modulo_bytes == 0,
	      "capture: buffer interleaved compacto");

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
	check(res.interleaved, "restore: blit interleaved de una pasada");
	check(res.bitplane_count == 1u && res.height == kCell * kPlanes,
	      "restore: un BLTSIZE para las 96 filas-plano");
	check(reinterpret_cast<const eng::u8*>(res.source.cwords()) == g_pool,
	      "restore: fuente = arena");
	check(reinterpret_cast<const eng::u8*>(res.destination.cwords()) ==
		      target_cell(16u, 16u),
	      "restore: destino = celda del anillo");
	check(res.source_plane_stride_bytes == 0u &&
		      res.destination_plane_stride_bytes == 0u,
	      "restore: sin strides por plano (una pasada)");
	check(res.source_modulo_bytes == 0 &&
		      res.destination_modulo_bytes ==
			      eng::graphics::mod16(static_cast<eng::s32>(kRowBytes) - 2),
	      "restore: módulos de buffer y anillo");

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

void test_size_classes() {
	eng::MemBank<eng::MemoryKind::Chip> bank {};
	bank.configure(g_set_pool, sizeof(g_set_pool));
	MapCellSaveCacheSet<kCapacity, kLargeCapacity> cache {};
	cache.configure(bank, kCellBytes, kLargeCellBytes);
	const BobTarget target = make_target();
	FramePlan plan {};
	plan.set_target_bounds(target.bound());
	check(cache.bytes_held() == static_cast<u32>(kCapacity) * kCellBytes +
					      static_cast<u32>(kLargeCapacity) * kLargeCellBytes,
	      "set: dos arenas reservadas");
	check(cache.capture(plan, target, 0u, 16u, 16u, kCell, kCell),
	      "set: celda pequena");
	check(cache.capture(plan, target, 1u, 32u, 32u, 32u, 32u),
	      "set: sprite 32x32 en arena grande");
	check(cache.used() == 2u, "set: dos entradas");
	check(cache.saved(0u) && cache.saved(1u), "set: ambas capturadas");
	check(plan.blit_job_count() == 2u, "set: un job por captura");
	check(cache.restore(plan, target, 0u), "set: restore pequena");
	check(cache.restore(plan, target, 1u), "set: restore grande");
	check(cache.painted(0u) == false && cache.painted(1u) == false,
	      "set: restore deja painted=false");
	cache.release(0u);
	cache.release(1u);
	check(cache.used() == 0u, "set: release libera ambas clases");
}

/// El camino **planar** conserva el contrato multi-plano con strides por plano;
/// solo el destino interleaved usa el blit único `[fila][plano][palabra]`.
void test_planar_copy_contract() {
	eng::MemBank<eng::MemoryKind::Chip> bank {};
	bank.configure(g_pool, sizeof(g_pool));
	MapCellSaveCache<kCapacity> cache {};
	cache.configure(bank, kCellBytes);
	const BobTarget target = eng::graphics::make_bob_target(
		eng::MemView<eng::PlaneTag, eng::MemoryKind::Chip> {
			eng::Address<eng::MemoryKind::Chip>::from_storage(g_target),
			sizeof(g_target)},
		kRowBytes, kHeight, kPlanes, eng::graphics::BobLayout::Planar);
	eng::graphics::FramePlan plan {};
	check(cache.capture(plan, target, 0u, 16u, 16u, kCell, kCell),
	      "planar capture: true");
	const eng::graphics::BlitJob& cap = plan.blit_job(0u);
	check(cap.bitplane_count == kPlanes, "planar capture: un job por plano");
	check(cap.source_plane_stride_bytes == target.plane_pointer_step(),
	      "planar capture: stride de plano del anillo");
	check(cap.destination_plane_stride_bytes == kCell * 2u,
	      "planar capture: stride de plano del buffer");
	plan.clear();
	check(cache.restore(plan, target, 0u), "planar restore: true");
	const eng::graphics::BlitJob& res = plan.blit_job(0u);
	check(res.bitplane_count == kPlanes, "planar restore: un job por plano");
	check(res.source_plane_stride_bytes == kCell * 2u,
	      "planar restore: stride de plano del buffer");
	check(res.destination_plane_stride_bytes == target.plane_pointer_step(),
	      "planar restore: stride de plano del anillo");
}

} // namespace

int main() {
	std::memset(g_target, 0x5a, sizeof(g_target));
	std::memset(g_pool, 0x00, sizeof(g_pool));
	test_capture_and_restore();
	test_planar_copy_contract();
	test_size_classes();
	if (g_failures != 0) {
		std::printf("HOST-502: %d fallos\n", g_failures);
		return 1;
	}
	std::printf("HOST-502: save-under de celdas OK\n");
	return 0;
}
