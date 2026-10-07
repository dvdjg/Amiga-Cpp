#include "amiga_internal.hpp"

#include <eng/os/input.hpp>
#include <eng/os/os.hpp>
#include <eng/os/timer.hpp>
#include <eng/platform/amiga/input_poll.hpp>

/// \file amiga_os.cpp
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
PadProducer g_pad {};
eng::u8 g_input_mask = InputAll; ///< dispositivos habilitados (`input_enable`); por defecto, todos
TimerService g_timers {};        ///< timers de usuario (`add_timer`)
void (*g_frame_task)(void*, eng::u16) = nullptr; ///< tarea de frame del mini-SO (`set_frame_task`)
void* g_frame_task_user = nullptr;

/// **Estado del reloj de µs (CIA-B Timer B continuo)**. El Timer B decrementa en cada tick del E
/// clock; se lee su latch de 16 bits y se extiende a 32 bits contando las vueltas por el salto
/// entre lecturas. Es un reloj monotónico estable (TIME-003), con precisión de µs; el sondeo sigue
/// siendo por VBlank, así que un timer µs vence con latencia de hasta un frame (TIME-004 se
/// resuelve con un pump más fino, fuera del alcance de esta pasada). Ver `MINI_OS_TIME.md`.
eng::u16 g_ciab_last = 0u;      ///< última lectura del contador de 16 bits de CIA-B
eng::u32 g_ciab_high = 0u;      ///< vueltas acumuladas (bits altos del reloj de 32 bits)
bool g_ciab_ready = false;

/// Lee el contador de CIA-B Timer B y lo extiende a 32 bits. En cada lectura, si el valor de 16
/// bits **bajó** respecto a la anterior, el contador dio una vuelta (decrementa): sube `high`.
eng::u32 ciab_ticks_now() {
	using eng::amiga::detail::ciab_reg;
	// Los dos bytes de TALO/TAHI forman el contador descendente de 16 bits (hardware -> u16).
	const eng::u16 lo = *ciab_reg(6u);
	const eng::u16 hi = *ciab_reg(7u);
	const eng::u16 value = static_cast<eng::u16>(lo | static_cast<eng::u16>(hi << 8u));
	if (g_ciab_ready && value > g_ciab_last) {
		g_ciab_high += 0x10000u; // envolvió
	}
	g_ciab_last = value;
	g_ciab_ready = true;
	return g_ciab_high + (0xffffu - value); // contador descendente -> ascendente
}

/// Arranca el **Timer B de CIA-B (modo continuo, reloj E)** como reloj libre de µs. Se usa CIA-B
/// para no colisionar con el timer de fondo (CIA-A Timer A) ni con el teclado (SP de CIA-A).
void start_ciab_clock() {
	using eng::amiga::detail::ciab_reg;
	volatile eng::u8* const crb = ciab_reg(0x0fu); // CRB
	volatile eng::u8* const tbhi = ciab_reg(7u);
	volatile eng::u8* const tblo = ciab_reg(6u);
	*crb = 0x00u;                                  // parar; INMODE=0 (reloj E)
	*tbhi = 0xffu;
	*tblo = 0xffu;
	*crb = 0x10u;                                  // LOAD (continuo: RUNMODE=0)
	*crb = 0x11u;                                  // LOAD|START
	(void)*tblo;
}

/// Lee el registro de desplazamiento del **pad CD32** del puerto 2. Devuelve 9 bits: `bit i` = nivel
/// en el i-ésimo pulso de reloj (1 = alto). Reloj = CIA-A PRA bit 7 como **salida** (el pin de fire
/// del puerto 2); dato = `POTINP` bit 14; `POTGO` arranca el puerto. Ref: `lowlevel.library` /
/// foro exxos; modelo en `WinUAE-DBG/inputdevice.cpp:4000-4057` (reloj por flanco de bajada de PRA
/// bit 7, dato en `p9dat = 0x4000`). Activo a 0.
eng::u16 read_cd32_shift_port2() {
	using eng::amiga::detail::ciaa_reg;
	using eng::amiga::detail::custom_base;
	volatile eng::u8* const pra = ciaa_reg(0x00u);
	volatile eng::u8* const ddra = ciaa_reg(0x02u); // DDRA = 0xBFE201 (índice = nº de registro)
	constexpr eng::u8 kClock = 0x80u; // PRA bit 7 = fire/reloj del puerto 2

	*ddra = static_cast<eng::u8>(*ddra | kClock);  // reloj como salida
	*pra = static_cast<eng::u8>(*pra & ~kClock);   // reloj a bajo
	custom_base[0x034 / 2] = 0x6f00u;              // POTGO: arranca el puerto 2
	for (volatile eng::u16 i = 0u; i < 64u; ++i) {} // asentar las capacidades del pot

	eng::u16 bits = 0u;
	for (eng::u8 i = 0u; i < 9u; ++i) {
		for (volatile eng::u16 d = 0u; d < 8u; ++d) {} // >= ~1.4 us (el CIA va al E clock)
		const eng::u16 potinp = custom_base[0x016 / 2];
		*pra = static_cast<eng::u8>(*pra | kClock);  // reloj a alto
		*pra = static_cast<eng::u8>(*pra & ~kClock); // reloj a bajo (pulso)
		if ((potinp & 0x4000u) != 0u) {
			bits = static_cast<eng::u16>(bits | (1u << i));
		}
	}
	*ddra = static_cast<eng::u8>(*ddra & ~kClock); // reloj de vuelta a entrada
	custom_base[0x034 / 2] = 0xffffu;              // POTGO a reposo
	return bits;
}

/// ¿El stream leído parece un **pad CD32**? Firma: tras los 7 botones, bit 7 = 1 y bit 8 = 0 (un
/// joystick normal no la produce). Ver `MINI_OS_INPUT.md` §6.
[[nodiscard]] bool cd32_present(eng::u16 bits) noexcept {
	return ((bits & 0x80u) != 0u) && ((bits & 0x100u) == 0u);
}

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

/// Cuerpo del **latido** del mini-SO: avanza el frame, señaliza el VBlank, pollea los productores
/// habilitados (`input_enable`) y postea los timers vencidos. Lo llama `tick()` (polling desde el
/// bucle) o la **IRQ de VBlank** que instala `os::init`.
void tick_body() {
	using eng::amiga::detail::ciaa_reg;
	using eng::amiga::detail::custom_base;

	// Arranca el reloj de µs (CIA-B Timer B) la primera vez: así los timers µs ven una base de
	// tiempo monotónica real, no `ticks_now == 0` (TIME-003).
	if (!g_ciab_ready) {
		start_ciab_clock();
		(void)ciab_ticks_now(); // primera lectura de referencia
	}

	++g_frame;
	g_vblank.signal(g_frame);
	g_port.signal(SigVBlank);

	// JOY0DAT ($DFF00A) = ratón (puerto 1); JOY1DAT ($DFF00C) = joystick (puerto 2). Fuegos en
	// CIA-A PRA ($BFE001): bit 6 = FIRE0, bit 7 = FIRE1; 0 = pulsado.
	Msg m {};
	if ((g_input_mask & InputMouse) != 0u) {
		const eng::u16 joy0 = custom_base[0x00a / 2];
		const eng::u8 pra = *ciaa_reg(0x00u);
		const eng::u8 mx = static_cast<eng::u8>(joy0 & 0xffu);
		const eng::u8 my = static_cast<eng::u8>((joy0 >> 8u) & 0xffu);
		const eng::u8 mbtn = ((pra & 0x40u) == 0u) ? 1u : 0u;
		if (g_mouse.update(mx, my, mbtn, g_frame, m)) {
			(void)g_port.post(m);
		}
	}

	// **Puerto 2**: pad CD32 (protocolo serie por reloj/POTGO) o joystick digital
	// (`JOY1DAT`). Si el pad CD32 está habilitado, se lee el stream y, si su firma
	// corresponde a un pad, se emite `Gamepad`; si **no** hay pad, se cae al joystick
	// (así el mismo binario sirve para ambas verificaciones sin recompilar).
	const bool try_cd32 = (g_input_mask & InputCd32Pad) != 0u;
	bool cd32_handled = false;
	if (try_cd32) {
		const eng::u16 shift = read_cd32_shift_port2();
		if (cd32_present(shift)) {
			const eng::u16 btn =
			    eng::os::cd32_mask_from_shift(static_cast<eng::u8>(shift & 0x7fu));
			if (g_pad.update(btn, g_frame, m)) {
				(void)g_port.post(m);
			}
			cd32_handled = true;
		}
	}
	if (!cd32_handled && (g_input_mask & InputJoystick) != 0u) {
		const eng::u16 joy1 = custom_base[0x00c / 2];
		const eng::u8 pra = *ciaa_reg(0x00u);
		const eng::u8 dirs = eng::amiga::decode_joystick(joy1);
		const eng::u8 fire = ((pra & 0x80u) == 0u) ? 1u : 0u;
		if (g_joy.update(dirs, fire, g_frame, m)) {
			(void)g_port.post(m);
		}
	}

	(void)g_timers.poll_and_post(g_port, g_frame, ciab_ticks_now());

	// Tarea de frame (p. ej. música): en el mismo contexto que el tick (IRQ si va por IRQ).
	if (g_frame_task != nullptr) {
		using eng::amiga::detail::vpos_long;
		g_frame_task(g_frame_task_user,
			     static_cast<eng::u16>((*vpos_long & 0x1ff00u) >> 8));
	}
}

void tick() { tick_body(); }

/// **Servicio de timers de alta frecuencia** (TIME-004): postea los timers vencidos usando el
/// reloj de µs (CIA-B) sin esperar al VBlank. Pensado para llamarse desde un bucle de espera
/// activa (p. ej. la E/S o un plazo corto) o, en el futuro, desde un **one-shot de CIA-B** armado
/// con `g_timers.next_micro_deadline()`. En el tick normal ya se llama una vez por VBlank; esta
/// entrada permite una segunda llamada sub-frame para los timers µs. Devuelve cuántos posteó.
eng::u16 service_timers() {
	return g_timers.poll_and_post(g_port, g_frame, ciab_ticks_now());
}

/// Tarea de frame opcional (ver `os.hpp`): se ejecuta en cada tick, tras entrada/timers.
void set_frame_task(void (*cb)(void*, eng::u16), void* user) {
	g_frame_task = cb;
	g_frame_task_user = user;
}

/// Habilita los dispositivos de `mask` (los demás no se pollean). El teclado instala su IRQ de
/// CIA-A; el pad CD32 cambia el puerto 2 a protocolo serie.
void input_enable(eng::u8 mask) {
	g_input_mask = mask;
	if ((mask & InputKeyboard) != 0u) {
		enable_keyboard();
	}
}

/// Añade/actualiza un **timer de usuario**: `MsgType::Timer` con `id` cada `frames` VBlanks.
/// `frames == 0` lo elimina. Es un timer periódico de frames con catch-up `Coalesce` (un mensaje
/// por_frame pendiente consolidado; `expirations` lleva los periodos condensados). Reemplaza
/// cualquier timer previo con el mismo `id`.
void add_timer(eng::u16 id, eng::u16 frames) {
	if (frames == 0u) {
		(void)g_timers.stop_by_id(id);
		return;
	}
	(void)g_timers.stop_by_id(id); // reemplaza (TIME-007: identidad explícita)
	(void)g_timers.start(id, frames, TimerUnit::Frames, true, g_frame, ciab_ticks_now());
}

/// Hook de VBlank del mini-SO (lo registra `os::init` en el `Engine`): ejecuta el latido.
void vblank_hook(void*) { tick_body(); }

/// Activa la lectura del **pad CD32** en el puerto 2. Añade `InputCd32Pad` al mask de entrada
/// **sin** quitar `InputJoystick`: el `tick` prefiere el pad si su firma está presente y, si no,
/// cae al joystick (auto-detección). Ver `MINI_OS_INPUT.md` §6; la inyección del pad en WinUAE se
/// cubre con `run-demo.sh --cd32`.
void enable_cd32_pad() {
	g_input_mask = static_cast<eng::u8>(g_input_mask | InputCd32Pad);
}

eng::u32 wait(eng::u32 mask) {
	// **Modelo de mensajes**: si el latido corre por IRQ de VBlank (señal Exec armada al
	// instalar el servicio), el puerto se señaliza desde la IRQ y esta tarea **duerme en
	// `Wait()`** hasta el próximo VBlank (CPU en el `STOP` de Exec si no hay otra tarea
	// lista); sin señal armada (modo polling) se coopera con `tick()`. En ambos casos se
	// devuelven los bits consumidos. En Workbench este `wait` se sustituye por el mismo
	// `Wait(señales Exec)` + volcado `Exec -> Msg` (ver `ROADMAP_WORKBENCH.md`, W5).
	if (mask == 0u) {
		return 0u;
	}
	if (eng::amiga::detail::vblank_signal_armed()) {
		static unsigned long seen_seq = 0u;
		for (;;) {
			const eng::u32 got = g_port.pending(mask);
			if (got != 0u) {
				return g_port.take_signals(got);
			}
			(void)eng::amiga::detail::vblank_signal_wait_next(seen_seq);
		}
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
