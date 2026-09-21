// ============================================================================
// Test HOST-255: E/S asincrona y enrutado por tag (eng/os/file.hpp + eng/res/resources.hpp).
// ============================================================================
//
// Valida el cookie `IoUser` (encode/decode) y que `route_io` entrega cada `FileDone`/`FileError`
// al subsistema correcto (cache/loader) sin cruzar consumidores, y descarta lo que no es E/S.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/255_io_route

#include <cstdio>

#include <eng/res/resources.hpp>

using namespace eng;
using namespace eng::os;

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

/// Cache falsa: registra la ultima llamada.
struct FakeCache {
	int calls = 0;
	u16 last_id = 0;
	s32 last_result = 0;
	void on_load_done(u16 id, s32 result) {
		++calls;
		last_id = id;
		last_result = result;
	}
};

/// Loader falso.
struct FakeLibs {
	int calls = 0;
	u16 last_id = 0;
	void on_file_done(u16 id, s32 result) {
		(void)result;
		++calls;
		last_id = id;
	}
};

Msg make_file_msg(MsgType type, u32 cookie, s32 result) {
	Msg m {};
	m.type = type;
	m.payload.file = {1u, result, 0u, cookie};
	return m;
}

void test_iouser() {
	const IoUser a {eng::res::kTagAsset, 7u};
	check(a.encode() == ((static_cast<u32>('A') << 16u) | 7u), "IoUser encode");
	const IoUser d = IoUser::decode(a.encode());
	check(d.tag == 'A' && d.id == 7u, "IoUser decode");
	check(IoUser::decode(a.encode()).tag == eng::res::kTagAsset, "tag asset");
}

void test_route() {
	FakeCache cache;
	FakeLibs libs;

	// FileDone de un asset -> cache.
	Msg m = make_file_msg(MsgType::FileDone, IoUser {eng::res::kTagAsset, 3u}.encode(), 100);
	check(eng::res::route_io(m, cache, libs), "route asset");
	check(cache.calls == 1 && cache.last_id == 3u && cache.last_result == 100, "cache recibe el id/result");

	// FileError de una lib -> loader.
	m = make_file_msg(MsgType::FileError, IoUser {eng::res::kTagLib, 5u}.encode(), -2);
	check(eng::res::route_io(m, cache, libs), "route lib");
	check(libs.calls == 1 && libs.last_id == 5u, "loader recibe el id");
	check(cache.calls == 1, "la cache no se cruza");

	// Tag desconocido (stream) -> no lo consume.
	m = make_file_msg(MsgType::FileDone, IoUser {eng::res::kTagStream, 9u}.encode(), 10);
	check(!eng::res::route_io(m, cache, libs), "stream no lo enruta la fachada");

	// Un mensaje que no es de E/S -> no lo consume.
	Msg vb {};
	vb.type = MsgType::VBlank;
	check(!eng::res::route_io(vb, cache, libs), "VBlank no es E/S");
}

} // namespace

int main() {
	test_iouser();
	test_route();

	if (failures == 0) {
		std::printf("OK: E/S asincrona (IoUser) y enrutado por tag validados.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
