# NES en Amiga 500 por el engine — guía de integración práctica

> Documento **compañero** de [NES_CONSUMER.md](NES_CONSUMER.md). Aquel fija **qué necesita** el
> consumidor NES y qué falta; **este** dice **cómo se accede HOY a cada recurso a través del engine**
> (no como uno imaginaría que se accede), **cómo se compila**, **cómo se lanza** y **cómo se depura**
> (GDB + canal lateral). Cada afirmación está anclada en una cabecera o demo real del repo.
>
> Regla de oro del proyecto: **«la app pide, el engine dispone»**. El emulador describe **intención**
> (mueve el fondo, cambia un color, coloca N sprites); el engine decide el *cómo* y **posee los
> recursos** (bitplanes, copperlists, assets). El adaptador del emulador traduce NES → API del engine
> y **no toca plano, registro ni Blitter**. Ver `ROADMAP_API_COHERENCE.md` §7.

---

## 0. Cómo está organizado (dos niveles y la frontera)

| Nivel | Qué usa el emulador | Cabecera |
|---|---|---|
| **A — dominio de juego** | `App`, `Screen`, `input()`, `audio()`, `world()`, `assets()` | `#include <eng/api/api.hpp>` |
| **B — dispositivo** (cuando A no llega) | `Device`, `Scene`, `graphics::C2p4`/`c2p_1x1_4`, `copper::*`, `FramePlan` | `api.hpp` + `eng/field/*`, `eng/graphics/*` |

El **backend concreto** (`eng::amiga::AmigaBackend`) se instancia **solo en `main()`** y se pasa al
engine; el emulador no lo nombra más allá de ahí. El engine corre en **68000 puro** (nada de 68020/FPU:
lo impone `tools/analyze/asm-audit.mjs`); el hot loop del 6502/PPU debe ser C++ muy apretado o **asm**.

**No metas las `I*`** (`IChipMem`, `IC2p`, …) en `engine/`: viven en **tu** proyecto y se implementan
con un adaptador fino sobre `api.hpp` (demostrado por **HOST-341**). El engine solo aporta **helpers
generales** reutilizables (F7 de `ROADMAP_API_COHERENCE.md` §7.3).

---

## 1. Acceso a los recursos (necesidad NES → API real)

### 1.1 Memoria (Chip / Slow / Fast) — **lo primero que necesitas**

El engine no expone `malloc`: reparte la RAM en **pools por banco** que reserva al arrancar y de los
que entrega **bloques tipados** (`Block<Tag, MemoryKind>`).

```cpp
#include <eng/hw/info.hpp>          // hw::HwInfo, hw::probe, has_fast_ram
#include <eng/api/api.hpp>

// En main(), antes de crear el App:
eng::amiga::AmigaBackend backend {};
// (a) AUTOMÁTICO: sondea el hardware y elige un perfil (A500/A1200/…):
if (!backend.configure_game_memory()) return 0;
// (b) MANUAL (recomendado para el emulador): declara tú los bancos, en BYTES.
//     chip = display/C2P/planos · fast = estado 6502 + tablas (CPU-privada) · slow = auxiliar
if (!backend.configure_memory({512u*1024u, 0u, 0u, 1u*1024u*1024u})) return 0;
```

- **Fast RAM** es **CPU-privada** (Agnus no la ve): ahí van el estado del 6502, las tablas del PPU y
  los buffers de trabajo. **Chip** es la única que ve el DMA: planos, buffers del C2P, copperlists.
  **Slow** es no-Chip que Agnus tampoco ve (peor que Fast para CPU): úsala solo como auxiliar.
- **Detectar Fast en runtime**: `eng::hw::HwInfo hw{}; eng::hw::probe(hw);` → `hw::has_fast_ram(hw)`
  y `hw.fast_ram_bytes`; `hw.chip_ram_bytes`; `hw.cpu`; `hw.chipset`.
- **Reservar/consultar** por la fachada: `app.memory_manager()` (o `backend.memory_manager()`) →
  `mm.fast().reserve<Tag>(bytes, align)` / `mm.chip().reserve<PlaneTag>(bytes, 16u)`.
  Para presupuesto antes de pedir: `app.resources()` → `res::Budget::remaining_chip()` / `can_fit()`.
- **Memoria reutilizable** (nametables/CHR que cambian cada frame): `eng::BlockPool`
  (`eng/memory/block_pool.hpp`, HOST-340) — bloques fijos con `free` (no la arena *bump*).
- **Pila en Fast** (opcional): añade `FAST_STACK=1` al `build.args` de tu app (mueve la pila del hilo
  principal y el SSP de IRQs a Fast si existe; ver `INTERNAL_TYPE_SYSTEM.md` §3.8).

> ⚠️ **Caveat operativo**: la config de arranque por defecto (`config/mcp-amiga-c-debug.uae`) es un
> **A500 512K+512K sin Fast**. Aunque declares `fast_bytes`, si la máquina emulada no tiene Fast, esa
> arena queda vacía (`MemoryManager::has_fast()` = false). Para el emulador **añade Fast RAM** a la
> config `.uae` (o usa un perfil `A1200` con Fast) — ver §3.

### 1.2 El framebuffer del PPU → pantalla (chunky → planar)

El PPU produce un **buffer lineal de índices** (1 byte/píxel, 256×240). En OCS/ECS la vía es
**chunky→planar por Blitter**: la CPU escribe el chunky (rápido) y el Blitter lo transpone a planos
(lo que ve Agnus). Piezas reales:

- **`eng::graphics::c2p_1x1_4(w, h, bplsize, chunky, planes)`** — un blit, genérico 4bpp
  (`eng/graphics/c2p.hpp`); su versión **asm 68000** es `c2p_1x1_4_asm` (Kalms/Scout 1999, en
  `support/c2p_1x1_4.s`). **Referencia: demo `061_c2p_chunky_4bpl`** (chunky→4 planos + doble buffer
  + swap de copperlist tras VBlank; ver también `080_fire_rgb` y su `C2p4` de 13 fases).
- **`eng::graphics::C2p4`** (13 fases, `eng/graphics/blitter_state.hpp`) +
  `AmigaBackend::c2p_4bpp_step(state)` / `c2p_4bpp_program(state)` — C2P encadenado por **IRQ de
  Blit** (solapa con la CPU).
- El display planar lo describe una **`Scene`** (`graphics::composition`): doble buffer, paleta y
  copperlist. `scene.takeover(backend)` toma el display; **instala la copperlist del buffer nuevo
  tras VBlank** (`install_copper_list`), nunca a media pantalla (tearing) — el motor ya te da el
  punto de VBlank en `render`.

Planos y color: 4 planos = **16 colores**. La NES usa subpaletas; modela el PPU con **DPF 3+3**
(`SceneMode::DualPlayfield`) o **5 planos** (32 colores, OCS). `eng::Palette32` + parches por
`FramePlan` para los cambios de paleta a mitad de frame (splits).

`eng::effects::CopperChunky` (display copper **sin** bitplanes) existe, pero a 256×240 es carísimo
(un MOVE de Copper por píxel): úsalo solo para **HUD/franjas** pequeñas.

> ⏳ **Pendiente**: un helper de la fachada «dame un framebuffer indexado y publícalo» (hoy el
> emulador baja a `Scene`/`C2P`/`Device`, nivel B). El camino está probado por 061/080; falta
> envolverlo en el nivel A.

### 1.3 Cargar la ROM y datos de disco

```cpp
#include <eng/os/vfs.hpp>
// Vfs sobre un backend de ficheros (dos.library / trackdisk):
//   exists / size / read / read_all  ·  mounts (prefijo lógico → volumen)  ·  list (enumeración)
//   open → VfsFile (handle RAII)
const auto n = vfs.read_all("data/rom/game.nes", {buf, sizeof buf});   // Expected<u32, VfsError>
```

- **Tipado, síncrono**: `eng::res::load_file<Tag>(mm, path)` (abre, mide, reserva en el banco del
  dominio y lee).
- **Asíncrono, sin bloquear el frame** (R6.7): `eng::res::AsyncRead` (`begin` + `os::file_pump` por
  frame + `on_done`) y `eng::res::AsyncOverlay` (lectura → `decode_engz` → `DynLoader`, por segmento).
  Referencias: demo `212_zone_resources`, HOST-410/402.
- **Carga ANTES de tomar el display** (`takeover_display`/`App::start`): `dos.library` necesita las
  interrupciones vivas.
- **ROM grande (>880 KB)**: no cabe en un disquete. Opciones: **HDD/ADF+tar** montado como volumen
  (`tools/fs/make-volume.mjs` genera el contenido de `DH1:`), o partir la ROM en **overlays**
  (`eng::res::DynLoader` carga/descarga módulos HUNK/`.englib`).

### 1.4 Timing: el «NMI» y VBlank

- El engine corre **`Game::init` una vez** y, **una vez por frame**, `Game::update` + `Game::render`;
  cada `update` ocurre **después de un VBlank**. Ese es el sitio de la **lógica de frame del 6502**
  (el NMI del emulador). `app.frame()` da el índice de frame.
- `main()` (bucle por defecto, **por IRQ mínima**, sin polling):

  ```cpp
  eng::App app {backend, game, backend.memory_manager()};
  if (!app.set_display(display) || !app.start()) return 0;
  app.run();            // o app.run(N) para N frames (útil para la captura determinista)
  ```
- **Mini-OS** (`eng/os`): el hook de VBlank publica el latido (`os::tick`: entrada, timers, cola). El
  `App` **drena** esa cola en `pump()`/`route_resource_io` (los `FileDone` de E/S asíncrona llegan
  ahí). Para solapar CPU y Blitter, `App::set_async_present(true)` + `blitter_memcpy_async`.

### 1.5 Entrada

```cpp
eng::input::InputAggregator& in = app.input();   // estado del frame
if (in.pad0.up)   { /* ... */ }
if (in.pad0.fire) { /* ... */ }                  // rojo; in.pad0.fire2 = azul (CD32)
```

`PadState` da `up/down/left/right/fire/fire2` + botones CD32 (`play/yellow/green/reverse/forward`) —
mapéalos al mando NES. El runner puede presentar un **pad CD32** con `--cd32`.

### 1.6 Audio (APU)

`app.audio()` → `AudioSystem` (SFX + música). Dos caminos (elección de §9.2 de `NES_CONSUMER.md`):

- **Fiel (tono por canal)**: `AudioMixer::play(SampleEvent{period, volume,…})` +
  `eng::audio::paula::period_for_hz(hz)` para reproducir el **APU replayer** con pitch/timbre.
- **Engine-native**: en el **pipeline offline** convierte SFX a **muestras** y música a **módulo**
  (Pt/P61) y el engine los reproduce (`app.play_music`/`audio()`).

### 1.7 Tiles/BG/sprites (para el port completo, más adelante)

- **BG nametable**: `tilemap::TileMap16` + `tilemap::TileEditor` (HOST-342) + `tilemap::AttributeTable`
  (HOST-344), y los **drivers** `field::StripScrollLayer` (tiras, 50 fps single) / `field::XlimitedScene`
  (corcóscru) / `ScrollKind` (`CopperRing`/`BlitterColumns`/`Fine`).
- **Sprites/OAM**: `scene::ActorStore`/`Actor` + `SpriteAllocator` + `compose_sprites` + fallback a
  BOB (`graphics::bob_draw`).
- ✅ Existen `decode_2bpp_planar` (F7.1), `TileEditor`/`AttributeTable` (F7.2), `eng::Copper` (F7.4),
  Paula (F7.5), `BlockPool` (F7.6) y `compose_sprites` (F7.7).
- ✅ **El 8-way (F7.3) ya está**: el driver **`CopperSplit`** es el **corkscrew `XlimitedScene`**
  (`y_mode = Ring`, demo 107), elegido por el planner (`scroll_kind_for_variant(XYLimited) →
  CopperSplit`, HOST-343). **No hace falta** un `XYUnlimited` "circular" (más caro en Chip:
  `CIRCULAR_VS_XLIMITED.md`). Para **ahorrar memoria**, usa `XLimited`/corkscrew (lo que propones).
- ✅ **Sprites (F7.7)**: `scene::compose_sprites` (con `SpriteAllocator` + BOB fallback) cubre la
  semántica NES 8/línea/overflow; el consumidor mapea `place`→`ActorStore::add`.

---

## 2. Compilar para Amiga

Requisitos (detalle en `docs/build/BUILD_AND_RUN.md`): **Windows nativo + Git Bash + Node.js**, el
toolchain `m68k-amiga-elf` de la extensión Bartman (`../vscode-amiga-debug/bin/win32`), Kickstart A500.

```powershell
$env:AMIGA_BIN_PATH = "$env:USERPROFILE/Documents/programa/AI/Amiga/vscode-amiga-debug/bin/win32"
& 'C:\Program Files\Git\bin\bash.exe' ./tools/build/build-demo.sh <ruta-de-tu-demo> --debug --clean
```

- **Nunca uses WSL**: mangla rutas y rompe `cc1plus`/WinUAE. Usa Git Bash por su ruta absoluta.
- **Estructura de una app**: `demos/<familia>/<categoria>/NNN_tema/src/main.cpp` (+ `README.md`). El
  `main()` Amiga mínimo:

  ```cpp
  #include <eng/api/api.hpp>
  #include <eng/platform/amiga/backend.hpp>
  #include <exec/execbase.h>  #include <proto/exec.h>
  #include "support/gcc8_c_support.h"
  struct ExecBase* SysBase = nullptr;
  extern "C" { __attribute__((used)) volatile eng::debug::RunStatus g_eng_run_status {
      eng::debug::run_status_magic, eng::debug::run_status_version,
      static_cast<eng::u16>(eng::debug::RunState::Cold), 0, 0, }; }

  // struct Game { void init(auto& app){ eng::debug::mark_init_started(g_eng_run_status); /*…*/ mark_ready(...);} 
  //              void update(auto& app){ eng::debug::mark_frame(g_eng_run_status, app.frame()); /*6502/PPU*/ }
  //              void render(auto& app){ app.present(); eng::debug::probe_when_ready(g_eng_run_status, app.frame()); } };

  int main() {
      SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
      eng::debug::reset(g_eng_run_status);
      eng::amiga::AmigaBackend backend {};
      backend.configure_memory({/*chip*/512u*1024u, /*slow*/0u, /*frame*/0u, /*fast*/1u*1024u*1024u});
      Game game {}; eng::GameDisplay display {}; display.width=320; display.height=256; display.color_depth=4;
      eng::App app {backend, game, backend.memory_manager()};
      if (!app.set_display(display) || !app.start()) return 0;
      app.run(0xffffu);
  }
  ```
- **`g_eng_run_status`** es obligatorio: el runner detecta `InitStarted`/`Ready`/`Failed` por el canal
  lateral (no dependas solo de la captura).
- **Ajustes por app** en `build.args` (una `CLAVE=valor` por línea, comentarios `#`):
  `FAST_STACK=1` (pila en Fast), `DEMO_OPT`/`ENGINE_OPT`/`C_OPT` (niveles de `-O`; hay un bug de
  codegen de gcc 15 a `-O1` en el camino del bucle → la 212 usa `DEMO_OPT=-O2`).
- **Variante de máquina**: `TARGET_MACHINE` (`A500` por defecto · `A1200` · `AtariST` · `Megadrive` ·
  `NeoGeo`); el `CONFIG_ID` resultante está en `out/demos/<app>/<CONFIG_ID>/`.
- **68000 puro**: valida con `node tools/analyze/asm-audit.mjs <app>.elf` (falla si aparece `muls.l`,
  `fmove`, `extb.l`, `bf*`, `cas.l`…).

---

## 3. Lanzar el emulador

```powershell
& 'C:\Program Files\Git\bin\bash.exe' ./tools/run/run-demo.sh <ruta-de-tu-demo> --keep-running
```

- El runner copia la app a `dh1:a.exe`, monta `out/fs/content` como **`DH1:`** (contenido generado por
  `node tools/fs/make-volume.mjs`), parchea una config WinUAE temporal, lanza `winuae-gdb.exe`, conecta
  GDB, **espera `READY` por el canal lateral** y guarda `out/run/<app>/screenshot.png` + `run-report.json`.
- Opciones: `--release` · `--warp` (acelera VBlanks; **solo** throughput/diagnóstico, no para juzgar
  suavidad) · `--keep-running` (deja WinUAE abierto) · `--disk <adf>` (monta un ADF en `DF0:`) ·
  `--cd32` (pad CD32) · `--config <ID>` · `--side-channel-port <n>`. Detalle: `docs/build/BUILD_AND_RUN.md`.
- ⚠️ **Fast RAM**: la config base es **A500 512K+512K sin Fast**. Para el emulador edita
  `config/mcp-amiga-c-debug.uae` (añade `fastmem_size`/z3) o usa un perfil con Fast/`A1200`, y declara
  `fast_bytes` en `configure_memory`. Sin Fast, el 6502 irá lastrado por el bus de Chip.
- **Regresión**: `bash ./tools/test-regression.sh --demo <ruta> --warp` (build → run → analyze).

---

## 4. Depurar y usar el canal lateral

Herramienta completa: `docs/debugging/system/debug-winuae-v2-guide.md` (léela). Resumen operativo:

- **Dos puertos, configurables por entorno** (cada hilo/agente usa su **par propio**; no mates
  instancias ajenas): **GDB** `WINUAE_GDB_PORT` (def. 2345) y **canal lateral**
  `WINUAE_SIDE_CHANNEL_PORT` (def. 2346). El runner los propaga al emulador.

  ```bash
  WINUAE_GDB_PORT=2355 WINUAE_SIDE_CHANNEL_PORT=2421 bash ./tools/run/run-demo.sh <app> --warp
  ```

- **Canal lateral** (independiente de GDB; útil con GDB inerte o para observar sin detener el 68000):
  `state`, `regs`, `mem <addr> <len>`, `runstatus <addr>`, `screenshot`, `input`.

  ```bash
  # consulta manual a una instancia viva (wrapper del repo)
  bash ./tools/debug/winuae-side-channel.sh state
  ```
- **GDB RSP** por código con el servidor de `mcp-winuae-emu/dist/` (`WinUAEConnection`/`GdbProtocol`):
  `readRegisters`, `setBreakpoint`, `step`, `readMemory`/`writeMemory`, `sendMonitorCommand('watch …')`.
- **Breakpoints/watchpoints con origen** (`monitor watch src=copper|blitter|dma|bpl|spr|audio|…`):
  «¿quién/cuándo escribe X?». Y **protect/cheat** (`monitor protect …`), **rewind**, **trace**.
- **Periférico de depuración in-Amiga** `0xB70000` (`eng/debug/peripheral.hpp`): consola byte a byte,
  **checkpoints** (coste por tramo del frame), contadores, `debug_arg` (A/B de variantes) y
  **breakpoint auto-dirigido**. Se lee con `monitor debugperiph checkpoints|counters|console`.
- **Símbolos**: resuelve direcciones runtime desde el `.map` (`out/demos/<app>/<CONFIG_ID>/<app>.map`)
  o con `winuae_print` + `mapPath` (DWARF del `.elf`).
- **Validación visual**: captura + `node tools/analyze/ollama-desc.mjs <dir> <idx> "<prompt>"` (modelo
  de visión local); para dinámica, **secuencia** de frames (no una sola captura).

---

## 5. Qué existe ✅ y qué está pendiente ⏳ (para no perseguir APIs fantasma)

| Necesidad NES | Estado | Dónde |
|---|---|---|
| Memoria Chip/Slow/Fast + bloques tipados | ✅ | `MemoryManager`, `hw::HwInfo`, `BlockPool` |
| Cargar ROM/datos (VFS, síncrono y asíncrono) | ✅ | `os/vfs.hpp`, `res::load_file`, `AsyncRead`/`AsyncOverlay` |
| Framebuffer del PPU → pantalla | ✅ **nivel B** | `c2p_1x1_4`/`C2p4` + `Scene` (demos 061/080); ⏳ falta el helper de nivel A |
| VBlank/frame sync + mini-OS | ✅ | `App::run` (`init/update/render` por frame), `os::tick`, `pump()` |
| Entrada (mando) | ✅ | `app.input().pad0` |
| Audio (SFX + tono por canal o módulo) | ✅ | `AudioSystem`, `AudioMixer::play(SampleEvent)` |
| Sprites/OAM (64, flip, prioridad) | ✅ | `scene::compose_sprites` (HW sprites + `SpriteAllocator` + BOB fallback; NES 8/línea → canales HW/multiplexado, el resto **BOB**; `degraded` = no cupieron) |
| BG nametable + atributo 16×16 | ✅ | `TileEditor` + `AttributeTable` (HOST-344); **8-way = corkscrew `XlimitedScene`** (`ScrollKind::CopperSplit`; demo 107) |
| Paleta + splits + énfasis | ✅ | `Palette32` + `FramePlan`/`eng::Copper` |
| Compilar/lanzar/depurar | ✅ | `build-demo.sh` / `run-demo.sh` / GDB + canal lateral |

**Adaptadores triviales** (ya cubiertos): `IBlitter`→`Device`/`FramePlan` · `IPalette`→`Palette`/
`FramePlan` · `IInput`→`App::input()` · `IDisplay`→C2P+`Scene` · `ISurface`→`gfx::Bitmap` ·
`ICopper`→`eng::Copper` · `IChipMem`→`BlockPool`+`res::Budget`.

---

## 6. Orden de ataque sugerido (MVP → completo)

1. **Esqueleto + memoria**: `App` + `configure_memory` con Fast; carga de ROM por el VFS (antes de
   `start`); pantalla de diagnóstico con `Screen`/`debug`.
2. **PPU → framebuffer**: escribe el chunky de índices (bucle 6502/PPU) + `c2p_1x1_4` + doble buffer
   + swap en VBlank (copia el patrón de **061**). Valida con captura + Ollama.
3. **6502 + mappers**: en `update`; mide ciclos con checkpoints del periférico; aprieta el hot loop
   (C++ `-O2` → asm 68000 si hace falta).
4. **Entrada + APU** (SFX offline primero; tono por canal después).
5. **BG con scroll 8-way y OAM**: cuando F7.3/F7.7 estén; hasta entonces, redibuja el BG por software
   (C2P) o usa `XLimited` (eje X).

Cualquier capacidad que creas necesaria y no encuentres: **no la simules bajando a registros**; pídela
como **helper general** en el engine (es la frontera acordada).
