// ============================================================================
// Test HOST-340: pool de bloques con free (eng::BlockPool) - generico por medio.
// ============================================================================
//
// Respalda `eng/memory/block_pool.hpp`: asignador *first-fit* con fusion de huecos sobre un buffer
// (sin heap). Generico: opera sobre cualquier medio y lleva su `MemoryKind` en cada reserva.
// Comprueba alineacion, free, fusion, el medio en `MemoryBlock`/`Block<Tag>` y rechazo si no cabe.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/res/340_block_pool

#include <cstdio>

#include <eng/core/types/domains.hpp>
#include <eng/memory/block_pool.hpp>
#include <eng/memory/mem_bank.hpp>

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

} // namespace

int main() {
	std::printf("== HOST-340 block_pool ==\n");

	eng::u8 buf[1024] {};
	eng::BlockPool pool {buf, sizeof(buf), eng::MemoryKind::Chip, 16u};

	check(pool.capacity() == 1024u && pool.free_bytes() == 1024u, "pool vacio");
	check(pool.kind() == eng::MemoryKind::Chip, "medio Chip");

	const eng::MemoryBlock a = pool.allocate(100u); // 100 -> 112 alineado
	check(a.valid(), "alloc a");
	check(a.kind == eng::MemoryKind::Chip, "bloque con el medio del pool");
	check(reinterpret_cast<eng::uintptr>(a.data) % 16u == 0u, "a alineado a 16");
	check(pool.free_bytes() == 1024u - 112u, "libre tras a");

	// Reserva tipada (mismo API que LinearArena).
	const auto b = pool.allocate_block<eng::PlaneTag>(256u);
	check(b.valid() && b.kind == eng::MemoryKind::Chip, "allocate_block<PlaneTag> con medio");
	check(pool.free_bytes() == 1024u - 368u, "libre tras a+b");

	pool.free(a.data);
	check(pool.free_bytes() == 1024u - 256u, "libre tras free(a)");

	pool.free(b.view.data());
	check(pool.free_bytes() == 1024u, "fusion: todo libre tras free(b)");
	check(pool.block_count() == 1u, "fusion de huecos contiguos");

	check(!pool.allocate(2000u).valid(), "no cabe -> invalido");

	// El mismo pool sirve para cualquier medio (Fast): generico, no una clase por banco.
	eng::BlockPool fast {buf, sizeof(buf), eng::MemoryKind::Fast};
	check(fast.allocate(16u).kind == eng::MemoryKind::Fast, "mismo tipo, medio Fast");

	eng::BlockPool empty {};
	check(!empty.allocate(16u).valid(), "pool por defecto -> invalido");

	// --- Ciclo reserve/release ORDENADO: idempotencia y no corromper el pool ---
	eng::BlockPool p2 {buf, sizeof(buf), eng::MemoryKind::Chip, 16u};
	const eng::MemoryBlock x = p2.allocate(128u);
	check(x.valid(), "alloc x");
	const eng::u32 after_x = p2.free_bytes();
	p2.free(x.data);
	check(p2.free_bytes() > after_x, "release devuelve al pool");
	p2.free(x.data); // doble free: no-op (idempotente)
	check(p2.free_bytes() == p2.capacity(), "doble free es no-op (no corrompe)");
	eng::u8 ajeno[16] {};
	p2.free(ajeno); // puntero ajeno: no-op
	check(p2.free_bytes() == p2.capacity(), "free de puntero ajeno es no-op");
	const eng::MemoryBlock y = p2.allocate(64u);
	check(y.valid(), "el pool sigue usable tras no-ops");
	p2.free(y.data);

	// --- MemBank: par reserve/release tipado por banco (unica puerta) ---
	eng::MemBank<eng::MemoryKind::Chip> bank {};
	bank.configure(buf, sizeof(buf), 16u);
	const auto planes = bank.reserve<eng::PlaneTag>(256u);
	check(planes.valid() && planes.kind == eng::MemoryKind::Chip, "MemBank reserve<PlaneTag>");
	check(planes.address().valid(), "el bloque da una Address<Chip> (DMA)");
	const eng::u32 used = bank.free_bytes();
	bank.release(planes);
	check(bank.free_bytes() > used, "MemBank release devuelve al banco");
	check(bank.block_count() >= 1u, "block_count informa de los slots");

	// --- FREE REAL (no LIFO): reservar graficos, sonido; liberar graficos; re-reservar ---
	// El corazon del pool: a diferencia de la arena *bump*, se libera en cualquier orden.
	{
		eng::u8 g[4096] {};
		eng::BlockPool gp {g, sizeof(g), eng::MemoryKind::Chip, 16u};
		const eng::MemoryBlock gfx = gp.allocate(1024u, 16u);
		const eng::MemoryBlock snd = gp.allocate(1024u, 16u);
		check(gfx.valid() && snd.valid(), "gfx + snd reservados");
		check(snd.data != gfx.data, "bloques distintos");
		const eng::u32 free_before = gp.free_bytes();
		gp.free(gfx.data); // libera el PRIMERO, con 'snd' vivo: imposible en bump
		check(gp.free_bytes() > free_before, "liberar gfx con snd vivo (no LIFO)");
		const eng::MemoryBlock gfx2 = gp.allocate(1024u, 16u); // reutiliza el hueco de gfx
		check(gfx2.valid() && gfx2.data == gfx.data, "re-reserva reutiliza el hueco liberado");
		check(snd.data != gfx2.data, "snd no se pisó");
	}

	// --- Base DESALINEADA: el pool alinea una vez y no acumula padding (peyote) ---
	{
		alignas(16) eng::u8 raw[2048] {};
		eng::u8* misaligned = raw + 8; // base no alineada a 16 (como AllocMem 1.3)
		eng::BlockPool mp {misaligned, 1024u, eng::MemoryKind::Chip, 16u};
		// Dos reservas grandes: con padding acumulativo la 2ª fallaba (bug demo 201).
		const eng::MemoryBlock r1 = mp.allocate(480u, 16u);
		const eng::MemoryBlock r2 = mp.allocate(480u, 16u);
		check(r1.valid() && r2.valid(), "base desalineada: dos reservas caben (sin peyote)");
		check(reinterpret_cast<eng::uintptr>(r1.data) % 16u == 0u, "r1 alineada a 16");
		check(reinterpret_cast<eng::uintptr>(r2.data) % 16u == 0u, "r2 alineada a 16");
	}

	// --- Alineación de una reserva MAYOR que la del pool ---
	{
		eng::u8 g[1024] {};
		eng::BlockPool ap {g, sizeof(g), eng::MemoryKind::Fast, 16u};
		const eng::MemoryBlock r = ap.allocate(64u, 64u);
		check(r.valid() && reinterpret_cast<eng::uintptr>(r.data) % 64u == 0u, "reserva alineada a 64");
		check(ap.free_bytes() < ap.capacity(), "el padding de 64 no consume todo el pool");
	}

	// --- slots_left() informa de la fragmentación ---
	{
		eng::u8 g[4096] {};
		eng::BlockPool fp {g, sizeof(g), eng::MemoryKind::Chip, 16u};
		const eng::MemoryBlock a = fp.allocate(256u, 16u);
		const eng::MemoryBlock b = fp.allocate(256u, 16u);
		const eng::MemoryBlock c = fp.allocate(256u, 16u);
		fp.free(b.data); // fragmenta: hueco en medio
		check(fp.slots_left() < eng::BlockPool::kMaxBlocks, "la fragmentacion consume slots");
		fp.free(a.data);
		fp.free(c.data);
		check(fp.block_count() == 1u, "coalesce total devuelve 1 hueco");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: BlockPool (first-fit, free, fusion, MemoryKind) validado.\n");
	return 0;
}
