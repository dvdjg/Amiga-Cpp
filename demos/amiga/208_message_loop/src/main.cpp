#include <eng/api/api.hpp>
#include <eng/os/message_pump.hpp>
#include <eng/os/os.hpp>
#include <eng/platform/amiga_minimal.hpp>

#include <proto/exec.h>
#include <exec/execbase.h>

#include "support/gcc8_c_support.h"

struct ExecBase* SysBase = nullptr;

extern "C" {
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
// Demo 206 — bucle reactivo del mini-SO (eng::os)
// -----------------------------------------------------------------------------
// El juego NO sondea hardware: el tick del mini-SO latcha el VBlank y pollea los
// productores de entrada (ratón puerto 1, joystick puerto 2) posteando mensajes; el
// bucle reactivo (`MessagePumpGame`) drena el puerto y entrega cada mensaje al App
// antes de la lógica de frame. La caja se mueve con el joystick.
//
//   bash ./tools/build/build-demo.sh demos/amiga/208_message_loop --debug --clean
//   bash ./tools/run/run-demo.sh demos/amiga/208_message_loop --wait-ms 8000
// -----------------------------------------------------------------------------

eng::s16 clamp_s16(eng::s16 v, eng::s16 lo, eng::s16 hi) {
	return v < lo ? lo : (v > hi ? hi : v);
}

struct DemoApp {
	eng::s16 box_x = 300;
	eng::s16 box_y = 220;
	eng::u32 frames = 0;
	eng::u32 msgs = 0;
	eng::u32 vblank_seq = 0;
	eng::u16 missed = 0;
	eng::u8 joy_dirs = 0;

	void on_start(auto&) {
		eng::debug::mark_init_started(g_eng_run_status);
		eng::debug::mark_ready(g_eng_run_status, 0x00020600u);
	}

	void on_msg(const eng::os::Msg& m) {
		++msgs;
		switch (m.type) {
		case eng::os::MsgType::Joystick:
			joy_dirs = m.payload.joy.dirs;
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
		d.text(64, 60, "AMG demo 206 - mini-OS message loop", 0x00ffffff);
		d.text(64, 88, "VBlank + input por mensajes (sin sondear hardware)", 0x0000ff80);

		char line[64];
		char* p = line;
		p = append(p, "frames: ");
		p = append_u32(p, frames);
		p = append(p, "   msgs: ");
		p = append_u32(p, msgs);
		p = append(p, "   joy: ");
		p = append_u32(p, joy_dirs);
		*p = '\0';
		d.text(64, 118, line, 0x00ffff00);
		d.text(64, 148, "mueve la caja con el joystick (puerto 2) o el raton (puerto 1)", 0x00aaaaaa);

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
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	eng::os::MessagePumpGame<DemoApp> game {};
	game.bind_port(eng::os::system_port());
	game.tick = &eng::os::tick;
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
