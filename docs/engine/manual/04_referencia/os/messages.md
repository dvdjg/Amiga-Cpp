# Referencia — mensajes y puerto

El vocabulario y las colas del mini-SO. Un `Msg` es un **valor pequeño y copiable** (sin punteros propietarios): cabe en una entrada de cola y se copia en la ISR sin coste apreciable.

## Tipos — `os/message.hpp`

`MsgType` (`message.hpp:20`) es **contiguo desde 0** (para que el `switch`/la tabla de despacho sean un índice directo) y se agrupa por rango: **entrada** (`KeyDown`/`KeyUp`/`MouseMove`/`MouseButton`/`Joystick`/`Gamepad`), **tiempo** (`VBlank`/`Timer`), **sistema/E-S** (`FileDone`/`FileError`/`DiskChange`/`BlitDone`/`IntentDone`/`AudioCue`/`MusicEnd`/`AudioUnderrun`) y **aplicación** (`User`/`Quit`); `COUNT` es el tamaño de la tabla.

`Signal` (`message.hpp:49`) es la máscara de bits (modelo Exec): cada subsistema tiene su bit; la cola lleva los mensajes y `signalled` es el aviso barato de «hay algo de este tipo». `KeyQual` (`message.hpp:62`) son los modificadores. `Msg` (`message.hpp:85`) es la unión trivial de payloads.

## Colas y puerto — `os/port.hpp`

| Tipo | Qué es |
|---|---|
| `MsgQueue<N>` (`port.hpp:22`) | **Anillo SPSC** IRQ-safe de capacidad fija (potencia de dos, indexado con máscara). El productor escribe `head`, el consumidor `tail`. `push_isr` nunca bloquea: si está lleno, descarta y suma `overflows`. `pop`/`peek`. |
| `MsgPrio`/`prio_of`/`signal_for` (`port.hpp:74`, `:77`, `:96`) | Prioridad por tipo de mensaje y su bit de señal. |
| `PrioMsgQueue<N>` (`port.hpp:118`) | Cola con **prioridad**. |
| `MsgPort<N = 32>` (`port.hpp:239`) | El puerto: cola + `signalled` (OR barato desde la ISR); el consumidor drena la cola entera al despertar. |
| `VBlankLatch`/`take_vblank` (`port.hpp:276`, `:292`) | Latch del VBlank: el contador de frames lo consume una vez por frame. |

## Bucle y despacho

`pump_messages(app, port)` (`message_pump.hpp:19`) **drena** el puerto y entrega cada mensaje al `app`; `MessagePumpGame<App, N>` (`message_pump.hpp:38`) es la variante de juego. `HandlerTable<Ctx>` (`dispatch.hpp:21`) es la tabla `constexpr` de handlers (`MsgHandler<Ctx>` = `void(*)(Ctx&, const Msg&)`) indexada por `MsgType`, para despacho **por tabla**. Ver `docs/engine/architecture/MINI_OS_MESSAGE_LOOP.md` §5/§10/§13.

Volver al [índice de `os/`](README.md) · [Referencia](../README.md) · [manual](../../README.md).
