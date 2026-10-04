# Mini-SO: tiempo, timers de hardware y profiling (`eng::os` time)

Cómo el mini-SO da **tiempo medible** y **temporizadores programables** sin sondeo, y cómo se
conectan al puerto de mensajes ([`MINI_OS_MESSAGE_LOOP.md`](MINI_OS_MESSAGE_LOOP.md)). Fuentes:
`../amiga-bootcamp/01_hardware/common/cia_chips.md` y `video_timing.md`, y el servicio CIA ya
existente en `engine/src/platform/amiga/amiga.cpp`
(`install_timer_service`, Timer A continuo de CIA-A sobre el autovector de nivel 2).

## 1. Qué fuente usar para qué

| Fuente | Resolución | Uso |
|---|---|---|
| **VBlank** | 20 ms (PAL) / ~16,7 ms (NTSC) | lógica de frame, tick de UI, timers "en frames" |
| **CIA Timer A/B** | ~1,4 µs/tick (reloj E ≈ 0,7 MHz) | one-shot, timeouts de E/S, medida fina |
| **CIA TOD** | 1/50 s o 1/60 s (según pin) | reloj de pared, no profiling |
| **VPOSR / VHPOSR** | línea / posición en el frame | perfilar *dentro* del frame (presupuesto) |

Regla: **frames** para lo que el juego vive, **ticks CIA** para medir y para timeouts, **beam**
para saber cuánto queda del frame.

## 2. Mapa de registros CIA

```text
  CIA-A  base $BFE001   TALO $BFE401  TAHI $BFE501  ICR $BFED01  CRA $BFEE01
  CIA-B  base $BFD000   TALO $BFD400  TAHI $BFD500  TBLO $BFD600  TBHI $BFD700
                        ICR  $BFDD00  CRA  $BFDE00  CRB  $BFDF00
  Reloj E:  709379 Hz (PAL)  /  715909 Hz (NTSC)   →  ~1,4 µs por tick
```

CIA-A Timer A ya lo usa el engine para el servicio de fondo (tareas cooperativas); el teclado
comparte su **mismo autovector de nivel 2 (PORTS)**, así que el manejador de esa IRQ debe mirar
los bits de `ICR` (TA y SP) y atender a ambos. Para un reloj libre de µs conviene **CIA-B**
(Timer A o B), que no colisiona con el servicio de fondo ni con el teclado.

## 3. Reloj de ticks (µs) — `TickClock`

Contador de 32 bits monotónico: un timer CIA en modo **continuo** (rebosa cada 65536 ticks ≈ 92 ms)
más un contador de reboses incrementado en la IRQ. La lectura es coherente leyendo `hi`, luego el
timer, luego `hi` otra vez.

```cpp
struct TickClock {
	volatile eng::u32 hi_overflows = 0u; ///< lo incrementa la IRQ del timer (CIA-B)

	void start_continuous() {
		// Timer A de CIA-B, continuo, reloj E (INMODE=0). Orden: parar, cargar, LOAD, START.
		cra_b() = 0x00u;
		talo_b() = 0xffu; tahi_b() = 0xffu;   // periodo máximo (≈92 ms por rebose)
		icr_b() = 0x81u;                      // SETCLR | TA: enmascarar IRQ del timer A
		cra_b() = 0x10u;                      // LOAD
		cra_b() = 0x11u;                      // LOAD|START (continuo, RUNMODE=0)
	}

	/// Ticks de ~1,4 µs desde `start_continuous` (monotónico).
	[[nodiscard]] eng::u32 now() const {
		eng::u32 hi, lo;
		do {
			hi = hi_overflows;
			lo = static_cast<eng::u16>(tahi_b()) << 8u | talo_b();
		} while (hi != hi_overflows);
		return hi * 65536u + (65535u - lo);   // el timer cuenta hacia abajo
	}
};

[[nodiscard]] inline eng::u32 ticks_now();
[[nodiscard]] inline eng::u32 ticks_to_us(eng::u32 t);   // t * 1e6 / kCiaHz
[[nodiscard]] inline eng::u32 us_to_ticks(eng::u32 us);  // us * kCiaHz / 1e6
```

## 4. Medir tramos — `ScopedTimer` y beam

```cpp
struct ScopedTimer {
	eng::u32 t0;
	ScopedTimer() : t0(ticks_now()) {}
	[[nodiscard]] eng::u32 elapsed_ticks() const { return ticks_now() - t0; }
	[[nodiscard]] eng::u32 elapsed_us() const { return ticks_to_us(elapsed_ticks()); }
};

struct BeamPos { eng::u16 v; eng::u16 h; };
[[nodiscard]] inline BeamPos beam_now();  // VPOSR (línea) + VHPOSR (posición horizontal)
```

`ScopedTimer` mide el coste de un tramo (para el HUD o la telemetría); `beam_now()` da la posición
del haz para saber el presupuesto consumido dentro del frame (útil para repartir el trabajo del
Blitter y del Copper).

## 5. Timers de usuario — `TimerService`

Timers de software sobre el VBlank (unidad *frames*) o sobre los ticks CIA (unidad *µs*),
one-shot o periódicos. El `TimerService` puro compara `frame_now`/`ticks_now` y postea
`MsgType::Timer`; nunca ejecuta lógica de juego en la ISR. La precisión efectiva depende del
productor que llama `poll_and_post`: se llama una vez por VBlank, así que **ambas unidades** se
observan con resolución de frame. Para deadlines sub-frame debe usarse `TickClock`/CIA one-shot y
publicar el vencimiento desde el pump seguro, no declarar precisión que el polling no ofrece.

El **reloj de µs** lo aporta un contador libre de **CIA-B Timer B** (modo continuo, reloj E): el
backend lo lee en cada `tick` (`ciab_ticks_now()`) y lo entrega a `poll_and_post` como `ticks_now`.
Así los timers µs comparan contra una base monotónica real, no contra cero. El sondeo sigue siendo
por VBlank, de modo que un timer µs vence con latencia de hasta un frame; resolver la resolución
sub-frame con un pump más fino o un one-shot de CIA es una mejora posterior.

El contrato de `TimerService` resuelve las inconsistencias TIME-005..008:

- **Fase preservada**: un periódico avanza `deadline += period` (no `now + period`), así que el
  periodo no acumula deriva aunque el sondeo llegue tarde.
- **Catch-up explícito**: `TimerCatchUp::Coalesce` (defecto) condensa el atraso en un mensaje con
  `expirations` = periodos vencidos; `SkipToNext` descarta el atraso y reprograma desde ahora;
  `CatchUpAll` entrega un mensaje por periodo vencido. Nunca se oculta el atraso.
- **Comparación wrap-safe**: un timer vence cuando `s32(now - deadline) >= 0`; `deadline` y
  `period` se acotan a menos de `2^31` (`kTimerMaxHorizon`) para que la aritmética modular no sea
  ambigua.
- **Identidad por handle**: `start` devuelve un `TimerHandle {slot, generation}` y `stop(handle)`
  cancela **una** instancia concreta; un handle obsoleto se rechaza. `stop_by_id(id)` cancela todas
  las instancias con ese id de usuario (compatibilidad con `os::add_timer`). El payload del mensaje
  lleva `id`, `handle`, `deadline` y `expirations`.

```cpp
enum class TimerUnit : eng::u8 { Frames, Microseconds };
enum class TimerCatchUp : eng::u8 { Coalesce, SkipToNext, CatchUpAll };

struct TimerHandle { eng::u8 slot; eng::u16 generation; /* valid(), packed() */ };

struct TimerSlot {
	bool active = false, periodic = false;
	TimerUnit unit = TimerUnit::Frames;
	TimerCatchUp catch_up = TimerCatchUp::Coalesce;
	eng::u16 id = 0, generation = 0;
	eng::u32 deadline = 0, period = 0; ///< horizonte < 2^31
};

constexpr eng::u8 kMaxTimers = 16;

struct TimerService {
	TimerSlot slots[kMaxTimers] {};

	/// `id` 0 = autoasignado; devuelve el handle o uno inválido si no hay slot.
	TimerHandle start(eng::u16 id, eng::u32 delay, TimerUnit unit, bool periodic,
			  eng::u32 frame_now, eng::u32 ticks_now,
			  TimerCatchUp catch_up = TimerCatchUp::Coalesce);
	bool stop(TimerHandle h);
	eng::u8 stop_by_id(eng::u16 id);

	/// Llamar 1× por VBlank: postea los timers vencidos y reprograma los periódicos.
	eng::u16 poll_and_post(MsgPort& port, eng::u32 frame_now, eng::u32 ticks_now);
};
```

Uso:

```cpp
// cada 1 s (PAL): periodo 50 frames
(void)os::g_timers.start(1, 50, os::TimerUnit::Frames, true, os::frame_count(), 0u);
// one-shot de 5 ms
(void)os::g_timers.start(2, 5000, os::TimerUnit::Microseconds, false, os::frame_count(), 0u);
```

El backend lo envuelve en `os::add_timer(id, frames)` (periódico de frames con `Coalesce`).

## 6. One-shot de hardware (CIA)

Para timeouts de E/S que necesitan precisión mejor que un frame, un one-shot real sobre CIA-B:

```cpp
/// Programa un one-shot de `us` microsegundos; al vencer postea MsgType::Timer(id).
void cia_oneshot_us(eng::u32 us, eng::u16 id);
```

El latch del timer se limita a 65535 ticks (~92 ms); para retardos mayores se combina con un
contador de reboses o se usa un timer de frames.

## 7. Integración con el bucle

- **Frames**: el `VBlank` latched (§10 del núcleo) ya da el ritmo; `TimerService` (unidad frames)
  es una capa fina encima y no añade productores.
- **µs**: el backend lee `ciab_ticks_now()` (CIA-B Timer B continuo) en cada `tick` y lo pasa a
  `TimerService`; la resolución efectiva sigue siendo de frame (TIME-004) hasta un pump más fino.
- **Profiling**: `ScopedTimer` y `beam_now()` no postean nada; son lectura directa para el HUD y la
  telemetría (no pasan por la cola, para no ensuciarla en el camino caliente).

## 8. Política de sincronización de frame

El contador real de VBlank existe independientemente de que el juego consuma mensajes. La
notificación, la espera del bucle y el trabajo de frame son decisiones separadas. `App::set_frame_sync`
elige la **política de notificación** sin duplicar el contador:

- `FrameSyncMode::Event`: publica un `MsgType::VBlank` **FIFO** por IRQ en `port()` (comportamiento
  histórico; puede desbordar una cola pequeña si el juego no la drena).
- `FrameSyncMode::Latch` (**recomendado**): no encola; la app lee la **instantánea coherente**
  `{sequence, missed}` con `take_frame_tick(VBlankTick&)`. `VBlankTick::frames_elapsed(from)` deriva
  el catch-up con aritmética unsigned.
- `FrameSyncMode::Disabled`: ni mensaje ni latch de la app; el contador sigue avanzando (para juegos
  que gestionan su propio sincronismo).

**Una única secuencia monotónica** (`App::m_vblank_count`) es la fuente de verdad; el backend mide el
VBlank (IRQ o `wait_vblank`) aunque la app no consuma mensajes. El `Engine` propaga el catch-up en
`context.frame.frames_elapsed` (latidos desde el frame anterior), de modo que el juego decide si hace
pasos fijos, anima por tiempo acumulado o renderiza latest-only, en lugar de que el bucle descarte
latidos en silencio (`run_frames` expone el conteo; `run_frames_polling` marca `1` por iteración).

## 9. Referencias

- `../amiga-bootcamp/01_hardware/common/cia_chips.md` (CIA-A/B, timer A/B, ICR, CRA/CRB, TOD).
- `../amiga-bootcamp/01_hardware/common/video_timing.md` (VPOSR/VHPOSR, líneas PAL/NTSC).
- `engine/src/platform/amiga/amiga.cpp` (`install_timer_service`, Timer A continuo).
- `docs/engine/architecture/BACKGROUND_TASKS.md` (tareas cooperativas que usan ese timer).
