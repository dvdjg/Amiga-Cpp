// HOST-393: DynamicString con asignador explícito y vistas de longitud exacta.

#include <cstdio>

#include <eng/core/util/arena_alloc.hpp>
#include <eng/core/util/allocator.hpp>
#include <eng/core/util/dynamic_string.hpp>

namespace eu = eng::util;

namespace {

unsigned g_fail = 0u;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

} // namespace

int main() {
	std::printf("== HOST-393 dynamic_string ==\n");

	static_assert(eu::Allocator<eu::BumpAlloc>, "BumpAlloc cumple Allocator");
	static_assert(eu::Allocator<eu::ArenaAlloc>, "ArenaAlloc cumple Allocator");

	// El tipo de texto no incluye ni presupone HeapAlloc; puede crecer en una arena del engine.
	{
		alignas(16) eng::u8 storage[128] {};
		eng::LinearArena arena {storage, sizeof(storage), eng::MemoryKind::Any};
		eu::DynamicString<eu::ArenaAlloc> text {eu::ArenaAlloc {arena}};
		check(text.append("arena"), "append sobre ArenaAlloc");
		check(text.view() == eu::StringView("arena") && arena.used() > 0u,
		      "texto respaldado por LinearArena");
	}

	// La vista fuente debe reconstruirse tras reserve, que puede mover el buffer.
	{
		alignas(16) eng::u8 storage[128] {};
		eu::BumpAlloc alloc {eng::Span<eng::u8> {storage, sizeof(storage)}};
		eu::DynamicString<eu::BumpAlloc> text {alloc};
		check(text.append("abc"), "append inicial");
		const eu::StringView original = text.view();
		check(text.append(original), "auto-append reserva y copia");
		check(text.view() == eu::StringView("abcabc"), "auto-append conserva el contenido");
		check(text.append(text.view().substr(1u, 3u)), "auto-append de una subvista");
		check(text.view() == eu::StringView("abcabcbca"),
		      "subvista propia sobrevive al crecimiento");

		const char embedded[] = {'x', '\0', 'y'};
		check(text.append(eu::StringView {embedded, sizeof(embedded)}), "append con NUL interno");
		check(text.size() == 12u && text.view()[10] == '\0' && text.view()[11] == 'y',
		      "la longitud incluye bytes NUL internos");
	}

	// Una arena agotada no debe perder ni truncar el contenido existente.
	{
		alignas(16) eng::u8 storage[4] {};
		eu::BumpAlloc alloc {eng::Span<eng::u8> {storage, sizeof(storage)}};
		eu::DynamicString<eu::BumpAlloc> text {alloc};
		check(text.append("abc"), "texto inicial cabe en la arena");
		check(!text.append("de"), "append falla al agotarse la arena");
		check(text.view() == eu::StringView("abc"), "fallo deja el texto intacto");
	}

	if (g_fail != 0u) {
		std::printf("FALLOS: %u\n", g_fail);
		return 1;
	}
	std::printf("OK: DynamicString con BumpAlloc validado.\n");
	return 0;
}
