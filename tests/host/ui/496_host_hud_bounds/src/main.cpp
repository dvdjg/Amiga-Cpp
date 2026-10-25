// ============================================================================
// Test HOST-496: HostHud sin desbordamiento con nombre/telemetría largos.
// ============================================================================
//
// Reproduce el caso de capacidad insuficiente: HostHud debe truncar con
// `TextLine` (sin escribir fuera) y conservar canarios alrededor del objeto.
//
//   bash tools/run-host-tests.sh tests/host/ui/496_host_hud_bounds

#include <cstdio>
#include <cstring>

#include <eng/debug/host_hud.hpp>

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

struct Overlay {
	char text_buf[512] {};
	eng::u16 x = 0u;
	eng::u16 y = 0u;
	eng::u32 rgb = 0u;

	void clear() { text_buf[0] = '\0'; }
	void text(eng::s16 tx, eng::s16 ty, const char* value, eng::u32 color) {
		x = static_cast<eng::u16>(tx);
		y = static_cast<eng::u16>(ty);
		rgb = color;
		if (value == nullptr) {
			text_buf[0] = '\0';
			return;
		}
		eng::usize n = 0u;
		while (value[n] != '\0' && n + 1u < sizeof(text_buf)) {
			text_buf[n] = value[n];
			++n;
		}
		text_buf[n] = '\0';
	}
};

struct WrappedHud {
	eng::u32 before = 0x13572468u;
	eng::debug::HostHud hud {};
	eng::u32 after = 0x24681357u;
};

} // namespace

int main() {
	WrappedHud w {};
	Overlay overlay {};
	char long_name[420];
	char long_stats[420];
	for (eng::usize i = 0u; i < sizeof(long_name) - 1u; ++i) {
		long_name[i] = static_cast<char>('A' + (i % 26u));
		long_stats[i] = static_cast<char>('a' + (i % 26u));
	}
	long_name[sizeof(long_name) - 1u] = '\0';
	long_stats[sizeof(long_stats) - 1u] = '\0';

	w.hud.draw(overlay, long_name, eng::debug::Version {1u, 2u}, 123456u, 0u, long_stats);
	check(w.before == 0x13572468u, "canario antes intacto");
	check(w.after == 0x24681357u, "canario despues intacto");
	check(w.hud.last_draw_truncated(), "texto largo marcado como truncado");
	check(w.hud.last_text().size() == eng::debug::HostHud::kTextCapacity - 1u,
	      "usa toda la capacidad (sin terminador)");
	check(overlay.text_buf[sizeof(overlay.text_buf) - 1u] == '\0', "overlay termina en NUL");
	check(std::strlen(overlay.text_buf) == eng::debug::HostHud::kTextCapacity - 1u,
	      "overlay no excede HostHud y llena la linea");
	check(overlay.x == eng::debug::HostHud::kMarginX, "margen X");
	check(overlay.y == eng::debug::HostHud::kMarginY, "margen Y");

	// Una línea corta no debe truncarse.
	WrappedHud short_hud {};
	Overlay short_overlay {};
	short_hud.hud.draw(short_overlay, "HUD CORTO", eng::debug::Version {1u, 0u}, 1u, 0u, "ok");
	check(!short_hud.hud.last_draw_truncated(), "texto corto no truncado");
	check(short_hud.before == 0x13572468u && short_hud.after == 0x24681357u,
	      "canarios cortos intactos");

	if (g_fail != 0) {
		std::printf("[FAIL] %d comprobaciones\n", g_fail);
		return 1;
	}
	std::printf("OK: HostHud trunca sin desbordar y respeta canarios.\n");
	return 0;
}
