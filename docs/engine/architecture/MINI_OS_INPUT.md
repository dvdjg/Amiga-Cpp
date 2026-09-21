# Mini-SO: entrada de hardware → mensajes (`eng::os` input)

Cómo los **registros de hardware del Amiga** se convierten en `Msg` de entrada en el puerto del
mini-SO ([`MINI_OS_MESSAGE_LOOP.md`](MINI_OS_MESSAGE_LOOP.md)), y cómo los consume la aplicación.
La regla de arquitectura es que la app **nunca lee `$DFF00A` ni `$BFE001`**: solo drena la cola.

Las fuentes son el **AHRM 3.ª** (cap. «Interface Hardware» para CIA, «Reading Digital Joystick
Controllers» para `JOYnDAT`, «Game Port Interface to Fire Buttons» para el fuego, y el protocolo
serie de teclado), el manual *input.device* (`docs/reference/amiga/hardware/input-device-rkm.md`)
y la decodificación ya portada en `engine/include/eng/platform/input_poll.hpp`.

## 1. Convenciones de puerto

```text
  Puerto 1 (derecha)   → ratón          → JOY0DAT ($DFF00A) + CIA-A PRA bit 6
  Puerto 2 (izquierda) → joystick / pad → JOY1DAT ($DFF00C) + CIA-A PRA bit 7
```

Ratón y joystick **no comparten puerto** a la vez: el puerto 1 es del ratón, el 2 del joystick o
del pad CD32. Los botones de fuego llegan al CIA-A (activos a **0**); las direcciones llegan a los
contadores de Denise (`JOYnDAT`), no al CIA.

## 2. Mapa de registros

| Uso | Dirección | Notas |
|---|---|---|
| Contadores ratón / joy | `$DFF00A` JOY0DAT, `$DFF00C` JOY1DAT | 16 bits: Y en 15..8, X en 7..0 (cuadratura) |
| Botones pot / CD32 | `$DFF016` POTINP, `$DFF034` POTGO | arranque/parada de contadores; datos serie del pad |
| Fuego joysticks | `$BFE001` CIA-A PRA | bit 6 = FIRE0 (puerto 1), bit 7 = FIRE1 (puerto 2); 0 = pulsado |
| Teclado | CIA-A `SDR` ($BFEC01) + `CRA` ($BFEE01) + `ICR` ($BFED01) | serie; IRQ cuando hay scancode |
| Máscaras IRQ | `$DFF01C` INTENA, `$DFF01E` INTREQ | CIA-A en nivel 2 (PORTS) |

## 3. Mensajes de entrada

```cpp
// eng/os/message.hpp (fragmento)
enum class MsgType : eng::u8 {
	KeyDown, KeyUp,
	MouseMove, MouseButton,
	Joystick,   ///< joystick digital (1 botón)
	Gamepad,    ///< CD32 / multi-botón
	// …
};

union MsgPayload {
	struct { eng::u16 code; eng::u16 qual; } key;        ///< scancode Amiga + modificadores
	struct { eng::s16 x, y; eng::s8 dx, dy; eng::u8 buttons; } mouse;
	struct { eng::u8 port; eng::u8 dirs; eng::u8 fire; } joy;
	struct { eng::u8 port; eng::u16 buttons; } pad;      ///< bitmask CD32
	// …
};
```

- **Flancos** (`KeyDown`/`KeyUp`, `MouseButton`): se emiten **solo al cambiar**, nunca por frame.
- **Movimiento** (`MouseMove`): delta + posición absoluta, con **coalescing** (gana el último).
- **Estado** (`Joystick`/`Gamepad`): se emite solo si cambió; opcionalmente *latched* (último
  estado), porque al juego le importa el estado actual, no el histórico.

El **estado de nivel** (teclas/botones mantenidos) no va por mensajes: lo mantiene
`eng::input::InputAggregator` (`engine/include/eng/input/input.hpp`), que el productor actualiza en
el mismo punto. Así conviven los dos modelos sin duplicar: mensajes para flancos, snapshot para
nivel. La decodificación pura de `JOYnDAT` y del fuego ya vive en
`eng/platform/input_poll.hpp` (`decode_joy`) y se **reutiliza** aquí.

## 4. Ratón (puerto 1)

El ratón es **relativo**: se comparan los contadores de 8 bits X/Y entre lecturas; el delta es la
diferencia en complemento a dos. Los botones: izquierdo en CIA-A PRA bit 6; derecho y medio por
`POTINP`. La posición absoluta se mantiene en el productor (clampada a la pantalla).

```cpp
struct MouseState {
	eng::u8 prev_x = 0, prev_y = 0;
	eng::u8 buttons = 0;              // bit0 = L, bit1 = R, bit2 = M
	eng::s16 abs_x = 160, abs_y = 128;
	bool inited = false;
};

/// Diferencia de un contador de 8 bits (cuadratura): el complemento a dos da el delta.
[[nodiscard]] inline eng::s8 delta8(eng::u8 now, eng::u8 prev) noexcept {
	return static_cast<eng::s8>(now - prev);
}

inline void poll_mouse(eng::u32 stamp) {
	const eng::u16 dat = joy0dat();
	const eng::u8 x = static_cast<eng::u8>(dat & 0xffu);
	const eng::u8 y = static_cast<eng::u8>((dat >> 8u) & 0xffu);
	if (!g_mouse.inited) { g_mouse.prev_x = x; g_mouse.prev_y = y; g_mouse.inited = true; return; }

	const eng::s8 dx = delta8(x, g_mouse.prev_x);
	const eng::s8 dy = delta8(y, g_mouse.prev_y);
	g_mouse.prev_x = x; g_mouse.prev_y = y;

	eng::u8 btn = 0;
	if ((ciaa_pra() & (1u << 6u)) == 0u) { btn |= 1u; }      // izquierdo (activo a 0)
	const eng::u16 pot = potinp();
	if ((pot & (1u << 10u)) == 0u) { btn |= 2u; }            // derecho
	if ((pot & (1u << 8u)) == 0u)  { btn |= 4u; }            // medio (si existe)

	if (dx != 0 || dy != 0) {
		clamp_abs(dx, dy);                                // abs_x/abs_y a 0..319 / 0..255
		Msg m {}; m.type = MsgType::MouseMove; m.time_stamp = stamp;
		m.payload.mouse = { g_mouse.abs_x, g_mouse.abs_y, dx, dy, btn };
		g_port.post(m);                                   // coalescido: gana el último
	}
	if (btn != g_mouse.buttons) {
		g_mouse.buttons = btn;
		Msg m {}; m.type = MsgType::MouseButton; m.time_stamp = stamp;
		m.payload.mouse = { g_mouse.abs_x, g_mouse.abs_y, 0, 0, btn };
		g_port.post(m);
	}
}
```

## 5. Joystick digital (puerto 2)

En `JOYnDAT` los bits de cuadratura se interpretan como direcciones. La decodificación canónica
(1 = pulsado) es la ya portada en `input_poll.hpp`: derecha = bit 1, izquierda = bit 9, arriba =
bit 9 XOR bit 8, abajo = bit 1 XOR bit 0. El fuego llega por CIA-A PRA bit 7.

```cpp
inline void poll_joystick_port2(eng::u32 stamp) {
	const eng::u8 dirs = decode_joy(joy1dat());                 // bits up/down/left/right
	const eng::u8 fire = ((ciaa_pra() & (1u << 7u)) == 0u) ? 1u : 0u;
	if (dirs == g_joy2.dirs && fire == g_joy2.fire) { return; } // solo al cambiar
	g_joy2.dirs = dirs; g_joy2.fire = fire;
	Msg m {}; m.type = MsgType::Joystick; m.time_stamp = stamp;
	m.payload.joy = { /*port*/ 2u, dirs, fire };
	g_port.post(m);
}
```

## 6. Gamepad CD32 (puerto 2)

El pad CD32 reutiliza el puerto de joystick y **serializa** los botones extra por la línea de pot:
se pulsa `POTGO` y se lee `POTINP` en cada paso. Las direcciones siguen leyéndose de `JOY1DAT`
como un joystick digital; solo los botones de color/Play/hombros usan el protocolo serie.

```cpp
/// Bitmask estable para la app (independiente del orden del stream del pad).
enum Cd32Btn : eng::u16 {
	CdBlue = 1u << 0, CdRed = 1u << 1, CdYellow = 1u << 2, CdGreen = 1u << 3,
	CdForward = 1u << 4, CdReverse = 1u << 5, CdPlay = 1u << 6,
};

/// Lectura de botones CD32 en el puerto 2. Llamar 1× por frame (VBlank).
[[nodiscard]] eng::u16 read_cd32_buttons_port2();

inline void poll_cd32(eng::u32 stamp) {
	const eng::u16 btn = read_cd32_buttons_port2();
	const eng::u8 dirs = decode_joy(joy1dat());
	if (btn == g_pad2.buttons && dirs == g_pad2.dirs) { return; }
	g_pad2 = { dirs, btn };
	Msg m {}; m.type = MsgType::Gamepad; m.time_stamp = stamp;
	m.payload.pad = { /*port*/ 2u, btn };
	g_port.post(m);
}
```

> El **orden exacto de bits** y el patrón de `POTGO` varían ligeramente entre fuentes; se calibra
> una vez contra un pad real en WinUAE. Lo que fija la arquitectura es el mecanismo: **una lectura
> por frame → un `MsgType::Gamepad` solo si cambió el estado**, con los botones ya remapeados a un
> bitmask estable. El estado de nivel (direcciones + botones) alimenta `PadState`
> (`eng/input/input.hpp`).

## 7. Teclado (CIA-A, serie)

El teclado envía scancodes por el puerto serie de CIA-A y genera IRQ (nivel 2, PORTS). El
manejador lee `SDR`, hace el *handshake* en `CRA`, corrige el **bit-reverse** del scancode y emite
`KeyDown`/`KeyUp` según el bit de "up". El teclado **no** se pollea en el frame loop.

```cpp
/// Servidor de la IRQ CIA-A (PORTS) cuando llega un scancode.
inline void kbd_isr() {
	const eng::u8 raw = ciaa_sdr();
	ciaa_ack();                                       // handshake en CRA (SPMODE)
	const bool up = (raw & 0x80u) != 0u;
	const eng::u8 code = reverse_bits7(static_cast<eng::u8>(raw & 0x7fu)); // bit-reverse CIA

	update_qualifiers(code, up);                      // shift/ctrl/amiga → g_qualifiers
	Msg m {}; m.type = up ? MsgType::KeyUp : MsgType::KeyDown;
	m.time_stamp = g_frame;
	m.payload.key = { code, g_qualifiers };
	g_port.post(m);
}
```

El detalle que más falla al implementar desde cero es el **bit-reverse** del scancode y el
*handshake*; conviene compararlo con la tabla de scancodes Amiga estándar. Los modificadores
(Shift, Ctrl, Amiga) se siguen en `g_qualifiers` y viajan en `payload.key.qual`.

## 8. Cableado de los productores

```text
  CIA-A IRQ (PORTS) ─► kbd_isr ───────────────► Msg KeyDown/Up ──► SigInput
  VERTB IRQ ─────────► poll_mouse ────────────► Msg MouseMove/Button ──► SigInput
                   ├─► poll_joystick_port2 ───► Msg Joystick ──► SigInput
                   ├─► poll_cd32 (si pad) ────► Msg Gamepad ──► SigInput
                   └─► latch VBlank ──────────► SigVBlank
  app: take_vblank + drenar cola → bridge a UiEvent / gameplay
```

Ratón, joystick y pad se pollean **una vez por frame** desde la IRQ de VBlank (estable y suficiente
a 50 Hz); el teclado va por su propia IRQ. Si se detecta un pad CD32 en el puerto 2, se usa
`poll_cd32` **en lugar de** `poll_joystick_port2` (mismo puerto).

## 9. Puente a la UI

La UI no lee hardware: el puente `os::Msg` → `ui::UiEvent` traduce los mensajes de entrada
([`MINI_OS_MESSAGE_LOOP.md`](MINI_OS_MESSAGE_LOOP.md) §8) y el juego consume `Joystick`/`Gamepad`
directamente para la lógica. El contrato es: **un hecho, un sitio** — flancos por mensaje, nivel
por `InputAggregator`, evento intra-frame por `eng::util::Event`.

## 10. Referencias

- AHRM 3.ª edición: cap. «Interface Hardware» (CIA), «Reading Digital Joystick Controllers»
  (`JOYnDAT`), «Game Port Interface to Fire Buttons» (fuego), protocolo serie de teclado.
- `docs/reference/amiga/hardware/input-device-rkm.md` (input.device, contexto con Exec).
- `engine/include/eng/platform/input_poll.hpp` (`decode_joy`, sondeo por frame).
- `engine/include/eng/input/input.hpp` (`InputAggregator`, `PadState`).
