// ============================================================================
// Test HOST-254: cache de assets (eng/res/asset_cache.hpp).
// ============================================================================
//
// Valida el ciclo de carga (Empty -> Loading -> Ready), `get` con placeholder, y la politica de
// desalojo (menor prioridad primero; a igualdad, LRU) con `pin`/`refcount` protegiendo.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/res/254_asset_cache

#include <cstdio>

#include <eng/res/asset_cache.hpp>

using namespace eng;
using namespace eng::res;

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

/// Backend falso: arena por banco (no reutiliza) + carga que "arranca" (la completa el test).
struct FakeBackend {
	u8 chip[1024] {};
	u8 fast[1024] {};
	u8 slow[1024] {};
	u32 chip_at = 0;
	u32 fast_at = 0;
	u32 slow_at = 0;
	u16 loads = 0;
	u16 frees = 0;

	MemoryBlock alloc(u32 bytes, MemoryKind bank) {
		u8* base = bank == MemoryKind::Chip ? chip : (bank == MemoryKind::Slow ? slow : fast);
		u32& at = bank == MemoryKind::Chip ? chip_at : (bank == MemoryKind::Slow ? slow_at : fast_at);
		if (at + bytes > 1024u) {
			return {};
		}
		MemoryBlock s {base + at, bytes, bank};
		at += bytes;
		return s;
	}
	void free(const MemoryBlock& block) {
		(void)block;
		++frees;
	}
	bool load(AssetId id, const char* path, Span<u8> dst) {
		(void)id;
		(void)path;
		(void)dst;
		++loads;
		return true;
	}
};

void test_lifecycle() {
	FakeBackend b;
	AssetCache<FakeBackend, 8> cache;
	CacheConfig cfg {};
	cfg.fast_budget = 300u;
	cfg.chip_budget = 100u;
	cache.init(b, cfg);

	const AssetId a = cache.declare("a", 100u, MemoryRequest::Fast, 128u);
	check(a == 1u, "declare devuelve id 1");
	check(cache.state(a) == AssetState::Empty, "estado inicial Empty");

	Span<u8> d = cache.get(a); // lanza la carga
	check(d.empty(), "get lanza la carga y devuelve vacio (placeholder)");
	check(cache.state(a) == AssetState::Loading, "Loading");
	check(b.loads == 1u, "el backend arranco la lectura");
	check(!cache.shutdown(), "shutdown no libera un buffer con lectura async activa");
	check(cache.used_fast() == 100u, "la carga activa conserva su reserva");

	cache.on_load_done(a, 100);
	check(cache.state(a) == AssetState::Ready, "Ready tras FileDone");
	d = cache.get(a);
	check(!d.empty() && d.size() == 100u, "get devuelve los datos");
	check(cache.used_fast() == 100u, "presupuesto Fast contabilizado");
}

void test_eviction_priority() {
	FakeBackend b;
	AssetCache<FakeBackend, 8> cache;
	CacheConfig cfg {};
	cfg.fast_budget = 250u;
	cache.init(b, cfg);

	const AssetId lo = cache.declare("lo", 100u, MemoryRequest::Fast, 50u);
	const AssetId hi = cache.declare("hi", 100u, MemoryRequest::Fast, 200u);
	cache.set_frame(1u);
	(void)cache.prefetch(lo);
	cache.on_load_done(lo, 100);
	cache.set_frame(2u);
	(void)cache.prefetch(hi);
	cache.on_load_done(hi, 100);
	check(cache.used_fast() == 200u, "dos assets cargados");

	const AssetId c = cache.declare("c", 100u, MemoryRequest::Fast, 128u);
	(void)cache.prefetch(c);
	cache.on_load_done(c, 100);
	check(cache.state(lo) == AssetState::Empty, "se desaloja el de menor prioridad");
	check(cache.state(hi) == AssetState::Ready, "el prioritario se mantiene");
	check(cache.state(c) == AssetState::Ready, "el nuevo entra");
}

void test_lru() {
	FakeBackend b;
	AssetCache<FakeBackend, 8> cache;
	CacheConfig cfg {};
	cfg.fast_budget = 250u;
	cache.init(b, cfg);

	const AssetId a = cache.declare("a", 100u, MemoryRequest::Fast, 128u);
	const AssetId bb = cache.declare("b", 100u, MemoryRequest::Fast, 128u);
	cache.set_frame(10u);
	(void)cache.prefetch(a);
	cache.on_load_done(a, 100);
	cache.set_frame(20u);
	(void)cache.prefetch(bb);
	cache.on_load_done(bb, 100);

	const AssetId c = cache.declare("c", 100u, MemoryRequest::Fast, 128u);
	(void)cache.prefetch(c);
	cache.on_load_done(c, 100);
	check(cache.state(a) == AssetState::Empty, "a igualdad de prioridad se desaloja el mas viejo");
	check(cache.state(bb) == AssetState::Ready, "el mas reciente se mantiene");
}

void test_pin_and_ref() {
	FakeBackend b;
	AssetCache<FakeBackend, 8> cache;
	CacheConfig cfg {};
	cfg.fast_budget = 150u;
	cache.init(b, cfg);

	const AssetId a = cache.declare("a", 100u, MemoryRequest::Fast, 128u);
	(void)cache.prefetch(a);
	cache.on_load_done(a, 100);
	cache.pin(a, true);

	const AssetId c = cache.declare("c", 100u, MemoryRequest::Fast, 128u);
	(void)cache.prefetch(c);
	check(cache.state(c) == AssetState::Error, "sin victima (a fijado) la carga falla");
	check(cache.state(a) == AssetState::Ready, "el fijado no se toca");

	cache.pin(a, false);
	cache.add_ref(a); // refcount protege igual
	(void)cache.prefetch(c);
	check(cache.state(c) == AssetState::Error, "con refcount tampoco se desaloja");
}

void test_generation_and_dma_lease() {
	FakeBackend b;
	AssetCache<FakeBackend, 8> cache;
	CacheConfig cfg {};
	cfg.chip_budget = 100u;
	cfg.max_assets = 8u;
	check(cache.init(b, cfg), "init para generation y DMA");

	const AssetId a = cache.declare("a", 100u, MemoryRequest::Chip, 128u);
	check(cache.prefetch(a), "carga inicial arranca");
	cache.on_load_done(a, 100);
	AssetView old = cache.view(a);
	check(cache.valid(old), "la vista inicial es valida");
	check(old.kind == MemoryKind::Chip, "la vista expone el banco efectivo como MemoryKind");
	AssetDmaLease dma_lease = cache.lease_dma(old.handle);
	check(dma_lease.valid(), "adquiere lease DMA RAII");

	const AssetId next = cache.declare("next", 100u, MemoryRequest::Chip, 128u);
	check(!cache.prefetch(next), "lease DMA impide evict");
	check(!cache.shutdown(), "shutdown no libera con DMA activo");
	check(cache.state(a) == AssetState::Ready && cache.valid(old), "owner sigue vivo mientras DMA usa la vista");
	dma_lease.reset();
	check(cache.prefetch(next), "tras cerrar DMA se puede desalojar");
	check(!cache.valid(old), "evict invalida la vista antigua");
	cache.on_load_done(next, 100);
	check(cache.prefetch(a), "se puede recargar el mismo slot desalojado");
	cache.on_load_done(a, 100);
	const AssetView fresh = cache.view(a);
	check(cache.valid(fresh) && fresh.handle.generation != old.handle.generation,
	      "la recarga publica nueva generacion");
	check(!old.handle.valid() || !cache.valid(old), "la vista antigua ya no se valida");
	check(cache.shutdown(), "shutdown sin DMA activo");
	check(!cache.valid(fresh), "shutdown invalida todas las vistas");
	check(b.frees >= 2u, "evict y shutdown liberan owners");
}

void test_slow_fallback_accounting() {
	FakeBackend b;
	AssetCache<FakeBackend, 4> cache;
	CacheConfig cfg {};
	cfg.fast_budget = 100u;
	cfg.slow_budget = 100u;
	cfg.max_assets = 4u;
	check(cache.init(b, cfg), "init con Fast y Slow");
	const AssetId fast = cache.declare("fast", 100u, MemoryRequest::Fast, 200u);
	check(cache.prefetch(fast), "ocupa Fast");
	cache.on_load_done(fast, 100);
	cache.pin(fast, true);
	const AssetId any = cache.declare("any", 100u, MemoryRequest::Any, 10u);
	check(cache.prefetch(any), "Any usa Slow cuando Fast esta lleno");
	cache.on_load_done(any, 100);
	check(cache.used_fast() == 100u && cache.used_slow() == 100u,
	      "contabilidad separada segun MemoryKind efectivo");
	const AssetView slow = cache.view(any);
	check(slow.kind == MemoryKind::Slow && cache.valid(slow), "la vista indica banco Slow efectivo");
	check(cache.shutdown(), "shutdown libera Fast y Slow");
	check(cache.used_fast() == 0u && cache.used_slow() == 0u, "contabilidad vacia al cerrar");
}

void test_no_chip_policy_and_cpu_lease() {
	FakeBackend b;
	AssetCache<FakeBackend, 4> cache;
	CacheConfig cfg {};
	cfg.chip_budget = 100u;
	cfg.fast_budget = 0u;
	cfg.slow_budget = 100u;
	cfg.max_assets = 4u;
	check(cache.init(b, cfg), "init NoChip");
	const AssetId id = cache.declare("cpu", 64u, MemoryRequest::NoChip);
	check(cache.prefetch(id), "NoChip reserva banco CPU");
	cache.on_load_done(id, 64);
	AssetLease lease = cache.lease(id);
	check(lease.valid(), "lease CPU valida");
	const AssetView before = lease.view();
	check(before.kind != MemoryKind::Chip, "NoChip nunca cae a Chip");
	const AssetId next = cache.declare("chip", 100u, MemoryRequest::Chip, 0u);
	check(cache.prefetch(next), "reserva Chip independiente");
	cache.on_load_done(next, 100);
	check(cache.used_slow() == 64u && cache.used_chip() == 100u,
	      "banco efectivo y budget corresponden con la politica");
	check(!cache.shutdown(), "shutdown rechaza leases CPU activas");
	lease.reset();
	const AssetId replacement = cache.declare("cpu-next", 64u, MemoryRequest::NoChip);
	check(cache.prefetch(replacement), "al cerrar la lease se puede desalojar");
	check(!cache.valid(before), "evict invalida la vista liberada");
	cache.on_load_done(replacement, 64);
	check(cache.shutdown(), "shutdown tras liberar leases");
}

void test_chip_dma_lease_is_distinct() {
	FakeBackend b;
	AssetCache<FakeBackend, 2> cache;
	CacheConfig cfg {};
	cfg.chip_budget = 128u;
	cfg.max_assets = 2u;
	check(cache.init(b, cfg), "init DMA lease");
	const AssetId id = cache.declare("dma", 64u, MemoryRequest::Chip);
	check(cache.prefetch(id), "asset Chip carga");
	cache.on_load_done(id, 64);
	AssetDmaLease dma = cache.lease_dma(cache.view(id).handle);
	check(dma.valid() && dma.view().kind == MemoryKind::Chip, "lease DMA valida solo Chip");
	check(!cache.shutdown(), "DMA lease mantiene owner durante teardown");
	dma.reset();
	check(cache.shutdown(), "owner liberado tras terminar DMA");
}

} // namespace

int main() {
	test_lifecycle();
	test_eviction_priority();
	test_lru();
	test_pin_and_ref();
	test_generation_and_dma_lease();
	test_slow_fallback_accounting();
	test_no_chip_policy_and_cpu_lease();
	test_chip_dma_lease_is_distinct();

	if (failures == 0) {
		std::printf("OK: cache de assets (owner, generation, DMA, ciclo, prioridad y LRU) validada.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
