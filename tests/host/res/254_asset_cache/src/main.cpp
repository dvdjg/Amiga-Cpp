// ============================================================================
// Test HOST-254: cache de assets (eng/res/asset_cache.hpp).
// ============================================================================
//
// Valida el ciclo de carga (Empty -> Loading -> Ready), `get` con placeholder, y la politica de
// desalojo (menor prioridad primero; a igualdad, LRU) con `pin`/`refcount` protegiendo.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/254_asset_cache

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
	u32 chip_at = 0;
	u32 fast_at = 0;
	u16 loads = 0;

	Span<u8> alloc(u32 bytes, MemBank bank) {
		u8* base = (bank == MemBank::Chip) ? chip : fast;
		u32& at = (bank == MemBank::Chip) ? chip_at : fast_at;
		if (at + bytes > 1024u) {
			return {};
		}
		Span<u8> s {base + at, bytes};
		at += bytes;
		return s;
	}
	void free(Span<u8> block, MemBank bank) {
		(void)block;
		(void)bank;
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

	const AssetId a = cache.declare("a", 100u, MemBank::Fast, 128u);
	check(a == 1u, "declare devuelve id 1");
	check(cache.state(a) == AssetState::Empty, "estado inicial Empty");

	Span<u8> d = cache.get(a); // lanza la carga
	check(d.empty(), "get lanza la carga y devuelve vacio (placeholder)");
	check(cache.state(a) == AssetState::Loading, "Loading");
	check(b.loads == 1u, "el backend arranco la lectura");

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

	const AssetId lo = cache.declare("lo", 100u, MemBank::Fast, 50u);
	const AssetId hi = cache.declare("hi", 100u, MemBank::Fast, 200u);
	cache.set_frame(1u);
	(void)cache.prefetch(lo);
	cache.on_load_done(lo, 100);
	cache.set_frame(2u);
	(void)cache.prefetch(hi);
	cache.on_load_done(hi, 100);
	check(cache.used_fast() == 200u, "dos assets cargados");

	const AssetId c = cache.declare("c", 100u, MemBank::Fast, 128u);
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

	const AssetId a = cache.declare("a", 100u, MemBank::Fast, 128u);
	const AssetId bb = cache.declare("b", 100u, MemBank::Fast, 128u);
	cache.set_frame(10u);
	(void)cache.prefetch(a);
	cache.on_load_done(a, 100);
	cache.set_frame(20u);
	(void)cache.prefetch(bb);
	cache.on_load_done(bb, 100);

	const AssetId c = cache.declare("c", 100u, MemBank::Fast, 128u);
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

	const AssetId a = cache.declare("a", 100u, MemBank::Fast, 128u);
	(void)cache.prefetch(a);
	cache.on_load_done(a, 100);
	cache.pin(a, true);

	const AssetId c = cache.declare("c", 100u, MemBank::Fast, 128u);
	(void)cache.prefetch(c);
	check(cache.state(c) == AssetState::Error, "sin victima (a fijado) la carga falla");
	check(cache.state(a) == AssetState::Ready, "el fijado no se toca");

	cache.pin(a, false);
	cache.add_ref(a); // refcount protege igual
	(void)cache.prefetch(c);
	check(cache.state(c) == AssetState::Error, "con refcount tampoco se desaloja");
}

} // namespace

int main() {
	test_lifecycle();
	test_eviction_priority();
	test_lru();
	test_pin_and_ref();

	if (failures == 0) {
		std::printf("OK: cache de assets (ciclo, prioridad, LRU, pin/refcount) validada.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
