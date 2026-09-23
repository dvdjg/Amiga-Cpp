// ============================================================================
// Test HOST-248: loader de codigo relocatable .englib (eng/res/dynloader.hpp).
// ============================================================================
//
// Construye una imagen .englib en RAM (code + relocs + exports), la carga/relocaliza y comprueba
// que las relocaciones se aplican y que `symbol` resuelve por nombre. No ejecuta codigo.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/248_dynloader

#include <cstdio>
#include <cstdint>

#include <eng/res/dynloader.hpp>

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

/// Monta un .englib minimo: header + code(8) + 1 reloc + 1 export ("foo" -> offset 0).
eng::usize build_lib(eng::u8* blob) {
	auto* hdr = reinterpret_cast<EngLibHeader*>(blob);
	hdr->magic = kEngLibMagic;
	hdr->version = 1u;
	hdr->code_size = 8u;
	hdr->data_size = 0u;
	hdr->bss_size = 0u;
	hdr->entry_offset = 0u;
	hdr->reloc_count = 1u;
	hdr->export_count = 1u;

	eng::u8* const code = blob + sizeof(EngLibHeader);
	*reinterpret_cast<eng::u32*>(code) = 0u; // celda relocable (se le sumara la base)

	auto* relocs = reinterpret_cast<eng::u32*>(code + 8u);
	relocs[0] = 0u; // reloc en code+0

	auto* ex = reinterpret_cast<LibExport*>(relocs + 1u);
	ex[0] = LibExport {DynLoader::hash_name("foo"), 0u};

	return sizeof(EngLibHeader) + 8u + 4u + sizeof(LibExport);
}

void test_load() {
	alignas(8) eng::u8 blob[64] {};
	const eng::usize n = build_lib(blob);
	eng::u8* const code = blob + sizeof(EngLibHeader);

	DynLoader dl;
	const LibHandle h = dl.declare("foo.englib");
	check(h == 1u, "declare devuelve handle 1");
	check(dl.load(h, eng::Span<eng::u8> {blob, n}), "load");
	check(dl.state(h) == LibState::Ready, "Ready");

	const eng::u32 base = static_cast<eng::u32>(reinterpret_cast<eng::uintptr>(code));
	check(*reinterpret_cast<eng::u32*>(code) == base, "la relocacion se aplico (celda = base)");
	check(dl.symbol(h, "foo") == code, "symbol foo = base del code");
	check(dl.symbol(h, "bar") == nullptr, "symbol inexistente = nullptr");

	dl.unload(h);
	check(dl.state(h) == LibState::Empty, "unload deja Empty");
	check(dl.symbol(h, "foo") == nullptr, "tras unload no hay simbolos");
}

void test_bad() {
	eng::u8 short_img[8] {};
	DynLoader dl;
	const LibHandle h = dl.declare("bad");
	check(!dl.load(h, eng::Span<eng::u8> {short_img, 8u}), "imagen corta -> falla");
	check(dl.state(h) == LibState::Error, "estado Error");

	eng::u8 no_magic[32] {};
	DynLoader dl2;
	const LibHandle h2 = dl2.declare("nomagic");
	check(!dl2.load(h2, eng::Span<eng::u8> {no_magic, 32u}), "magic invalido -> falla");
}

void test_hash() {
	check(DynLoader::hash_name("foo") == DynLoader::hash_name("foo"), "hash estable");
	check(DynLoader::hash_name("foo") != DynLoader::hash_name("bar"), "hash distinto");
}

} // namespace

int main() {
	test_load();
	test_bad();
	test_hash();

	if (failures == 0) {
		std::printf("OK: .englib (relocacion y simbolos) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
