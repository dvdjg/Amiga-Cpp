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
KeyProducer g_keys {};

/// IRQ del teclado (SP de CIA-A): lee `SDR`, produce `KeyDown`/`KeyUp` y pulsa el handshake.
///
/// Handshake (AHRM 3.ª, "The Keyboard"): tras recibir un byte hay que pulsar SP **bajo y luego
/// alto**, con el pulso bajo de >= 85 µs, para que el teclado envíe la siguiente tecla. Las **dos
/// transiciones deben ir juntas** (un solo pulso): si se separan (p. ej. el alta en el siguiente
/// `tick`), el MCU emulado interpreta cada transición como un handshake y **reenvía** el byte
/// (duplicado). El pulso bajo se hace con una espera activa corta; a 7 MHz PAL ~150 iteraciones
/// superan los 85 µs. Se ejecuta dentro de la ISR de nivel 2, que puede ser preemptada por IRQ de
/// nivel 3/4 (audio), así que no bloquea el camino crítico.
void os_kbd_isr() {
	using eng::amiga::detail::ciaa_reg;
	Msg m {};
	const eng::u8 raw = *ciaa_reg(0x0cu); // SDR ($BFEC01)
	if (g_keys.update(raw, g_frame, m)) {
		(void)g_port.post(m);
	}
	volatile eng::u8* const cra = ciaa_reg(0x0eu);
	*cra = static_cast<eng::u8>(*cra & 0xbfu);            // SPMODE=0: SP bajo
	for (volatile eng::u16 i = 0u; i < 150u; ++i) {}      // >= 85 µs (AHRM)
	*cra = static_cast<eng::u8>(*cra | 0x40u);            // SPMODE=1: SP alto (fin del pulso)
}

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

void enable_keyboard() {
	using eng::amiga::detail::ciaa_reg;
	using eng::amiga::detail::custom_base;
	using eng::amiga::detail::custom_intena_offset;
	using eng::amiga::detail::g_cia_installed;
	using eng::amiga::detail::g_cia_old_vector;
	using eng::amiga::detail::g_os_kbd_isr;

	g_os_kbd_isr = &os_kbd_isr;
	// Instala el autovector de nivel 2 si no lo hizo ya el servicio de timer (comparten vector).
	if (!g_cia_installed) {
		volatile eng::u32* const vector2 = reinterpret_cast<volatile eng::u32*>(0x68u);
		g_cia_old_vector = *vector2;
		*vector2 = reinterpret_cast<eng::u32>(&::cia_irq);
		g_cia_installed = true;
	}
	*ciaa_reg(0x0du) = 0x88u;                     // ICR: SETCLR | SP (enmascarar el teclado)
	*ciaa_reg(0x0eu) = static_cast<eng::u8>(*ciaa_reg(0x0eu) | 0x40u); // SPMODE=1: SP alto (listo)
	custom_base[custom_intena_offset] = 0xc008u;  // SETCLR | INTEN | PORTS
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

eng::u32 wait(eng::u32 mask) {
	// Host del **engine**: se coopera con `tick()` (ritmo de VBlank) hasta que alguna señal de
	// `mask` está puesta, y se devuelven los bits consumidos. En Workbench este `wait` se sustituye
	// por `Wait(señales Exec)` + volcado `Exec -> Msg` (ver `ROADMAP_WORKBENCH.md`, W5).
	if (mask == 0u) {
		return 0u;
	}
	for (;;) {
		const eng::u32 got = g_port.pending(mask);
		if (got != 0u) {
			return g_port.take_signals(got);
		}
		tick();
	}
}

} // namespace eng::os
