// ============================================================================
// Test HOST-263: GUI — keymaps nacionales (ES/FR/IT/DE/RU).
// ============================================================================
//
// Valida que `rawkey_to_key(raw, shift, layout)` traduce la MISMA posicion fisica (rawkey) a los
// caracteres propios de cada distribucion (ñ, ç, ¡/¿ en ES; Y/Z y ü/ö/ä en DE; A/Q y Z/W en FR;
// è/ò/ù en IT; cirilico en RU). Ver `amiga-bootcamp/11_libraries/keymap.md`.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/263_ui_keymap_layouts

#include <cstdio>
#include <cstring>

#include <eng/os/message.hpp>
#include <eng/ui/editbox.hpp>
#include <eng/ui/keymap.hpp>
#include <eng/ui/msg_adapter.hpp>
#include <eng/ui/widgets.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

using eng::ui::KeyboardLayout;
constexpr eng::u16 k(eng::u8 raw, bool shift, KeyboardLayout l) {
	return eng::ui::rawkey_to_key(raw, shift, l);
}

} // namespace

int main() {
	// US por defecto: la misma posicion que DE da 'y', en US da 'y'.
	check(k(0x15u, false, KeyboardLayout::Us) == static_cast<eng::u16>('y'), "US 0x15 -> y");

	// --- Español ---
	check(k(0x29u, false, KeyboardLayout::Es) == 0xf1u, "ES 0x29 -> ñ");
	check(k(0x29u, true, KeyboardLayout::Es) == 0xd1u, "ES 0x29+shift -> Ñ");
	check(k(0x0cu, false, KeyboardLayout::Es) == 0xa1u, "ES 0x0C -> ¡");
	check(k(0x0cu, true, KeyboardLayout::Es) == 0xbfu, "ES 0x0C+shift -> ¿");
	check(k(0x0du, false, KeyboardLayout::Es) == 0xe7u, "ES 0x0D -> ç");
	check(k(0x2bu, true, KeyboardLayout::Es) == 0xaau, "ES 0x2B+shift -> ª");

	// --- Alemán (QWERTZ) ---
	check(k(0x15u, false, KeyboardLayout::De) == static_cast<eng::u16>('z'), "DE 0x15 -> z");
	check(k(0x15u, true, KeyboardLayout::De) == static_cast<eng::u16>('Z'), "DE 0x15+shift -> Z");
	check(k(0x31u, false, KeyboardLayout::De) == static_cast<eng::u16>('y'), "DE 0x31 -> y");
	check(k(0x1au, false, KeyboardLayout::De) == 0xfcu, "DE 0x1A -> ü");
	check(k(0x0bu, false, KeyboardLayout::De) == 0xdfu, "DE 0x0B -> ß");
	check(k(0x29u, false, KeyboardLayout::De) == 0xe4u, "DE 0x29 -> ä");

	// --- Francés (AZERTY) ---
	check(k(0x10u, false, KeyboardLayout::Fr) == static_cast<eng::u16>('a'), "FR 0x10 -> a");
	check(k(0x10u, true, KeyboardLayout::Fr) == static_cast<eng::u16>('A'), "FR 0x10+shift -> A");
	check(k(0x20u, false, KeyboardLayout::Fr) == static_cast<eng::u16>('q'), "FR 0x20 -> q");
	check(k(0x11u, false, KeyboardLayout::Fr) == static_cast<eng::u16>('z'), "FR 0x11 -> z");
	check(k(0x31u, false, KeyboardLayout::Fr) == static_cast<eng::u16>('w'), "FR 0x31 -> w");
	check(k(0x02u, false, KeyboardLayout::Fr) == 0xe9u, "FR 0x02 -> é");

	// --- Italiano ---
	check(k(0x1au, false, KeyboardLayout::It) == 0xe8u, "IT 0x1A -> è");
	check(k(0x1au, true, KeyboardLayout::It) == 0xe9u, "IT 0x1A+shift -> é");
	check(k(0x0cu, false, KeyboardLayout::It) == 0xecu, "IT 0x0C -> ì");

	// --- Ruso (cirílico) ---
	check(k(0x10u, false, KeyboardLayout::Ru) == 0x439u, "RU 0x10 -> й");
	check(k(0x10u, true, KeyboardLayout::Ru) == 0x419u, "RU 0x10+shift -> Й");
	check(k(0x24u, false, KeyboardLayout::Ru) == 0x43fu, "RU 0x24 -> п");

	// --- teclas especiales comunes a todas las distribuciones ---
	check(k(0x44u, false, KeyboardLayout::Ru) == eng::ui::kKeyReturn, "Return comun");
	check(k(0x4fu, false, KeyboardLayout::De) == eng::ui::kKeyLeft, "Left comun");

	// --- dispatch_msg aplica la distribucion ---
	eng::ui::Panel root;
	root.bounds = eng::ui::Rect {0, 0, 40u, 12u};
	char buf[8] = "";
	eng::ui::EditBox e;
	e.bounds = eng::ui::Rect {0, 0, 30u, 10u};
	e.buf = buf;
	e.cap = sizeof(buf);
	root.add_child(&e);
	eng::ui::UiContext ctx;
	ctx.set_root(&root);
	ctx.set_focus(&e);

	eng::os::Msg m {};
	m.type = eng::os::MsgType::KeyDown;
	m.payload.key = {0x15u, 0u}; // posicion de la 'y' en US; 'z' en DE
	eng::ui::dispatch_msg(ctx, m, KeyboardLayout::De);
	check(buf[0] == 'z' && e.len == 1u, "dispatch_msg con layout DE inserta 'z'");

	if (failures == 0) {
		std::printf("OK: GUI keymaps nacionales (ES/FR/IT/DE/RU) validados.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
