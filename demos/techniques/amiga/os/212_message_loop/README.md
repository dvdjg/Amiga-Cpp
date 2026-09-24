# Demo 212 — bucle reactivo del mini-SO (`eng::os`)

Primera demo del mini-SO: el juego **no sondea hardware**. El tick del mini-SO (`eng::os::tick`)
latcha el VBlank y pollea los productores de entrada (ratón puerto 1, joystick puerto 2),
posteando mensajes; el bucle reactivo (`eng::os::MessagePumpGame`) drena el puerto y entrega cada
mensaje al `App` **antes** de la lógica de frame. La caja se mueve con el joystick (o el ratón).

## Qué muestra

- Overlay: `frames` (contador de VBlank), `msgs` (mensajes recibidos), `joy` (estado del joystick) y
  `bg` (slices de idle de la tarea de fondo, M10).
- La caja naranja se mueve con el joystick del puerto 2; el ratón (puerto 1) también la mueve.
- Una **tarea de fondo** (`eng::os::TaskSystem`) avanza un paso por cada slice de idle; el bucle
  (`MessagePumpGame::bind_tasks`) solo le da idle en los frames **sin** mensajes, así que `bg` prueba
  que el frame/entrada siempre gana. `bg` se reporta además por el run-status como `0x2120BBBB`.

## Compilar / ejecutar

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/os/212_message_loop --debug --clean
bash ./tools/run/run-demo.sh demos/techniques/amiga/os/212_message_loop --wait-ms 8000
```

## Resultado de referencia (A500)

```
frames: 27   msgs: 1   joy: 0   bg: 26
```

`msgs: 1` es el primer estado de entrada; los productores solo emiten al **cambiar**. `bg` avanza
en los frames sin mensajes (una lectura del run-status pasó de `0x21200002` a `0x2120001F`).

Diseño: `docs/engine/architecture/MINI_OS_MESSAGE_LOOP.md` (+ `MINI_OS_INPUT.md`, `MINI_OS_TASKS.md`).
Tests: HOST-219/220/236/237/252/253/307.
