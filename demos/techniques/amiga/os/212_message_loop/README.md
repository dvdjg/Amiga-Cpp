# Demo 212 — bucle reactivo del mini-SO (`eng::os`)

Primera demo del mini-SO: el juego **no sondea hardware**. El tick del mini-SO (`eng::os::tick`)
latcha el VBlank y pollea los productores de entrada (ratón puerto 1, joystick puerto 2),
posteando mensajes; el bucle reactivo (`eng::os::MessagePumpGame`) drena el puerto y entrega cada
mensaje al `App` **antes** de la lógica de frame. La caja se mueve con el joystick (o el ratón).

## Qué muestra

- Overlay: `frames` (contador de VBlank), `msgs` (mensajes recibidos), `joy` (joystick), `keys`
  (teclado), `pad` (pad CD32), `t` (timers) y `bg` (slices de idle de la tarea de fondo, M10).
- La caja naranja se mueve con el joystick del puerto 2; el ratón (puerto 1) también la mueve.
- Una **tarea de fondo** (`eng::os::TaskSystem`) avanza un paso por cada slice de idle; el bucle
  (`MessagePumpGame::bind_tasks`) solo le da idle en los frames **sin** mensajes, así que `bg` prueba
  que el frame/entrada siempre gana. `bg` se reporta además por el run-status como `0x2120BBBB`.
- **Timer de usuario con periodo 2** (`os::add_timer(1u, 2u)`), reportado como `0x2123TTTT`. El TU
  de la demo se compila a `-O2` vía `build.args` por el bug de codegen de gcc 15 m68k a `-O1`
  (`docs/debugging/investigaciones/pump-timer-o1-codegen.md`).
- **Pad CD32** en el puerto 2 (`os::enable_cd32_pad()`), reportado como `0x2124MMMM`. Se prueba como
  `Gamepad` y, si no hay pad, el `tick` **cae al joystick**. La presentación del pad en WinUAE está
  pendiente de resolver en el emulador (ver ROADMAP_MINI_OS M2); el runner expone `--cd32`.
- **Teclado** por IRQ de CIA-A (`os::enable_keyboard()`), reportado como `0x2121LLKK`.

## Compilar / ejecutar

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/os/212_message_loop --debug --clean
bash ./tools/run/run-demo.sh demos/techniques/amiga/os/212_message_loop --wait-ms 8000
# con pad CD32 en el puerto 2 (pendiente de resolver en WinUAE) + boton rojo:
bash ./tools/run/run-demo.sh demos/techniques/amiga/os/212_message_loop --cd32 --joy 1:fire
```

## Resultado de referencia (A500)

```
frames: 49   msgs: 20   joy: 0   keys: 0   bg: 23   t: 25   pad: 0
```

`msgs` sube con cada mensaje; los productores de entrada solo emiten al **cambiar**, pero el
**timer de periodo 2** entrega `t` periódicamente (`t: 25`), lo que verifica ese caso. `bg` avanza
en los frames sin mensajes (una lectura del run-status pasó de `0x21200002` a `0x2120001F`).

Diseño: `docs/engine/architecture/MINI_OS_MESSAGE_LOOP.md` (+ `MINI_OS_INPUT.md`, `MINI_OS_TASKS.md`).
Tests: HOST-219/220/236/237/252/253/307/308.
