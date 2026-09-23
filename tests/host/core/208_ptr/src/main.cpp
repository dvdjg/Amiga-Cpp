// Test HOST-208: punteros "inteligentes" sin heap (eng/core/ptr.hpp).
//
// Valida `Ref<T>` (observador no propietario y anulable), `NonNull<T>` (no nulo por
// contrato) y `Opt<T>` (opcional en sitio). Sustituyen al `T*` crudo y a `std::optional`
// en un engine freestanding (sin heap, sin STL).
//
//   CXX=<g++> bash tools/run-host-tests.sh tests/host/208_ptr
#include <cstdio>

#include <eng/core/ptr.hpp>
#include <eng/core/span.hpp>

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

struct Obj {
	int x = 0;
	[[nodiscard]] int get() const { return x; }
};

} // namespace

int main() {
	Obj a {42};

	// Ref: por defecto nula; desde referencia implica válida; `reset` la anula.
	eng::Ref<Obj> r;
	check(!r.valid() && !static_cast<bool>(r), "Ref por defecto es nula");
	r = a;
	check(r.valid() && r->x == 42 && (*r).x == 42 && r.get() == &a, "Ref desde referencia");
	eng::Ref<Obj> r2 {a};
	check(r2 == r && !(r2 != r), "Ref comparacion");
	r.reset();
	check(!r.valid(), "Ref::reset la anula");
	eng::Ref<Obj> r3 {static_cast<Obj*>(nullptr)};
	check(!r3.valid(), "Ref explicita desde nullptr");

	// Conversiones implicitas: `&obj` y `nullptr` construyen el `Ref` sin escribir el tipo
	// (el llamador pasa el puntero crudo o el literal y la API queda legible).
	struct Takes {
		static bool by_ref(eng::Ref<Obj> p) { return p.valid() && p->x == 42; }
		static bool by_ptr(eng::Ref<Obj> p) { return p.valid(); }
		static bool by_null(eng::Ref<Obj> p) { return !p.valid(); }
	};
	check(Takes::by_ref(a), "Ref implicita desde referencia en llamada");
	check(Takes::by_ptr(&a), "Ref implicita desde puntero en llamada");
	check(Takes::by_null(nullptr), "Ref implicita desde nullptr en llamada");
	eng::Ref<const Obj> rc3 = a;
	check(rc3.valid() && rc3->get() == 42, "Ref<const T> implicita desde referencia no-const");

	// NonNull: no puede ser nulo.
	eng::NonNull<Obj> nn {a};
	check(nn->get() == 42 && nn.get() == &a, "NonNull observa el objeto");

	// Opt: en sitio, sin heap.
	eng::Opt<int> o;
	check(!o.has_value(), "Opt por defecto vacio");
	o.set(7);
	check(o.has_value() && o.value() == 7 && *o == 7, "Opt::set y acceso");
	o.reset();
	check(!o.has_value(), "Opt::reset");
	eng::Opt<int> o2 {9};
	check(o2.has_value() && o2.value() == 9, "Opt desde valor");

	// Ref<const T>: observador de solo lectura.
	const Obj& ca = a;
	eng::Ref<const Obj> rc {ca};
	check(rc.valid() && rc->get() == 42 && rc.get() == &a, "Ref<const T> desde referencia");
	eng::Ref<const Obj> rc2 {static_cast<const Obj*>(&a)};
	check(rc2 == rc, "Ref<const T> desde puntero y comparacion");

	// Span: CTAD (`{ptr, n}` sin nombrar el tipo), array sin count y conversion
	// cualificante `Span<T> -> Span<const T>` (como `std::span`).
	int buf[8] {1, 2, 3, 4, 5, 6, 7, 8};
	int* p = buf;
	eng::Span s_ctad {p, 5u};
	static_assert(eng::detail::same<decltype(s_ctad), eng::Span<int>>::value,
		      "CTAD (T*, usize) deduce Span<int>");
	check(s_ctad.size() == 5u && s_ctad[4] == 5, "Span por CTAD {ptr, n}");
	eng::Span s_arr = buf;
	static_assert(eng::detail::same<decltype(s_arr), eng::Span<int>>::value,
		      "array deduce Span<int> y su tamano");
	check(s_arr.size() == 8u, "Span desde array deduce el tamano");
	eng::Span<const int> s_const = (eng::Span {p, 3u});
	check(s_const.size() == 3u && s_const[0] == 1, "Span<T> -> Span<const T> implicita");
	check(eng::Span<const int> {p, 2u}.size() == 2u, "inicializador {ptr, n} en Span<const T>");

	if (g_fail == 0) {
		std::printf("OK: ptr (Ref/NonNull/Opt, sin heap) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", g_fail);
	return 1;
}
