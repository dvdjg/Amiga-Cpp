// ============================================================================
// Test HOST-302: teclas comunes del keymap vs la tabla de la AHRM 3.ª.
// ============================================================================
//
// Valida `eng::ui::rawkey_to_key` para las teclas del area comun (0x40-0x5F) contra la
// tabla «RAW Keycodes 40-5F hex (Codes common to all keyboards)» del Amiga Hardware
// Reference Manual 3.ª edicion (docs/reference/ahrm/). Fija en particular:
//   - Space = 0x40 (que en la tabla base 0x00-0x3F NO existe);
//   - 0x4C = cursor ARRIBA y 0x4D = cursor ABAJO (se corrigio una inversion).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/ui/302_ui_keymap_specials

#include <cstdio>

#include <eng/ui/keymap.hpp>

namespace {

int g_fail = 0;
void check(bool ok, const char* m) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", m);
		++g_fail;
	}
}

using eng::ui::kKeyBackspace;
using eng::ui::kKeyDelete;
using eng::ui::kKeyDown;
using eng::ui::kKeyEsc;
using eng::ui::kKeyLeft;
using eng::ui::kKeyReturn;
using eng::ui::kKeyRight;
using eng::ui::kKeyTab;
using eng::ui::kKeyUp;

} // namespace

int main() {
	std::printf("== HOST-302 keymap specials ==\n");

	// Tabla AHRM «RAW Keycodes 40-5F» (teclas comunes a todos los teclados).
	check(eng::ui::rawkey_to_key(0x40u, false) == ' ', "0x40 = Space");
	check(eng::ui::rawkey_to_key(0x41u, false) == kKeyBackspace, "0x41 = Backspace");
	check(eng::ui::rawkey_to_key(0x42u, false) == kKeyTab, "0x42 = Tab");
	check(eng::ui::rawkey_to_key(0x44u, false) == kKeyReturn, "0x44 = Return");
	check(eng::ui::rawkey_to_key(0x45u, false) == kKeyEsc, "0x45 = Escape");
	check(eng::ui::rawkey_to_key(0x46u, false) == kKeyDelete, "0x46 = Delete");

	// Cursor: la AHRM da 0x4C = up y 0x4D = down (antes estaban invertidas).
	check(eng::ui::rawkey_to_key(0x4cu, false) == kKeyUp, "0x4C = cursor arriba");
	check(eng::ui::rawkey_to_key(0x4du, false) == kKeyDown, "0x4D = cursor abajo");
	check(eng::ui::rawkey_to_key(0x4eu, false) == kKeyRight, "0x4E = cursor derecha");
	check(eng::ui::rawkey_to_key(0x4fu, false) == kKeyLeft, "0x4F = cursor izquierda");

	// Space tambien debe poder insertarse en un EditBox (es imprimible).
	check(eng::ui::is_printable_key(eng::ui::rawkey_to_key(0x40u, false)), "Space imprimible");

	if (g_fail == 0) {
		std::printf("OK: teclas comunes del keymap (AHRM 40-5F) validadas.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es)\n", g_fail);
	return 1;
}
