// Test HOST-208: punteros "inteligentes" sin heap (eng/core/ptr.hpp).
//
// Valida `Ref<T>` (observador no propietario y anulable), `NonNull<T>` (no nulo por
// contrato) y `Opt<T>` (opcional en sitio). Sustituyen al `T*` crudo y a `std::optional`
// en un engine freestanding (sin heap, sin STL).
//
//   CXX=<g++> bash tools/run-host-tests.sh tests/host/208_ptr
#include <cstdio>

#include <eng/core/ptr.hpp>

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

	if (g_fail == 0) {
		std::printf("OK: ptr (Ref/NonNull/Opt, sin heap) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", g_fail);
	return 1;
}
