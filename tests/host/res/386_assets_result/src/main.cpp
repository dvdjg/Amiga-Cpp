// ============================================================================
// Test HOST-386: Assets con Result (add_checked / create_checked).
// ============================================================================
//
// Respalda `eng::Assets` (`eng/api/assets.hpp`) sobre `Expected<void/Block, Result>`: la reserva
// de recursos devuelve la **causa** del fallo (sin gestor -> InvalidArgument; no cabe ->
// OutOfMemory; tabla llena -> HardwareLimit). Unifica con el resto de APIs de reserva
// (`MemBank::Status`, `MeshStatus`). Ver `ROADMAP_MEMORY_OWNERSHIP.md` Fase 4.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/res/386_assets_result

#include <cstdio>

#include <eng/api/assets.hpp>
#include <eng/core/types/domains.hpp>
#include <eng/memory/memory_manager.hpp>

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

} // namespace

alignas(16) eng::u8 g_chip[8u * 1024u];

int main() {
	std::printf("== HOST-386 assets_result ==\n");

	static const eng::u8 blob[64] {};

	// Sin gestor ligado -> InvalidArgument.
	{
		eng::Assets a {};
		auto r = a.add_checked<eng::PlaneTag>("x", blob, sizeof(blob));
		check(!r.has_value() && r.error() == eng::Result::InvalidArgument,
		      "sin gestor -> InvalidArgument");
		auto c = a.create_checked<eng::PlaneTag>(32u);
		check(!c.has_value() && c.error() == eng::Result::InvalidArgument,
		      "create sin gestor -> InvalidArgument");
	}

	eng::MemoryManager mm {};
	mm.configure(g_chip, sizeof(g_chip), nullptr, 0u, nullptr, 0u, 16u);
	eng::Assets a {};
	a.bind(mm);

	// Éxito: copia el blob y lo registra.
	auto ok = a.add_checked<eng::PlaneTag>("map", blob, sizeof(blob));
	check(ok.has_value(), "add valido -> Ok");
	check(a.has("map"), "el asset queda registrado");
	check(a.bytes("map").size() == sizeof(blob), "bytes() devuelve el tamano");

	// Sin datos -> InvalidArgument.
	auto bad = a.add_checked<eng::PlaneTag>("n", nullptr, 0u);
	check(!bad.has_value() && bad.error() == eng::Result::InvalidArgument,
	      "datos nulos -> InvalidArgument");

	// No cabe -> OutOfMemory.
	auto big = a.create_checked<eng::PlaneTag>(static_cast<eng::u32>(sizeof(g_chip)) + 1u);
	check(!big.has_value() && big.error() == eng::Result::OutOfMemory, "no cabe -> OutOfMemory");

	// Estado limpio y determinista: `clear()` libera y **vacía** la tabla.
	a.clear();
	check(mm.chip().used_bytes() == 0u && a.count() == 0u, "clear() libera y vacia la tabla");

	// Rellenar la tabla hasta `kMaxBlocks`: los que quepan registran; el desbordamiento devuelve
	// `HardwareLimit` y **no** deja memoria viva.
	eng::u8 ok_count = 0u;
	eng::u8 limit_count = 0u;
	for (eng::u8 i = 0u; i < eng::Assets::kMaxBlocks + 4u; ++i) {
		char name[8] {};
		name[0] = static_cast<char>('a' + (i % 26u));
		name[1] = static_cast<char>('0' + (i / 26u));
		const auto r = a.add_checked<eng::PlaneTag>(name, blob, sizeof(blob));
		if (r.has_value()) {
			++ok_count;
		} else if (r.error() == eng::Result::HardwareLimit) {
			++limit_count;
		}
	}
	check(ok_count >= 1u && limit_count >= 1u, "tabla llena -> algunos Ok y algunos HardwareLimit");
	check(a.tracked_count() == a.count(), "todo asset registrado esta rastreado (liberable)");

	// `reset_phase` libera todos los bloques rastreados; el banco vuelve a estar libre.
	a.reset_phase();
	check(mm.chip().used_bytes() == 0u, "reset_phase libera todo");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: Assets con Result (add_checked/create_checked + reset_phase) validado.\n");
	return 0;
}
