#include <eng/api/api.hpp>
#include <eng/os/message_pump.hpp>
#include <eng/os/os.hpp>
#include <eng/platform/amiga_minimal.hpp>

#include <proto/exec.h>
#include <exec/execbase.h>

#include "support/gcc8_c_support.h"

struct ExecBase* SysBase = nullptr;

extern "C" {
// Último rawkey visto (0xffffffff = ninguno) y bitmask de rawkeys vistos (128 bits). Símbolos
// estables para que el runner pueda mapear `event id -> rawkey` por el canal lateral (`mem`),
// calibrando `--keys` contra el binario.
__attribute__((used)) volatile eng::u32 g_key_last_raw = 0xffffffffu;
__attribute__((used)) volatile eng::u32 g_key_mask[4] = {0u, 0u, 0u, 0u};
__attribute__((used)) volatile eng::debug::RunStatus g_eng_run_status {
	eng::debug::run_status_magic,
	eng::debug::run_status_version,
	static_cast<eng::u16>(eng::debug::RunState::Cold),
	0,
	0,
};
}

namespace {

// -----------------------------------------------------------------------------
// Demo 212 — bucle reactivo del mini-SO (eng::os)
// -----------------------------------------------------------------------------
// El juego NO sondea hardware: el tick del mini-SO latcha el VBlank y pollea los
// productores de entrada (ratón puerto 1, joystick puerto 2) posteando mensajes; el
// bucle reactivo (`MessagePumpGame`) drena el puerto y entrega cada mensaje al App
// antes de la lógica de frame. La caja se mueve con el joystick.
//
//   bash ./tools/build/build-demo.sh demos/amiga/212_message_loop --debug --clean
//   bash ./tools/run/run-demo.sh demos/amiga/212_message_loop --wait-ms 8000
// -----------------------------------------------------------------------------

eng::s16 clamp_s16(eng::s16 v, eng::s16 lo, eng::s16 hi) {
	return v < lo ? lo : (v > hi ? hi : v);
}

// Tarea de fondo (M10): avanza un paso por cada slice de idle. El bucle solo le da
// idle en los frames **sin** mensajes, así que su contador prueba la integración.
eng::u32 g_bg_work = 0u;
bool bg_step(eng::os::TaskId, void*, eng::u32) {
	++g_bg_work;
	return true;
}

struct DemoApp {
	eng::s16 box_x = 300;
	eng::s16 box_y = 220;
	eng::u32 frames = 0;
	eng::u32 msgs = 0;
	eng::u32 vblank_seq = 0;
	eng::u16 missed = 0;
	eng::u8 joy_dirs = 0;
	eng::u8 joy_seen = 0; ///< OR de las direcciones vistas (para el run-status; pegajoso)
	eng::u32 timers = 0;  ///< `MsgType::Timer` recibidos (os::add_timer)
	eng::u32 keys = 0;
	eng::u16 key_last = 0u;
	eng::u32 bg_work = 0;

	void on_start(auto&) {
		eng::debug::mark_init_started(g_eng_run_status);
		// Teclado por IRQ de CIA-A serie: a partir de aqui los scancodes llegan como
		// KeyDown/KeyUp por el puerto (el runner los inyecta con `--keys <hex>`).
		eng::os::enable_keyboard();
		eng::debug::mark_ready(g_eng_run_status, 0x00021200u);
	}

	void on_msg(const eng::os::Msg& m) {
		++msgs;
		switch (m.type) {
		case eng::os::MsgType::Joystick:
			joy_dirs = m.payload.joy.dirs;
			joy_seen = static_cast<eng::u8>(joy_seen | joy_dirs);
			// Reporta las direcciones vistas (0x2122DDDD; pegajoso) para verificarlo desde el runner.
			g_eng_run_status.detail = 0x21220000u | joy_seen;
			break;
		case eng::os::MsgType::KeyDown:
			key_last = m.payload.key.code;
			g_key_last_raw = static_cast<eng::u32>(key_last);
			{
				const eng::u8 rk = static_cast<eng::u8>(key_last & 0x7fu);
				g_key_mask[rk >> 5u] =
					static_cast<eng::u32>(g_key_mask[rk >> 5u] | (1u << (rk & 31u)));
			}
			++keys;
			// Reporta el último rawkey y el conteo por el run-status (0x2121_LLKK: LL = último
			// rawkey, KK = nº de KeyDown): evidencia de que el teclado por IRQ llega.
			g_eng_run_status.detail =
			    0x21210000u | (static_cast<eng::u32>(key_last) << 8) | (keys & 0xffu);
			break;
		case eng::os::MsgType::Timer:
			++timers;
			// Reporta el conteo de timers (0x2123TTTT) para verificarlo desde el runner.
			g_eng_run_status.detail = 0x21230000u | (timers & 0xffffu);
			break;
		case eng::os::MsgType::MouseMove:
			// El ratón (puerto 1) también mueve la caja (posición absoluta ya escalada).
			box_x = clamp_s16(static_cast<eng::s16>(m.payload.mouse.x * 2), 44, 700);
			box_y = clamp_s16(static_cast<eng::s16>(m.payload.mouse.y), 150, 480);
			break;
		default:
			break;
		}
	}

	void on_frame(eng::u32 f) {
		frames = f;
		bg_work = g_bg_work;
		// Reporta el avance del fondo por el run-status (0x2120BBBB). Si ya llegó alguna tecla,
		// conserva su marca (0x2121KKKK) para que el runner pueda verla al final.
		if (keys == 0u && joy_seen == 0u && timers == 0u) {
			g_eng_run_status.detail = 0x21200000u | (g_bg_work & 0xffffu);
		}

		// kJoyRight=1<<3, kJoyLeft=1<<2, kJoyDown=1<<1, kJoyUp=1<<0.
		if ((joy_dirs & 0x08u) != 0u) { box_x = clamp_s16(static_cast<eng::s16>(box_x + 3), 44, 700); }
		if ((joy_dirs & 0x04u) != 0u) { box_x = clamp_s16(static_cast<eng::s16>(box_x - 3), 44, 700); }
		if ((joy_dirs & 0x02u) != 0u) { box_y = clamp_s16(static_cast<eng::s16>(box_y + 3), 150, 480); }
		if ((joy_dirs & 0x01u) != 0u) { box_y = clamp_s16(static_cast<eng::s16>(box_y - 3), 150, 480); }
	}

	void on_render(auto& backend) {
		eng::os::Msg vb {};
		auto& d = backend.debug();
		d.clear();
		d.filled_rect(40, 40, 720, 520, 0x00082030);
		d.rect(40, 40, 720, 520, 0x00ffffff);
		d.text(64, 60, "AMG demo 212 - mini-OS message loop", 0x00ffffff);
		d.text(64, 88, "VBlank + input por mensajes (sin sondear hardware)", 0x0000ff80);

		char line[64];
		char* p = line;
		p = append(p, "frames: ");
		p = append_u32(p, frames);
		p = append(p, "   msgs: ");
		p = append_u32(p, msgs);
		p = append(p, "   joy: ");
		p = append_u32(p, joy_dirs);
		p = append(p, "   keys: ");
		p = append_u32(p, keys);
		p = append(p, "   bg: ");
		p = append_u32(p, bg_work);
		p = append(p, "   t: ");
		p = append_u32(p, timers);
		p = append(p, "   last: 0x");
		p = append_hex(p, key_last);
		*p = '\0';
		d.text(64, 118, line, 0x00ffff00);
		d.text(64, 148, "mueve la caja con el joystick (puerto 2) o el raton (puerto 1)", 0x00aaaaaa);
		d.text(64, 176, "bg = slices de idle de la tarea de fondo (M10; solo sin mensajes)", 0x0000ff80);

		// La caja.
		d.filled_rect(box_x, box_y, static_cast<eng::s16>(box_x + 20), static_cast<eng::s16>(box_y + 20),
			      0x00ff8000);
		eng::debug::probe_when_ready(g_eng_run_status, frames);
	}

private:
	static char* append(char* p, const char* s) {
		while (*s != '\0') { *p++ = *s++; }
		return p;
	}
	static char* append_u32(char* p, eng::u32 v) {
		char tmp[10];
		eng::u8 n = 0;
		do { tmp[n++] = static_cast<char>('0' + (v % 10u)); v /= 10u; } while (v != 0u && n < 10u);
		while (n > 0u) { *p++ = tmp[--n]; }
		return p;
	}
	/// Añade `v` en hex de 4 dígitos (p. ej. el último rawkey).
	static char* append_hex(char* p, eng::u16 v) {
		constexpr char kHex[] = "0123456789abcdef";
		for (eng::s8 shift = 12; shift >= 0; shift = static_cast<eng::s8>(shift - 4)) {
			*p++ = kHex[(v >> shift) & 0x0fu];
		}
		return p;
	}
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	eng::os::MessagePumpGame<DemoApp> game {};
	game.bind_port(eng::os::system_port());

	// Tarea de fondo (M10): el bucle le da un slice de idle solo en frames sin mensajes.
	eng::os::TaskSystem tasks {};
	(void)tasks.init();
	const eng::os::TaskId bg = tasks.create(eng::os::TaskDesc {&bg_step, nullptr, "bg", 128u});
	(void)tasks.start(bg);
	game.bind_tasks(tasks);

	eng::Engine engine { backend, game };
	// Fachada del mini-SO: habilita la entrada y **engancha el latido al VBlank del `Engine`**
	// (ya no se llama `os::tick` en el bucle). El timer de usuario va a 1 frame: es el único
	// periodo verificado en hardware; con periodo > 1 el backend postea los `Timer` pero el
	// pump no los recibe (bug abierto, ver ROADMAP_MINI_OS.md).
	(void)eng::os::init(engine, eng::os::InputAll);
	eng::os::add_timer(1u, 1u);
	engine.run_frames_polling(0xffff);

	return 0;
}
