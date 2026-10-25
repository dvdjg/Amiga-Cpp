# VALIDATION - 500 Caverna

## Medición

```text
500_caverna/A500_release (estático)              | emulado=49,27 fps | 143990 ciclos/frame | 1,015 campos/frame
500_caverna/A500_release (joystick der., 2 px/f) | emulado=49,51 fps | 143276 ciclos/frame | 1,010 campos/frame
```

Ventana de 12 s; `measure-fps`. La fila en movimiento usa **entrada real sostenida** (`measure-fps --joy 1:right`), no auto-scroll. La tabla de parcheo por placement permite que un canal reutilizado por multiplexado vertical (nueve actores) siga parcheando POS/CTL en vez de reemitir la copperlist completa.

El mapa es `FixedTileLayerMap<256>` y la cámara se conduce por la fachada. El display es **DPF 3+3** de 320×208; actores: jugador + ocho enemigos (nueve entidades de `SpriteDirector`). El anillo usa `display_height=240` para el staging del walk horizontal.

## Alcance verificado

- build y ejecución en WinUAE/A500;
- mapa fijo, scroll horizontal, cámara con dead-zone y jugador visible;
- colisión de la caja 16×16 del jugador contra los tiles sólidos y las plataformas flotantes;
- gravedad, aterrizaje y **salto**: con `--joy 1:up` el jugador sube (y=226→196→186 en la secuencia) y vuelve a bajar; el seguimiento por píxeles del sprite naranja confirma la curva;
- ocho enemigos con patrulla, stomp (mueren y suman 100), daño lateral con invulnerabilidad, vidas y reinicio al llegar a cero;
- cuatro hojas de sprite del manifiesto copiadas a Chip; el director asigna canales, culled fuera de pantalla y parchea POS/CTL por placement;
- `SpriteDirectorPass` conduce el director desde el `App` antes del scroll; `refresh_positions` actualiza X/Y/alto/DATA;
- fix de scroll: filas 14..19 de PF2 vacías y anillo de 240 px; verificado con captura paso a paso;
- manifiesto de assets (`palette`, mapas, filas, módulo y SFX) + prebuild reproducible; blobs en big-endian;
- audio: `m_music_ok=true` (P61) y `m_audio_ready=true` (mixer), con SFX de 256/384/128 B registrados;
- HUD de host con `CAVERNA DEMO 500`, versión, frame y fps;
- entrada de ratón y joystick; el joystick del puerto 2 se lee también por hardware como fallback.

## Visión y movimiento

`src/VISION.md` es la evaluación canónica del prompt compartido. El modelo describe el jugador naranja en el aire, cambios de posición de enemigos y ausencia de halos/recortes. La comprobación específica `vision-jump.mjs` sigue respondiendo `SALTO: NO` con la escena completa (confunde al jugador con enemigos naranjas); la subida/bajada sí se midió por píxeles en las capturas.

## Pendiente / NO VERIFICADO

- confirmación automática del salto por VLM (la traza de píxeles sí lo confirma);
- latencia input→visible medida de forma objetiva (hoy solo se valida la curva de salto);
- animación de dos poses desactivada para mantener ~49,3-49,5 fps; el arte de dos poses sigue en el manifiesto;
- presupuesto de audio en el frame y prueba auditiva automática.

## Pruebas de entrada

Salto con joystick del puerto 2:

```bash
bash tools/run/run-demo.sh games/500_caverna --config A500_release --wait-ms 800 \
  --joy 1:up --joy-through-sequence --sequence-frames 6 --sequence-interval-ms 40
```

Movimiento horizontal:

```bash
bash tools/run/run-demo.sh games/500_caverna --config A500_release --wait-ms 800 \
  --joy 1:right --joy-through-sequence --sequence-frames 6 --sequence-interval-ms 40
```

Host tests:

```bash
CXX=<g++> bash tools/run-host-tests.sh \
  tests/host/res/386_assets_result \
  tests/host/scene/485_sprite_director \
  tests/host/scene/497_sprite_bank \
  tests/host/scene/498_caverna_gameplay
```
