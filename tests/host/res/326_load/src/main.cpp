// ============================================================================
// Test HOST-326: carga tipada de assets (eng::res::load<Tag>).
// ============================================================================
//
// Respalda `eng/res/load.hpp`: el medio/alineación por dominio (`DomainAsset`) y la carga
// de bytes a un `Block<Tag>` (copia + reserva) sobre los **bancos tipados** (`MemoryManager`),
// sustituyendo `allocate_block` + `memcpy`. Comprueba la copia, la alineación a 16 de los planos,
// el rechazo de fuente vacía y de overflow (bloque inválido, sin excepciones) y que
// `MemoryKind::Fast` cae a Slow cuando no hay Fast.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/res/326_load

#include <cstdio>

#include <eng/memory/memory_manager.hpp>
#include <eng/res/load.hpp>

// El medio/alineación por dominio es lo que blinda la carga (una sola verdad).
static_assert(eng::res::DomainAsset<eng::PlaneTag>::kind == eng::MemoryKind::Chip);
static_assert(eng::res::DomainAsset<eng::PlaneTag>::align == 16u);
static_assert(eng::res::DomainAsset<eng::MusicTag>::align == 4u);

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
	std::printf("== HOST-326 load ==\n");

	eng::u8 src[100] {};
	for (eng::u32 i = 0; i < sizeof(src); ++i) {
		src[i] = static_cast<eng::u8>(i * 7u + 1u);
	}

	eng::u8 chip_buf[1024] {};
	eng::u8 slow_buf[256] {};
	eng::MemoryManager ms {};
	ms.configure(chip_buf, sizeof(chip_buf), slow_buf, sizeof(slow_buf), nullptr, 0u, 16u);

	// --- Carga a Chip con la alineación del dominio -------------------------
	{
		const auto b = eng::res::load<eng::PlaneTag>(
			ms, eng::Span<const eng::u8> {src, sizeof(src)});
		check(b.valid(), "bloque valido");
		check(b.kind == eng::MemoryKind::Chip, "vive en Chip");
		check(reinterpret_cast<eng::uintptr>(b.view.data()) % 16u == 0u, "alineado a 16");
		bool same = true;
		for (eng::u32 i = 0; i < sizeof(src); ++i) {
			if (b.view.data()[i] != src[i]) {
				same = false;
			}
		}
		check(same, "datos copiados byte a byte");
		check(b.view.size() >= sizeof(src), "reserva con margen");
		check(ms.chip().free_bytes() < ms.chip().capacity(), "consume el banco Chip");
	}

	// --- Fuente vacía -> inválido -------------------------------------------
	{
		const auto e = eng::res::load<eng::PlaneTag>(ms, eng::Span<const eng::u8> {});
		check(!e.valid(), "fuente vacia -> bloque invalido");
	}

	// --- Overflow -> inválido (sin excepciones) -----------------------------
	{
		eng::u8 tiny[8] {};
		eng::MemoryManager ms2 {};
		ms2.configure(tiny, sizeof(tiny), nullptr, 0u, nullptr, 0u, 16u);
		const auto o =
			eng::res::load<eng::PlaneTag>(ms2, eng::Span<const eng::u8> {src, sizeof(src)});
		check(!o.valid(), "no cabe -> bloque invalido");
	}

	// --- `fast_or_slow` (helper de banco): sin Fast cae a Slow --------------
	{
		const auto f = eng::fast_or_slow<eng::MapCellsTag>(ms, 64u);
		check(f.valid(), "fast_or_slow sin Fast -> bloque valido");
		check(ms.slow().free_bytes() < ms.slow().capacity(), "cae a Slow cuando no hay Fast");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: res::load (dominio, copia, alineacion, overflow, Fast->Slow) validado.\n");
	return 0;
}
