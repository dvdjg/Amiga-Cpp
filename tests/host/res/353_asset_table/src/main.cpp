// ============================================================================
// Test HOST-353: tabla declarativa de assets en memoria (eng::res::AssetTable).
// ============================================================================
//
// Respalda `eng/res/asset_table.hpp`: el juego registra blobs (`incbin`) por nombre y los pide
// con su dominio (`get<PlaneTag>("abyss")` -> `ByteView<PlaneTag>`), sin tamanos ni punteros en
// la logica. Capacidad fija (sin heap) y busqueda por nombre.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/res/353_asset_table

#include <cstdio>

#include <eng/core/types/domains.hpp>
#include <eng/res/asset_table.hpp>

namespace {

int g_fail = 0;
void check(bool ok, const char* m) {
	if (!ok) {
		std::printf("[FAIL] %s\n", m);
		++g_fail;
	}
}

const eng::u8 g_img[8] = {1, 2, 3, 4, 5, 6, 7, 8};
const eng::u8 g_mod[4] = {9, 10, 11, 12};

} // namespace

int main() {
	std::printf("== HOST-353 asset_table ==\n");

	eng::res::AssetTable t {};
	check(t.add<eng::PlaneTag>("abyss", g_img, sizeof(g_img)), "add imagen");
	check(t.add<eng::MusicTag>("tune", g_mod, sizeof(g_mod)), "add modulo");
	check(t.count() == 2u && t.has("abyss") && t.has("tune"), "registrados por nombre");

	const eng::ByteView<eng::PlaneTag> img = t.get<eng::PlaneTag>("abyss");
	check(img.size() == 8u && img.data() == g_img && img[7] == 8u, "vista de dominio");

	check(t.get<eng::PlaneTag>("nope").empty(), "inexistente -> vista vacia");
	check(!t.has("nope"), "has(inexistente)");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: AssetTable (assets en memoria por nombre+dominio) validada.\n");
	return 0;
}
