# Demo 206 — bucle reactivo del mini-SO (`eng::os`)

Primera demo del mini-SO: el juego **no sondea hardware**. El tick del mini-SO (`eng::os::tick`)
latcha el VBlank y pollea los productores de entrada (ratón puerto 1, joystick puerto 2),
posteando mensajes; el bucle reactivo (`eng::os::MessagePumpGame`) drena el puerto y entrega cada
mensaje al `App` **antes** de la lógica de frame. La caja se mueve con el joystick (o el ratón).

## Qué muestra

- Overlay: `frames` (contador de VBlank), `msgs` (mensajes recibidos) y `joy` (estado del joystick).
- La caja naranja se mueve con el joystick del puerto 2; el ratón (puerto 1) también la mueve.

## Compilar / ejecutar

```bash
bash ./tools/build/build-demo.sh demos/amiga/206_message_loop --debug --clean
bash ./tools/run/run-demo.sh demos/amiga/206_message_loop --wait-ms 8000
```

## Resultado de referencia (A500)

```
frames: 27   msgs: 1   joy: 0
```

`msgs: 1` es el primer estado de entrada; los productores solo emiten al **cambiar**.

Diseño: `docs/engine/architecture/MINI_OS_MESSAGE_LOOP.md` (+ `MINI_OS_INPUT.md`). Tests:
HOST-219/220/236/237/252/253.
