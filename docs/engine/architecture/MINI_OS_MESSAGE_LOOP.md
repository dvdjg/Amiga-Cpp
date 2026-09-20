# Mini-SO de mensajes: bucle reactivo sobre el engine

Diseño de una **capa de abstracción de sistema en miniatura** (`eng::os`) para los juegos que
toman la máquina (sin Workbench): un **puerto de mensajes** al que los productores (IRQ de
VBlank, CIA/potgo para teclado y ratón, disco, timers) encolan eventos, y un **bucle de
aplicación reactivo** que espera señales en lugar de sondear hardware en espera activa. Sobre
ella se apoya una capa de interfaz (`eng::ui`) que solo consume eventos y nunca lee puertos.

El modelo es el clásico de Exec (puertos, mensajes y señales) reimplementado en miniatura: no
hay multitarea preemptiva ni SO real, pero la **API de la aplicación es la misma** que tendría
sobre un sistema con *message loop*.

## 1. Motivación y alcance

En Amiga "a pelo" el control lo toma el programa: `update`/`render` corren atados a la IRQ de
VBlank y todo lo demás (input, disco, timers) suele mezclarse con sondeo dentro del frame. Eso
produce dos problemas de arquitectura: (1) lógica de juego acoplada a registros de hardware, y
(2) esperas activas (`while (!listo) {}`) que queman CPU.

`eng::os` resuelve ambos con un único patrón: **productores que encolan mensajes** y un
**consumidor que espera señales**. Ámbito:

- Entrada (teclado, ratón, joystick) como **eventos de flanco** (down/up/move), no como sondeo.
- VBlank como **tick de sistema** (un mensaje por frame), base del ritmo de juego y de los
  timers.
- E/S de disco **asíncrona**: la petición vuelve al momento y el resultado llega por mensaje.
- Timers de usuario y mensajes de aplicación (`User`) para desacoplar subsistemas.
- **No** es un planificador de procesos, ni gestiona memoria, ni sustituye al engine gráfico:
  es la capa de eventos y servicios que hoy no existe.

## 2. Relación con lo que ya existe (no duplicar)

El engine ya tiene piezas que deben **componerse** con esta capa, no reemplazarse:

| Pieza existente | Qué aporta hoy | Papel con `eng::os` |
|---|---|---|
| `eng::Engine` (`engine/include/eng/engine.hpp`) | Bucle de frames; modo **interrupt-driven** con `set_vblank_service` (el juego late en la IRQ) y modo polling | El bucle de mensajes **no** crea un segundo bucle: se integra como un `Game` (§7). El VBlank que ya dispara `update`/`render` es el mismo que emite `MsgType::VBlank`. |
| `eng::task::BackgroundQueue` (`eng/task/background.hpp`) | Tareas **cooperativas** drenadas en el hueco de VBlank (decodificar, precargar, simular) | El trabajo pesado de una E/S de disco se hace como tarea de fondo; el mensaje `FileDone` solo **avisa** de que hay datos, no los decodifica en la ISR. |
| `eng::input::InputAggregator` (`eng/input/input.hpp`) | Estado de entrada **por frame** (nivel) | Se mantiene como **instantánea de nivel** (teclas/botones mantenidos); los mensajes cubren los **flancos**. Ver §6.1. |
| `eng::util::Event<...>` (`eng/core/util/event.hpp`) | Emisor de eventos en el **mismo hilo** (observador) | Para eventos internos del juego dentro del frame; los mensajes son la vía **cruzada IRQ↔hilo principal** (el `Event` no es IRQ-safe). |
| `eng::debug::RunStatus` (`eng/debug/run_status.hpp`) | Estado READY/FAILED para el runner | Se conserva; no es un canal de mensajes. |

La regla es: **un hecho, un sitio**. La entrada de flanco va por mensajes; la de nivel, por el
snapshot; el evento intra-frame, por `Event`; el trabajo diferido, por `BackgroundQueue`.

## 3. Modelo

```text
  IRQs / DMA / Copper / trackdisk / CIA
            │
            ▼
   ┌──────────────────┐   push_isr(Msg)    ┌─────────────────────────┐
   │   Productores    │ ─────────────────► │   MsgPort               │
   │ (ISR, no bloquea)│                    │  ring SPSC + signalled  │
   └──────────────────┘                    └───────────┬─────────────┘
                                                       │ wait(signals)
                                                       ▼
                                            ┌─────────────────────────┐
                                            │   Bucle de aplicación   │
                                            │  pop → dispatch por tipo │
                                            └───────────┬─────────────┘
                                                        │
                     ┌──────────────┬───────────────┬───┴──────────┬──────────────┐
                     ▼              ▼               ▼              ▼              ▼
                  Input          VBlank          FileDone       Timer          User
                (→ UiEvent)   (frame/tick)    (→ BackgroundTask) (?)
```

Nada de `while (!boton) {}`. Quien necesita entrada se suscribe al puerto; quien necesita
frame espera `SigVBlank` (o un timer derivado). El productor nunca espera: encola y marca señal.

## 4. Mensajes

Un mensaje es un **valor pequeño y copiable** (sin punteros propietarios), pensado para caber en
una entrada de la cola y copiarse en la ISR sin coste apreciable.

```cpp
#pragma once

#include <eng/core/types.hpp>

namespace eng::os {

/// Tipo de mensaje. Los valores se agrupan por rango (entrada / tiempo / E-S / app) para
/// que un filtro de prioridad sea una comparación de rango.
enum class MsgType : eng::u8 {
	None = 0,

	// Entrada (flancos)
	KeyDown, KeyUp,
	MouseMove, MouseButton,
	Joystick,

	// Tiempo
	VBlank,
	Timer,

	// Sistema / E-S
	FileDone, FileError,
	DiskChange,

	// Aplicación
	User,
	Quit,
};

/// Máscara de señales (modelo Exec): cada subsistema tiene su bit. La cola lleva los
/// mensajes; `signalled` es el aviso barato de "hay algo de este tipo".
enum Signal : eng::u32 {
	SigNone   = 0u,
	SigVBlank = 1u << 0,
	SigInput  = 1u << 1,
	SigFile   = 1u << 2,
	SigTimer  = 1u << 3,
	SigUser   = 1u << 4,
	SigQuit   = 1u << 5,
	SigAll    = 0x3fu,
};

/// Payload de un mensaje. La unión no tiene constructores: se escribe el campo del tipo
/// que toca. Todos los miembros son triviales (copiables en la ISR).
union MsgPayload {
	struct { eng::u16 code; eng::u16 qual; } key;      ///< scancode Amiga + modificadores
	struct { eng::s16 x, y; eng::s8 dx, dy; eng::u8 buttons; } mouse;
	struct { eng::u8 port; eng::u16 state; } joy;      ///< direcciones + botones
	struct { eng::u16 id; } timer;
	struct { eng::u16 handle; eng::s32 result; } file; ///< bytes o error
	struct { eng::u32 code; eng::u32 a; eng::u32 b; } user;

	constexpr MsgPayload() noexcept : user {} {}
};

struct Msg {
	MsgType type = MsgType::None;
	eng::u16 flags = 0u;        ///< flags por tipo (p. ej. coalescido/atrasado)
	eng::u32 time_stamp = 0u;   ///< frames o ticks desde el arranque
	MsgPayload payload {};
};

} // namespace eng::os
```

Notas de diseño:

- **`MsgType` como `enum class`** evita colisiones entre tipos y da *switch* exhaustivo
  chequeable; el rango agrupa por subsistema para priorizar (input antes que E/S).
- **`time_stamp`** lo rellena el productor con el contador de frames del sistema, de modo que
  el consumidor pueda medir latencia sin leer el reloj.
- El payload es una **unión trivial**: nada de constructores virtuales ni `std::variant` (que
  arrastraría código y no es IRQ-apto). Si un día se necesita extensibilidad, la alternativa es
  payload por handle a un pool fijo, no un `variant` en la cola.

## 5. Puerto y cola IRQ-safe

La primitiva es un **anillo de un productor y un consumidor (SPSC)**: el productor corre en la
ISR y el consumidor en el hilo principal. No hay contención porque **cada índice lo escribe un
solo lado**; el resto es disciplina de memoria (§12).

```cpp
#pragma once

#include <eng/core/types.hpp>
#include <eng/os/message.hpp>

namespace eng::os {

/// Anillo SPSC IRQ-safe de capacidad fija `N` (N potencia de dos para indexar con máscara).
/// El productor escribe `head`; el consumidor escribe `tail`; ambos leen el otro índice.
template <eng::u16 N>
class MsgQueue {
	static_assert((N & (N - 1u)) == 0u, "N debe ser potencia de dos");

public:
	/// Encola desde la ISR (o con interrupciones deshabilitadas). `false` si está llena:
	/// el mensaje se descarta y el contador de overflow lo registra (nunca bloquea).
	bool push_isr(const Msg& m) {
		const eng::u16 next = static_cast<eng::u16>((m_head + 1u) & (N - 1u));
		if (next == m_tail) {          // llena
			++m_overflows;
			return false;
		}
		m_buf[m_head] = m;
		m_head = next;                 // publicación del mensaje
		return true;
	}

	/// Saca el siguiente mensaje (hilo principal). `false` si está vacía.
	bool pop(Msg& out) {
		if (m_tail == m_head) {
			return false;
		}
		out = m_buf[m_tail];
		m_tail = static_cast<eng::u16>((m_tail + 1u) & (N - 1u));
		return true;
	}

	[[nodiscard]] bool empty() const { return m_head == m_tail; }
	[[nodiscard]] eng::u16 overflows() const { return m_overflows; }

private:
	Msg m_buf[N] {};
	volatile eng::u16 m_head = 0u;  ///< escribe el productor (ISR)
	volatile eng::u16 m_tail = 0u;  ///< escribe el consumidor
	volatile eng::u16 m_overflows = 0u;
};

/// Puerto de mensajes: la cola más la máscara de señales. `signal` es lo único que toca la
/// ISR además de encolar; `wait` es el único punto donde el hilo principal se duerme.
template <eng::u16 N = 64u>
struct MsgPort {
	MsgQueue<N> queue {};
	volatile eng::u32 signalled = 0u;

	void signal(eng::u32 mask) { signalled = signalled | mask; }

	/// Consume del campo `signalled` los bits pedidos que estén puestos (devuelve 0 si
	/// ninguno). No espera.
	eng::u32 take_signals(eng::u32 mask) {
		const eng::u32 s = signalled;
		const eng::u32 got = s & mask;
		if (got != 0u) {
			signalled = s & ~mask;
		}
		return got;
	}

	/// Espera a que haya señales de `mask`. El backend decide **cómo** esperar sin quemar
	/// CPU (`os_wait_interrupt`): en máquina tomada, `stop` con el vector de IRQ armado; en
	/// host de test, un `yield`. Devuelve los bits que se consumieron.
	eng::u32 wait(eng::u32 mask);
};

} // namespace eng::os
```

Decisiones:

- **Anillo SPSC, no cola genérica**: con un productor y un consumidor por puerto no hacen falta
  cerrojos. Si un subsistema necesita varios productores (p. ej. dos ISR de distinto nivel), se
  le da **su propio puerto** (§12), en lugar de meter un *lock* en el camino de la ISR.
- **`signal` es OR-eado**: barato desde la ISR (una escritura) y agrupa varios mensajes del
  mismo tipo bajo una señal. El consumidor **drena la cola entera** al despertar, así que no se
  pierde un mensaje por haber llegado dos entre señales.
- **Overflow contado, no bloqueante**: si la ISR llena la cola, descarta y suma
  `m_overflows`; la aplicación puede vigilarlo (telemetría) pero nunca se cuelga dentro de la
  ISR.

## 6. Productores

Cada productor vive en el backend (código de máquina) y solo hace dos cosas: leer el hardware y
`push_isr` + `signal`. Nunca decide reglas de juego.

### 6.1 VBlank

```cpp
void amiga_vblank_isr() {
	Msg m {};
	m.type = MsgType::VBlank;
	m.time_stamp = ++g_frame_counter;      // lo lee os::frame_count()
	g_port.queue.push_isr(m);
	g_port.signal(SigVBlank);
}
```

Además de emitir el mensaje, la IRQ actualiza el **snapshot de nivel** de entrada
(`eng::input::InputAggregator`) para el frame: teclas y botones mantenidos son un *estado*, no
un evento. Así conviven los dos modelos sin duplicar: mensajes para flancos, snapshot para
nivel.

### 6.2 Teclado, ratón y joystick

La fuente puede ser una **IRQ de CIA** (timer/flanco) o un **soft-int** lanzado tras el sondeo
de `potgo`/`joyport`. En cualquier caso el manejador produce flancos:

```cpp
void input_isr() {
	const eng::u16 raw = read_key_matrix();       // hardware, una vez
	if (raw != g_last_raw) {
		const eng::u16 changed = static_cast<eng::u16>(raw ^ g_last_raw);
		for (eng::u8 sc = 0; sc < 8u; ++sc) {     // por byte de la matriz
			eng::u8 bits = static_cast<eng::u8>((changed >> (sc * 8u)) & 0xffu);
			while (bits != 0u) {
				Msg m {};
				m.type = ((raw >> (sc * 8u)) & 0xffu & bits) ? MsgType::KeyDown
									      : MsgType::KeyUp;
				m.payload.key.code = sc;      // + bit
				g_port.queue.push_isr(m);
				bits = static_cast<eng::u8>(bits & (bits - 1u)); // limpia el bit bajo
			}
		}
		g_last_raw = raw;
		g_port.signal(SigInput);
	}
}
```

El ratón produce `MouseMove` (deltas) con coalescing natural: varios movimientos entre frames
se pueden fundir en uno (`flags` con bit de coalescido) o conservarse; el consumidor decide.
El joystick produce `Joystick` con `port` y una palabra de estado.

### 6.3 Disco / E-S asíncrona

```cpp
struct FileReq {          // viva mientras el DMA la referencia
	eng::u16 handle;
	eng::u8* buf;
	eng::u32 len;
	eng::u32 offset;
	bool write;
};

void file_done_isr(eng::u16 handle, eng::s32 result) {
	Msg m {};
	m.type = (result >= 0) ? MsgType::FileDone : MsgType::FileError;
	m.payload.file = { handle, result };
	g_port.queue.push_isr(m);
	g_port.signal(SigFile);
}
```

La API de usuario vuelve al momento (`os::file_read_async(handle, buf, len, off)`); el resultado
llega por `FileDone`/`FileError`. El **decodificado** del bloque no se hace en la ISR: la
aplicación registra una tarea en `BackgroundQueue` al recibir el mensaje (§7), de modo que el
trabajo caro se reparte en los huecos de VBlank.

## 7. Bucle de aplicación

El bucle de mensajes **no compite** con `eng::Engine`: se implementa como un `Game` cuyo
`update` (que ya corre en la IRQ de VBlank o en el bucle de polling) drena el puerto. Así el
VBlank hace de tick de sistema y de latido de frame a la vez.

```cpp
template <class App>
struct MessagePumpGame {
	App app {};
	eng::os::MsgPort<64>* port = nullptr;

	void init(eng::amiga::MinimalBackend& backend, eng::GameContext& ctx) {
		port = &eng::os::system_port();
		eng::os::init(backend, ctx);   // IRQs, productores, timers
		app.on_start(ctx);
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& ctx) {
		// 1) Esperar señales (el VBlank ya ha despertado este tick; aquí se recogen
		//    también input/E-S pendientes sin bloquear).
		(void)port->wait(eng::os::SigVBlank | eng::os::SigInput | eng::os::SigFile |
				 eng::os::SigTimer | eng::os::SigUser | eng::os::SigQuit);

		// 2) Drenar la cola entera: puede haber varios mensajes por señal.
		eng::os::Msg m;
		while (port->queue.pop(m)) {
			switch (m.type) {
				case eng::os::MsgType::Quit:    app.on_quit(); break;
				case eng::os::MsgType::VBlank:  app.on_vblank(m.time_stamp); break;
				case eng::os::MsgType::KeyDown:
				case eng::os::MsgType::KeyUp:
				case eng::os::MsgType::MouseMove:
				case eng::os::MsgType::MouseButton:
				case eng::os::MsgType::Joystick: app.on_input(m); break;
				case eng::os::MsgType::FileDone:  app.on_file_done(m.payload.file); break;
				case eng::os::MsgType::FileError: app.on_file_error(m.payload.file); break;
				case eng::os::MsgType::Timer:     app.on_timer(m.payload.timer.id); break;
				case eng::os::MsgType::User:      app.on_user(m.payload.user); break;
				default: break;
			}
		}
		// 3) Lógica y pintado del frame se apoyan en `app` y en el snapshot de entrada.
		app.on_frame(backend, ctx);
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& ctx) {
		app.on_render(backend, ctx);
	}
};
```

El **bucle clásico** de aplicación (para un backend sin `Game`) es el mismo patrón sin el
`Engine`, y se documenta aquí solo como referencia conceptual:

```text
loop:
  sig = port.wait(SigVBlank | SigInput | SigFile | SigQuit)
  while port.queue.pop(msg): dispatch(msg)
  if sig & SigVBlank: frame()      // lógica + FramePlan + present
```

Patrón de juego recomendado:

1. En **VBlank**: `ui.dispatch(Tick)`, lógica de frame, construir `FramePlan`, pintar lo sucio
   y hacer `commit`/swap.
2. En **Input**: traducir el mensaje a `UiEvent` (§8) y `ui.dispatch`.
3. En **FileDone**: registrar la tarea de decodificado en `BackgroundQueue` (no bloquear).

## 8. Puente a la UI

La UI **no** lee el ratón. Solo consume `UiEvent` que fabrica el puente `os::Msg` → `UiEvent`:

```cpp
void on_input(eng::ui::UiContext& ui, const eng::os::Msg& msg) {
	eng::ui::UiEvent ev {};
	switch (msg.type) {
		case eng::os::MsgType::MouseMove:
			ev.kind = eng::ui::UiEvent::MouseMove;
			ev.x = msg.payload.mouse.x; ev.y = msg.payload.mouse.y;
			ev.buttons = msg.payload.mouse.buttons;
			break;
		case eng::os::MsgType::MouseButton:
			ev.kind = (msg.payload.mouse.buttons & 1u) ? eng::ui::UiEvent::MouseDown
								  : eng::ui::UiEvent::MouseUp;
			ev.x = msg.payload.mouse.x; ev.y = msg.payload.mouse.y;
			ev.buttons = msg.payload.mouse.buttons;
			break;
		case eng::os::MsgType::KeyDown:
			ev.kind = eng::ui::UiEvent::KeyDown;
			ev.key = msg.payload.key.code;
			ev.shift = (msg.payload.key.qual & kQualShift) != 0u;
			break;
		case eng::os::MsgType::KeyUp:
			ev.kind = eng::ui::UiEvent::KeyUp;
			ev.key = msg.payload.key.code;
			break;
		default: return;
	}
	ui.dispatch(ev);
}
```

Contrato de responsabilidades:

| Capa | Responsabilidad |
|---|---|
| Mini-SO (`eng::os`) | Hardware → `Msg` en cola; `wait(signals)`; snapshot de nivel |
| Puente (`eng::ui`) | `Msg` de entrada → `UiEvent`; actualiza el snapshot si hace falta |
| `UiContext` | Foco, widgets, regiones sucias, `paint(&FramePlan)` |
| Juego | Reacciona a VBlank / FileDone / Timer / User |

## 9. Servicios del mini-SO

```text
os::init(backend, ctx)        // IRQs, puerto de sistema, productores, timers
os::system_port()             // MsgPort de la aplicación

// Tiempo
os::frame_count()             // contador de VBlank
os::add_timer(id, frames)     // -> MsgType::Timer cada `frames`

// Entrada (habilitar dispositivos; la app NO lee hardware)
os::input_enable(mouse | kbd | joy)

// E-S asíncrona
os::FileHandle os::file_open(path)
os::file_read_async(h, buf, len, off)   // -> FileDone / FileError
os::file_write_async(h, buf, len, off)
os::file_close(h)

// Aplicación
os::post_user(code, a, b)     // desde cualquier sitio (ISR-safe)
os::request_quit()
```

Opcional: **varios puertos** (`input_port`, `io_port`) para drenar siempre la entrada antes que
la E/S, o un anillo de prioridad (rango de `MsgType`). La decisión se documenta en el roadmap.

## 10. VBlank como despertador universal

Emitir siempre un `VBlank` da un único ritmo de sistema (50/60 Hz) que sirve de frame, de tick
de UI (caret, *toasts*) y de base de timers. El juego no necesita `WaitTOF` a mano: espera
`SigVBlank`.

Si un frame se alarga, el productor puede **coalescer**: emitir un `VBlank` con
`flags |= kMsgLate` y un contador de frames perdidos en `payload.user.a`, en lugar de encolar
dos mensajes idénticos. El consumidor decide si pinta un frame o acumula lógica. La alternativa
(siempre un `VBlank` por retrazo) se descarta por ruido.

## 11. ¿Busy-wait eliminado?

| Espera | Antes | Con el mini-SO |
|---|---|---|
| Siguiente frame | `WaitTOF` / bucle de beam | `wait(SigVBlank)` |
| Tecla | sondeo en bucle | `KeyDown`/`KeyUp` en la cola |
| Disco | sondeo de estado | `FileDone` / `FileError` |
| UI modal | sondeo del ratón | los mismos mensajes; el modal solo filtra en `UiContext` |

En A500 "máquina tomada" la espera sigue dependiendo de IRQs: no hay multitarea preemptiva,
pero la **API de la aplicación es reactiva**, igual que un *message loop* de escritorio.

## 12. Notas de implementación (concurrencia y C++23)

Notas propias para que la implementación sea de muy baja sobrecarga y encaje con el engine:

- **SPSC sin cerrojos.** El anillo con un índice por lado no necesita `atomic`. En 68000 (sin
  memoria débil en el sentido de los SMP) basta `volatile` para impedir que el compilador cachee
  los índices; la exclusión real la da que **solo la ISR escribe `head`**. Si un puerto tuviera
  dos productores, se le da un puerto propio antes que meter un *lock* en la ISR.
- **Publicar en orden.** Escribir el mensaje **antes** de avanzar `head` (o `tail`), y no
  reordenar el payload tras el índice. En hosts con hilos reales, usar
  `eng::parallel` (`atomic` con `memory_order_release/acquire`) desde el principio: en m68k se
  degrada a no-op y no cuesta nada.
- **`stop` vs espera activa.** `os::MsgPort::wait` se implementa por backend. En Amiga, `stop`
  con el vector de la IRQ de VBlank armado es la opción correcta (la CPU no consume bus); el
  sondeo de `signalled` es el *fallback* si el vector no está listo. En host, `wait` puede ser
  un simple volcado de la cola (los tests no tienen ISR).
- **C++23 al servicio de la sobrecarga cero.** `enum class` + `switch` exhaustivo (el compilador
  avisa si olvidas un `MsgType`); `consteval`/`constexpr` para capacidades y máscaras;
  *designated initializers* para construir `Msg` legible y sin coste; `concept` (como
  `GameModule`) para que la app del mini-SO se valide en compilación; `[[likely]]` en el caso
  `VBlank`. Nada de `virtual`, `variant` ni excepciones en el camino de la ISR.
- **Reutilizar `BackgroundQueue`.** La E/S asíncrona **no** introduce un segundo motor de
  trabajo diferido: `FileDone` es la señal, y el decodificado se registra como tarea
  cooperativa. El mismo mensaje sirve de "hay datos", y la cola de fondo reparte el coste.
- **Reutilizar `InputAggregator`.** Mantener el estado de nivel en el snapshot y reservar los
  mensajes para flancos evita duplicar la semántica de "tecla mantenida" y hace que la lógica
  que ya lee `InputAggregator` siga funcionando sin cambios.
- **`Event` solo intra-frame.** `eng::util::Event` no es IRQ-safe (invoca callables del lado
  consumidor); usarlo para difusión dentro del frame, nunca desde la ISR.
- **Telemetría de la cola.** `overflows()` y los contadores de frames perdidos deben exponerse
  (como `IrqTelemetry`) para que el juego detecte saturación sin fallar en silencio.
- **Incrementar la integración futura.** Si algún día se corre bajo Exec real, este diseño mapea
  casi 1:1 a `Wait(sigmask)` + `GetMsg(port)` + `DoIO`, cambiando solo el backend.

## 13. Referencias

- Modelo de puertos/mensajes/señales: Amiga ROM Kernel Reference Manual *Libraries* (Exec:
  `CreatePort`, `PutMsg`, `Wait`, `Signal`, `DoIO`) y AHRM 3.ª edición cap. 7 (interrupciones)
  para el productor VBlank.
- Entrada de hardware (CIA/potgo/joyport): `docs/reference/amiga/` (técnicas de input) y
  `engine/include/eng/platform/input_poll.hpp`.
- E/S de disco (trackdisk / trackloader): `docs/engine/architecture/STREAMING_LOADER.md`.
- Tareas cooperativas: `docs/engine/architecture/BACKGROUND_TASKS.md`.
- Bucle del engine y servicios de VBlank/blit: `docs/engine/architecture/ENGINE_DESIGN.md` y
  `engine/include/eng/engine.hpp`.
