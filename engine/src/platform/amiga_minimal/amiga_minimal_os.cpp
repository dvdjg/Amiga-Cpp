#include "amiga_minimal_internal.hpp"

#include <eng/os/input.hpp>
#include <eng/os/os.hpp>
#include <eng/platform/input_poll.hpp>

/// \file amiga_minimal_os.cpp
/// Implementación Amiga de la fachada del mini-SO (`eng::os`): el puerto del sistema, el contador de
/// frames y el **tick** que latcha el VBlank y pollea los productores de entrada (ratón en el
/// puerto 1 con `JOY0DAT`, joystick en el 2 con `JOY1DAT`; fuegos en CIA-A PRA). Referencias:
/// `docs/engine/architecture/MINI_OS_INPUT.md` y AHRM 3.ª ("Reading Digital Joystick Controllers").

namespace eng::os {
namespace {

MsgPort<32> g_port {};
VBlankLatch g_vblank {};
volatile eng::u32 g_frame = 0u;
JoyProducer g_joy {};
MouseProducer g_mouse {};

} // namespace

MsgPort<32>& system_port() { return g_port; }

eng::u32 frame_count() { return g_frame; }

void post_user(eng::u32 code, eng::u32 a, eng::u32 b) {
	Msg m {};
	m.type = MsgType::User;
	m.payload.user = {code, a, b};
	(void)g_port.post(m);
}

void request_quit() {
	Msg m {};
	m.type = MsgType::Quit;
	(void)g_port.post(m);
}

void tick() {
	using eng::amiga::detail::ciaa_reg;
	using eng::amiga::detail::custom_base;

	++g_frame;
	g_vblank.signal(g_frame);
	g_port.signal(SigVBlank);

	// JOY0DAT ($DFF00A) = ratón (puerto 1); JOY1DAT ($DFF00C) = joystick (puerto 2). Fuegos en
	// CIA-A PRA ($BFE001): bit 6 = FIRE0, bit 7 = FIRE1; 0 = pulsado.
	const eng::u16 joy0 = custom_base[0x00a / 2];
	const eng::u16 joy1 = custom_base[0x00c / 2];
	const eng::u8 pra = *ciaa_reg(0x00u);

	Msg m {};
	const eng::u8 mx = static_cast<eng::u8>(joy0 & 0xffu);
	const eng::u8 my = static_cast<eng::u8>((joy0 >> 8u) & 0xffu);
	const eng::u8 mbtn = ((pra & 0x40u) == 0u) ? 1u : 0u;
	if (g_mouse.update(mx, my, mbtn, g_frame, m)) {
		(void)g_port.post(m);
	}

	const eng::u8 dirs = eng::amiga::decode_joystick(joy1);
	const eng::u8 fire = ((pra & 0x80u) == 0u) ? 1u : 0u;
	if (g_joy.update(dirs, fire, g_frame, m)) {
		(void)g_port.post(m);
	}
}

} // namespace eng::os
