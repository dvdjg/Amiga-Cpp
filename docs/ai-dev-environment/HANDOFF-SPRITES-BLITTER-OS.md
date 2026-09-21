# Handoff — Sprites, Blitter↔Copper y mini-SO

Documento de traspaso para continuar este trabajo en otro hilo. Resume **objetivo**, **lo ya
hecho**, **lo pendiente (ordenado)** y los **hechos clave** aprendidos (para no repetir la
investigación).

---

## 1. Objetivo del hilo

Mejorar el soporte del engine para: (a) **sprites hardware** (multiplexado, attached/15
colores, colisión, sprite-as-playfield); (b) técnicas **Blitter ↔ Copper**; (c) el **mini-SO de
mensajes** (`eng::os`) como base de notificaciones asíncronas (p. ej. fin de blit).

Punto de partida documental: `docs/reference/amiga/techniques/sprite-layer.md`,
`sprite-horizontal-multiplex.md`, `docs/guides/roadmap/ROADMAP_BLITTER_COPPER.md`,
`engine/include/eng/os/README.md`, `docs/engine/architecture/MINI_OS_MESSAGE_LOOP.md`.

---

## 2. Hecho (y pusheado)

### Sprites
- **Colisión hardware validada** (`graphics/sprite_collision.hpp` +
  `MinimalBackend::set/read_sprite_collision`; demo `206_sprite_collision`). El bug era leer el
  bit **5** además del **1** (ver §4).
- **`SPRxCTL` corregido**: `ATTACH` = **bit 7**, `VSTART[8]`=bit 2, `VSTOP[8]`=bit 1,
  `HSTART[0]`=bit 0 (en `sprite_manager.hpp`, `copper/scheduler.hpp`, `SpriteLayer`). El engine
  (y el bootcamp) lo tenían mal.
- **`effects::SpriteLayer`** (`eng/api/effects.hpp`): capa de fondo (sprite-as-playfield) con
  **rearm horizontal** (un `WAIT` al inicio de cada línea + ráfaga `POS`/`DATB`/`DATA` por
  canal) y **canales DMA** (estructura con **cabecera POS+CTL**, `SPRxPT`→cabecera). Demos
  `207_sprite_layer`.
- **`SpriteAllocator`**: pares **attached** alineados a canal par (HOST-003).
- **`SpriteConfig.attach`** (bit 7) + `emit_config`.

### Blitter
- **`MinimalBackend::blitter_memcpy(dst, src, wait)`**: copia **lineal** (`D=A`, módulos 0),
  síncrona/asíncrona.
- **`MinimalBackend::blitter_memcpy_async(dst, src, on_done, user)`**: arranca sin esperar y
  arma la **IRQ BLIT** para notificar. Doc: `docs/reference/amiga/techniques/blitter-memcpy.md`.
- **Demo `208_blitter_memcpy`**: **verde** — `sync: OK` y `async (IRQ BLIT -> MsgPort): OK`.

### Mini-SO
- **`eng::os::MsgPort`/`MsgQueue`** (`engine/include/eng/os/port.hpp`): cola SPSC + señal
  (HOST-250).
- **`App::port()`/`pump()`** (`engine/include/eng/api/game.hpp`) + contadores VBlank/BlitDone.

### Docs / reglas
- `docs/reference/emulators/` (`README`, `winuae/collision.md`, `winuae/sprite-dma.md`).
- `docs/reference/ahrm/ERRATA_Y_NOTAS.md` (ATTACH=bit7, par/impar, `collision_level`, cabecera DMA).
- `AGENTS.md` **§1.11** (si el hardware no funciona → fuente del emulador).
- `docs/guides/roadmap/ROADMAP_BLITTER_COPPER.md`, `docs/reference/amiga/techniques/{sprite-layer,sprite-horizontal-multiplex,blitter-memcpy,copper-timing-and-budget}.md`.

Commits (recientes): `63c263f` (os port), `c3d02d8` (blitter async), `dbd78c2` (demo 208),
`7a84565` (App port). `master` al día con `origin`.

---

## 3. Pendiente (ordenado por dependencia/prioridad)

1. **Demo 207: columna naranja suelta.** Un canal Copper queda armado fuera de la banda.
   Revisar el armado/`SPRxPT` de los canales Copper en `SpriteLayer`.
2. **Bucle reactivo (demo 209).** Con `App::port()/pump()`, consumir **VBlank/BlitDone** en
   `update` y **verificar** el puerto. Requiere un *hook* del `Engine` que reenvíe el VBlank al
   puerto (sin duplicar el servicio de VBlank del bucle interrupt-driven).
3. **`eng::os` completo** (`message.hpp`, `os.hpp`, `time.hpp`, `timer.hpp`, `task.hpp`,
   `file.hpp`; solo `port.hpp` hecho). Por fases, cada una con test. Ver
   `engine/include/eng/os/README.md` y `ROADMAP_MINI_OS.md`.
4. **Técnica A — Copper lanza blits.** `CopperIntentKind::BlitterJob` (+ campos de registros en
   `CopperIntent`) + emisión en `Scheduler::emit_single_intent` + **política de ventana segura**
   (serializar con los blits de CPU) + demo (borde de scroll).
5. **Técnica B — Blitter escribe/parchea la copperlist.** Solo si el perfil muestra **muchos**
   moves/frame.
6. **`SpriteAllocator`**: contemplar `width_words=2` (AGA 32 px).
7. **`SpriteLayer` ↔ `EffectCost`/`reserve_band`/`Scene::add_effect`** (integración).
8. **Deuda de documentación** (inicio del hilo): `field/` (~30), `sim/` (~120), `ai/` (~44);
   baseline `doc-coverage` ~747. Y **refinar el detector** (constructores/`operator` multilínea).

---

## 4. Hechos clave (NO re-investigar)

- **`SPRxCTL`**: `ATTACH` = **bit 7** (AHRM cap. 4; el bootcamp `sprites.md` lo pone mal en bit 0).
  `VSTART[8]`=bit 2, `VSTOP[8]`=bit 1, `HSTART[0]`=bit 0. WinUAE lo confirma (`custom.cpp:4036-4042`).
- **Colisión `CLXCON`/`CLXDAT`**: la comparación es **por grupo par/impar** (`enable & 0xAA` y
  `enable & 0x55`). Con solo planos de un grupo, la comparación del grupo vacío
  `(apixel & 0) == 0` es **siempre cierta** → el bit del grupo excluido se enciende **siempre**.
  Para plano **impar** (`BPL1`) mirar **bit 1**; par (`BPL2`), **bit 5**. Fuente:
  `../WinUAE-DBG/drawing.cpp:4196-4211`.
- **`currprefs.collision_level`** (WinUAE): `0`=sin colisión, `≥1` sprite-sprite, `>1`
  sprite-bpl, `≥3` bpl-bpl.
- **Sprite DMA**: la estructura en Chip RAM lleva **cabecera `POS`+`CTL`**
  (`[POS, CTL, DAT0, DATB0, …, 0, 0]`); `SPRxPT` apunta al **inicio**. Sin cabecera, el DMA
  interpreta la imagen como POS/CTL (basura).
- **Rearm horizontal** (sprite-as-playfield): **un `WAIT` al inicio de cada línea** + **ráfaga**
  `POS`/`DATB`/`DATA` de **todos** los canales; el `CTL` **por línea** (`VSTART=line`,
  `VSTOP=line+1`). Un `WAIT` por canal en su X **no** funciona. Fuente: `spr_layer/Sprite_Layer/`.
- **El Blitter es único**: escribir `BLTSIZE` (CPU o Copper) con un blit activo lo aborta →
  serializar. El engine hoy **no** dispara blits desde el Copper.
- **IRQ BLIT** (nivel 3): `level3_dispatch` (`amiga_minimal.cpp`) llama a la tarea de
  `install_blit_service`/`set_blit_service` → es la notificación de fin de blit.

---

## 5. Ficheros clave

- Sprites: `engine/include/eng/graphics/{sprite_manager,sprite_allocator,sprite_collision}.hpp`,
  `engine/include/eng/graphics/copper/scheduler.hpp`, `engine/include/eng/api/effects.hpp`
  (`SpriteLayer`).
- Blitter: `engine/include/eng/platform/amiga_minimal.hpp`,
  `engine/src/platform/amiga_minimal/amiga_minimal_blitter.cpp`.
- Mini-SO: `engine/include/eng/os/port.hpp`, `engine/include/eng/api/game.hpp`.
- Intents: `engine/include/eng/graphics/raster_intent.hpp` (`CopperIntentKind`).
- Demos: `demos/amiga/{206_sprite_collision,207_sprite_layer,208_blitter_memcpy}/`.
- Tests: `tests/host/{003_sprite_allocator,105_sprite_hrearm,132_rotozoom,134_raster_gradient,250_os_port}/`.
- Docs: `docs/reference/{emulators/,ahrm/ERRATA_Y_NOTAS.md,amiga/techniques/}`,
  `docs/guides/roadmap/ROADMAP_BLITTER_COPPER.md`, `AGENTS.md §1.11`.
- Emulador (fuente): `../WinUAE-DBG/` (`custom.cpp`, `drawing.cpp`, `include/custom.h`).

---

## 6. Comandos

```bash
# Build / run / captura de una demo
bash tools/build/build-demo.sh demos/amiga/208_blitter_memcpy --debug
bash tools/run/run-demo.sh demos/amiga/208_blitter_memcpy --warp --sequence-frames 1 --sequence-interval-ms 300

# Tests host
CXX=/c/Users/dvdjg/Documents/programa/AI/Amiga/mingw64/bin/g++.exe \
  bash tools/run-host-tests.sh tests/host/250_os_port

# Gates
node tools/check/{doc-coverage,encoding,links,test-numbering,demo-numbering}.mjs
```

---

## 7. Reglas aplicables

- **`AGENTS.md §1.11`**: si un mecanismo del chipset no funciona, leer el **fuente del
  emulador** (`../WinUAE-DBG/`), documentarlo en `docs/reference/emulators/<emu>/<tema>.md`
  citando `fichero:línea`, y completar la referencia (`ERRATA_Y_NOTAS.md`).
- **`AGENTS.md §1.5`**: sin **demo** no se considera *verificado*.
- **`AGENTS.md §1.8`**: commitear lo del turno anterior al inicio; no commitear el turno en curso.
- Estilo/API: `docs/engine/architecture/{CODING_STYLE,PUBLIC_API}.md`.

---

## 8. Frase para arrancar el otro hilo

> Continúa el trabajo de sprites/blitter/mini-SO de este repo. Lee primero
> `docs/ai-dev-environment/HANDOFF-SPRITES-BLITTER-OS.md` (§3 pendientes y §4 hechos clave) y
> `AGENTS.md §1.11`. Empieza por el **pendiente 1** (columna naranja de la demo 207) y sigue el
> orden. Verifica cada paso con demo o test host.
