// ============================================================================
// Test HOST-079: StringView, FunctionRef y Callback (vistas y callables).
// ============================================================================
//
// Respalda `eng/core/util/string_view.hpp`, `eng/core/util/function_ref.hpp` y
// `eng/core/util/callback.hpp`.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/079_string_view_function_ref

#include <cstdio>

#include <eng/core/util/callback.hpp>
#include <eng/core/util/function_ref.hpp>
#include <eng/core/util/string_view.hpp>
#include <eng/core/util/type_traits.hpp>

namespace eu = eng::util;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

int triple(int x) {
	return x * 3;
}

struct Add {
	int base = 0;
	int operator()(int x) const { return base + x; }
};

/// Función de orden superior que recibe un callable por referencia.
int apply(eu::FunctionRef<int(int)> fn, int x) {
	return fn(x);
}

int g_sink = 0;
void accumulate_into(eu::FunctionRef<void(int)> fn, int x) {
	fn(x);
}

void cb_add(void* ctx, int x) {
	*static_cast<int*>(ctx) += x;
}

} // namespace

int main() {
	std::printf("== HOST-079 string_view + function_ref ==\n");

	// --- StringView ----------------------------------------------------------
	const eu::StringView lit {"VIDAS"};
	check(lit.size() == 5u && !lit.empty(), "tamaño del literal");
	check(lit[0] == 'V' && lit.front() == 'V' && lit.back() == 'S', "acceso");
	check(lit == eu::StringView("VIDAS"), "operator== (mismo contenido)");
	check(lit != eu::StringView("VIDA"), "operator!= (distinto tamaño)");

	const eu::StringView sub = lit.substr(1, 3);
	check(sub.size() == 3u && sub[0] == 'I' && sub[2] == 'A', "substr(1,3)");
	check(lit.substr(10).empty(), "substr fuera de rango queda vacío");

	eu::StringView trim {"  hola  "};
	trim.remove_prefix(2);
	trim.remove_suffix(2);
	check(trim == eu::StringView("hola"), "remove_prefix/suffix");

	check(lit.starts_with('V') && lit.ends_with('S'), "starts_with/ends_with char");
	check(lit.starts_with(eu::StringView("VID")) && lit.ends_with(eu::StringView("DAS")),
	      "starts_with/ends_with StringView");
	check(!lit.starts_with(eu::StringView("XYZ")), "starts_with falso");

	check(lit.find('D') == 2u, "find(char)");
	check(lit.find(eu::StringView("DAS")) == 2u, "find(StringView)");
	check(lit.find('Z') == eu::StringView::npos, "find ausente = npos");

	const char raw[8] = {'P', 'U', 'N', 'T', 'O', 'S', '\0', 'X'};
	const eu::StringView from_buf(raw, 6);
	check(from_buf.size() == 6u && from_buf == eu::StringView("PUNTOS"), "constructor (ptr, n)");

	// --- FunctionRef ---------------------------------------------------------
	check(apply(triple, 4) == 12, "función libre");
	const auto add_one = [](int x) { return x + 1; };
	check(apply(add_one, 4) == 5, "lambda sin captura");
	int captured = 10;
	const auto add_captured = [captured](int x) { return x + captured; };
	check(apply(add_captured, 5) == 15, "lambda con captura (referencia, sin copia)");
	Add add {20};
	check(apply(add, 5) == 25, "functor");

	g_sink = 0;
	const auto sink = [](int x) { g_sink = g_sink + x; };
	accumulate_into(sink, 7);
	check(g_sink == 7, "FunctionRef de retorno void");

	// --- Callback (fn + ctx, POD) --------------------------------------------
	static_assert(eu::is_trivially_copyable_v<eu::Callback<int>>, "Callback es POD");
	int cb_state = 0;
	eu::Callback<int> cb {&cb_add, &cb_state};
	check(cb.valid() && static_cast<bool>(cb), "Callback instalado");
	cb(5);
	cb(3);
	check(cb_state == 8, "Callback pasa el contexto");
	eu::Callback<int> empty {};
	check(!empty.valid(), "Callback vacío no válido");
	empty(1); // no-op
	check(cb_state == 8, "Callback vacío no llama");
	cb.clear();
	check(!cb.valid(), "clear desactiva el Callback");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: StringView y FunctionRef validados.\n");
	return 0;
}
