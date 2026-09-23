// ============================================================================
// Test HOST-043: XlimitedTileBank (banco propio o aliaseado) + kind del bloque
// ============================================================================
//
// Valida el descriptor `eng::field::XlimitedTileBank` (xlimited_scene.hpp): un
// banco de bloques X-Limited que puede ser **propio** (reservado por los builders
// en Chip RAM) o **aliaseado** a un incbin de solo lectura. Transporta la vista de
// dominio (`TileBankBytes`) y el `MemoryKind`, de modo que la escena no guarda un
// `MemoryBlock` crudo. Tambien comprueba que `Block<Tag>` propaga el `kind` de la
// reserva (`allocate_block`).

#include <cstdio>

#include <eng/field/xlimited_scene.hpp>
#include <eng/memory/arena.hpp>

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) { std::printf("[FAIL] %s\n", what); ++g_fail; }
}

} // namespace

int main() {
	// --- Aliaseado a un array incbin de solo lectura -------------------------
	static const eng::u8 kIncbin[8] {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0};
	const eng::field::XlimitedTileBank aliased {
		eng::TileBankBytes { kIncbin, sizeof(kIncbin) },
		eng::MemoryKind::Chip,
	};
	check(aliased.valid(), "aliased valido");
	check(aliased.kind == eng::MemoryKind::Chip, "aliased kind Chip");
	check(aliased.view.size() == sizeof(kIncbin), "aliased tamano");
	// `words()` es la MISMA memoria reinterpretada a u16 (sin copia, endian-neutral).
	check(aliased.words() == reinterpret_cast<const eng::u16*>(kIncbin), "aliased words == incbin");
	check(reinterpret_cast<const eng::u8*>(aliased.words())[0] == 0x12, "aliased byte 0");

	// --- Propio: reserva tipada en una arena Chip ----------------------------
	eng::u8 raw[64] {};
	eng::LinearArena arena {raw, sizeof(raw), eng::MemoryKind::Chip};
	eng::Block<eng::TileBankTag> block = arena.allocate_block<eng::TileBankTag>(16u, 16u);
	check(block.valid(), "reserva valida");
	check(block.kind == eng::MemoryKind::Chip, "allocate_block propaga kind Chip");

	const eng::field::XlimitedTileBank owned { block.view.as_const(), block.kind };
	check(owned.valid(), "owned valido");
	check(owned.kind == eng::MemoryKind::Chip, "owned kind Chip");
	check(reinterpret_cast<const eng::u8*>(owned.words()) == block.view.data(), "owned words -> reserva");

	// --- Vacio -----------------------------------------------------------------
	eng::field::XlimitedTileBank empty {};
	check(!empty.valid(), "vacio no valido");
	check(empty.words() == nullptr, "vacio words null");

	// En otros medios el `kind` se conserva (Slow/Fast) sin cambiar el dominio.
	eng::u8 slow_raw[32] {};
	eng::LinearArena slow {slow_raw, sizeof(slow_raw), eng::MemoryKind::Slow};
	const eng::field::XlimitedTileBank dark { slow.allocate_block<eng::TileBankTag>(8u, 2u).view.as_const(),
	                                          eng::MemoryKind::Slow };
	check(dark.kind == eng::MemoryKind::Slow, "kind Slow conservado");

	if (g_fail != 0) { std::printf("%d fallo(s)\n", g_fail); return 1; }
	std::printf("OK: XlimitedTileBank (propio/aliaseado) y kind del bloque validados.\n");
	return 0;
}
