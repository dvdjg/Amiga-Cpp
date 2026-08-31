# Engine 2D — Abstracciones (borrador en revisión)

> **Estado**: BORRADOR en revisión. Puede sufrir cambios menores tras discutir enlaces externos (referencias a otros engines y técnicas). Este documento es la especificación técnica interna; de aquí se derivará después una descripción de alto nivel para el gran público con artículos por tema concreto.
> Normas del engine que condicionan el diseño: gnu++23, sin excepciones, sin RTTI, sin asignación dinámica en gameplay, header-only en lo posible, y composición por tipos (el linker decide qué código viaja al binario).
> Este documento es también una **lista de deseos**: incorpora capas para audio, periféricos, ciclo de vida de efectos y familias de efectos gráficos (inspiradas en un repositorio de demoscene Amiga). No todo estará implementado en la primera iteración; el orden está en el roadmap.

---

## 1. Objetivos

- **Portabilidad**: un mismo juego debe poder generarse para Amiga OCS/AGA, Atari ST, Megadrive y Neo-Geo (esta última con concesiones por no tener acceso directo al framebuffer).
- **Composición estática**: las técnicas que no se usan no deben existir en el binario final; la selección la decide el compilador/linker (no `#ifdef` muertos). Sin DLL ni plugins dinámicos.
- **Detección prematura de errores**: lo que es estático se valida en compilación (`static_assert`); lo dinámico se advierte en runtime con severidad y es visible para el desarrollador y para la IA (canal lateral).
- **Soporte a juegos y efectos de referencia**: scroll 8-way (corkscrew), fondos por planos independientes (RoboCod: 5 planos sin DPF con plano de fondo animado), DPF + sprites hardware reprogramados a mitad de frame (Jim Powers, Risky Woods), playfields virtuales en Fast RAM, música y SFX, input (teclado/ratón/joystick), GUI (botones, cajas, popups, puntero de ratón) y la familia de efectos de la demoscene (plasma, fuego, floor, wireframe, texturas 3D, blur, color cycling, copper-chunky…).

---

## 2. Visión general: el núcleo de cinco capas

```
        ┌─────────────────────────────────────────────────────────────┐
        │                        Scene (composición)                  │
        │   playfields + scroll engines + surfaces + sprites + copper │
        └──────┬──────────┬───────────┬───────────┬───────────────────┘
               │          │           │           │
        ┌──────▼───┐ ┌────▼─────┐ ┌───▼─────┐ ┌──▼─────────┐
        │ Scroll   │ │ Playfield│ │ Surface │ │ Sprite     │
        │ Engine   │ │ (display │ │ (dibujo │ │ Manager<N> │
        │ (algorit.│ │  + mapeo)│ │  + clip)│ │            │
        └──────┬───┘ └────┬─────┘ └───┬─────┘ └────────────┘
               │          │           │
        ┌──────▼──────────▼───────────▼──────────────────────┐
        │                    Bitmap (memoria)                 │
        │   Chip RAM / Fast RAM · interleaved / separate      │
        └────────────────────────────────────────────────────┘
```

| Capa | Qué es | Responde a |
|---|---|---|
| `Bitmap` | memoria Chip/Fast + layout (interleaved/separate) + planos | ¿dónde vive la memoria? |
| `Playfield` | un `Bitmap` + mapeo lógico→físico + config de display + `hardware_view()` | ¿cómo se muestra / a qué dirección física va un píxel lógico? |
| `Surface` | subregión rectangular (origen + tamaño + clip) sobre un playfield, con las primitivas de dibujo | ¿dónde dibujo y qué recorto? |
| `ScrollEngine` | algoritmo de scroll por tiles (mapa + tileset + modo) que mueve la cámara y emite blits | ¿cómo scrollea este playfield? |
| `Scene` | composición: playfields, scroll engines, surfaces, sprites, paleta, zonas, copper | ¿qué hay en pantalla y cómo se combina? |

Regla central: **`Playfield` no dibuja** (hardware + mapeo); **`Surface` es el único contexto de dibujo** (con clip); **`ScrollEngine` es un strategy separado** (no un tipo de playfield); **`Scene` compone** piezas independientes (no un struct de configuración monolítico).

Sobre este núcleo se montan capas de sistema y extensiones: el **ciclo de vida de efecto** (quién orquesta Render/VBlank), **audio**, **input**, y las **familias de efectos gráficos** (chunky, paleta, copper por línea, 3D, filtros, texto). Todas son componentes estáticos adicionales, no parte del núcleo de dibujo.

---

## 3. Modelo de propiedad: `Owner` / `Ref` / `Span`

La API pública no expone punteros crudos (`T*`, `T&`). Se usan:
- `eng::Ref<T>` — referencia no-propietaria, no-nula, comprobada (rol de `observer_ptr`/`gsl::not_null`). Para relaciones (`Surface→Bitmap`, `Playfield→Bitmap`, `FramePlan→tabla`).
- `eng::Owner<T>` — propiedad exclusiva, no-copia, movible, **respaldada por arena o valor** (sin `operator new` en gameplay). Para componentes que poseen a otros.
- `eng::Span<T>` — rango contiguo (ya existe) para paletas, tilesets, datos de sprite, mapas, muestras de audio y payloads.

Virtudes:
- API sin punteros crudos; el ownership está explícito; no hay fugas ni `delete` a mano; `Ref` no-nula elimina la familia de bugs de punteros colgantes en las relaciones entre capas.
- Respeta la regla "sin heap en gameplay": `Owner` vive en una arena de largo plazo (init) o como miembro de valor.

Defectos:
- `Ref`/`Owner` son tipos propios (no `std::unique_ptr`): hay que mantenerlos y documentarlos; el ecosistema estándar espera `std::` y puede haber fricción de estilo si se integra código externo.
- `Ref` no-nula con comprobación en runtime añade una rama por acceso (mínima en Amiga, pero existe); si se abusa, cuesta ciclos.
- La arena única de composición obliga a pensar en "quién construye y en qué orden" en init; no hay destrucción ordenada por el RAII estándar.

---

## 4. `Bitmap` y dominios de memoria (Chip / Fast)

El Blitter solo alcanza Chip RAM; la CPU alcanza ambas. `BitmapConfig` declara `MemoryDomain { Chip, Fast, Any }` y `Bitmap` expone `blitter_accessible()`.

```
   ┌─────────────── Chip RAM ───────────────┐   ┌─────────── Fast RAM ───────────┐
   │  Bitmap A (display)    │  Blitter ✓    │   │  Bitmap V (virtual) │ Blitter ✗ │
   │  interleaved 4 planos │  CPU ✓        │   │  separate 4 planos  │ CPU ✓    │
   │  BPLxPT -> copper     │               │   │  (solo CPU/software)│          │
   └───────────────────────┴───────────────┘   └──────────────────────┴──────────┘
                          ▲                                        │
                          │   composite(dirty_rect)                │
                          └─────────────── Blitter │ CPU ◄─────────┘
                             (src Chip -> Blitter; src Fast -> software_copy)
```

- Un playfield **virtual** vive en Fast RAM: se dibuja por CPU (o con blits si el destino fuese Chip) y **no se puede mostrar directamente** (`hardware_view()` inválida).
- La `Scene` ofrece `composite(src, dst, region, plan)`: copia una subregión (dirty rect) del virtual al playfield mostrado. El backend ejecuta `blit_copy` si el origen es Chip, o un comando `software_copy` (CPU) si cruza dominios.
- La misma regla sirve para buffers de audio y de trabajo en general: los datos que consume el DMA (bitplanes, muestras de audio, datos de sprite) viven en Chip; lo que solo toca la CPU puede vivir en Fast.

Virtudes:
- Permite jugar con la memoria: Fast RAM es abundante y la CPU puede pintar ahí sin competir con el Blitter.
- Los dirty rects hacen que volcar un virtual grande cueste solo lo que cambió.

Defectos:
- El `software_copy` cruza el bus de forma menos eficiente que el Blitter; para pantallas completas puede ser el cuello de botella.
- Doble buffer de memoria (virtual + mostrado) duplica el coste en RAM y en lógica de sincronización.

---

## 5. `Playfield` (display + mapeo, sin primitivas)

Representa "una forma de hardware del framebuffer": un `Bitmap` + el mapeo lógico→físico (el corkscrew tiene walk/costura/espejo; el lienzo plano es identidad; en MD/NG el mapeo es a tiles) + la config de display (viewport, fetch, modulos) + `hardware_view()` para el copper.

```
   mundo lógico (wx, wy)                 memoria física (byte)
   ┌────────────────────┐   mapeo    ┌────────────────────────────────┐
   │  tile  tile  tile   │  ───────► │ planelínea = (wy%DH)*planes+p  │
   │  tile  tile  tile   │  walk X   │ byte = wx/8  (cruza planelínea)│
   │  tile  tile  tile   │  costura  │ espejo (lineal) +DH*planes     │
   └────────────────────┘            └────────────────────────────────┘
        world row 20                      planeline 80 = fila 20, plano 0
```

Virtudes:
- Separa el hardware (memoria + dirección física) de la lógica de dibujo; un playfield es agnóstico de quién pinta.
- El mapeo es una policy del backend/layout: el mismo concepto sirve para Amiga (interleaved+walk), ST (planar), MD/NG (tilemap).

Defectos:
- El mapeo corkscrew es complejo y está acoplado al layout interleaved; extraerlo como policy obliga a que cada backend implemente su propia versión.
- Sin primitivas en `Playfield`, hay una indirección más (todo dibujo pasa por `Surface`); para scripts pequeños puede parecer burocrático.

### 5.1 Política de API segura: "frontera con `Span`, núcleo crudo"

La API pública de dibujo NO expone punteros crudos del framebuffer (`u8*`/`const u8*`): invitan a aritmética de punteros sin tamaño y rompen el invariante "todo se dibuja por primitivas que validan". La política es la misma que en Rust (`&[u8]`/`&mut [u8]`): **el tamaño viaja con la referencia**.

```
   frontera pública (safe)                 núcleo interno (optimizado)
   ┌───────────────────────────┐           ┌───────────────────────────────┐
   │ Bitmap::bytes() -> Span   │           │ u8* m_frontbuffer (Playfield) │
   │ blit(..., Span<u16> src)  │  in-app   │ BlitJob { const u16* src }    │
   │ SpriteManager::sprite()   │ ───────►  │ CopperBuilder (BPLxPT crudos) │
   │   -> Span<u8>             │           │ amiga_minimal (direcciones)   │
   │ Surface primitivas ->bool │           │ (invariantes conocidos +      │
   │   (validan y recortan)    │           │  documentados en el código)   │
   └───────────────────────────┘           └───────────────────────────────┘
```

Reglas concretas:
- `Bitmap`/`Playfield`/`XLimitedPlayfield` no tienen `frontbuffer()` público; solo `Span` (`bytes()`, `sprite_data()`) con su tamaño como contrato.
- Los blits reciben `Span<const u16>` para fuente y máscara; el playfield valida `src.size()` contra `src_plane_stride*planes` y devuelve `false` si el origen no cubre lo pedido (sin UB).
- `SpriteConfig::data` es un `Span<const u16>` y `set()` descarta el sprite si la DATA no cubre `height*width_words*2` words.
- Los punteros crudos quedan SOLO dentro del engine (núcleo): `BlitJob`, `CopperBuilder`, backend. Ahí los invariantes (Chip RAM, alineación, layout) son conocidos y están documentados, y el chipset Amiga exige direcciones físicas.
- No es "C con clases": no se envuelve cada buffer en un blob opaco; un `Span` es un buffer con su tamaño y las primitivas devuelven `bool` + `constexpr`. El anti-patrón a evitar es el setter por doquier que ahoga los casos legítimos de contenido generado por CPU.

---

## 6. `Surface` (contexto de dibujo con clip)

Subregión rectangular sobre un playfield: `SurfaceConfig { Ref<Playfield> target; Point origin; Size size; Rect clip; }`. Primitivas `set_pixel`, `fill_rect`, `draw_line`, `blit`, `blit_masked` — todas recortadas contra `clip` y enrutadas por el mapeo del playfield.

> **Estado (M1a, implementado)**: `engine/include/eng/field/surface.hpp` con `Surface` (tipo valor, `Playfield*` no-propietario por ahora; `Ref` pendiente) + `SurfaceRect`; las primitivas CPU salieron de `Playfield` (que conserva `write_pixel`, el atómico del mapeo). La demo 107 dibuja el HUD, el FG lienzo y los píxeles fijos vía `scene.bg_surface()`/`hud_surface()`/`canvas_fg_surface()`; regresión verde.

```
   Playfield (mundo)                          Surface (botón)
   ┌──────────────────────────────────┐      ┌──────────────┐
   │                                  │      │  clip        │
   │        ┌──────────────────┐      │      │  ┌────────┐  │
   │        │  Surface (HUD)   │      │      │  │  draw  │  │
   │        │  ┌────────────┐  │      │      │  │  aquí  │  │
   │        │  │  widget    │  │      │      │  │        │  │
   │        │  │  (botón)   │  │      │      │  └────────┘  │
   │        │  └────────────┘  │      │      └──────────────┘
   │        └──────────────────┘      │      hit_test(punto) -> rect
   └──────────────────────────────────┘
```

Es la base del GUI futuro: un `Widget` es una `Surface` + `draw()` + `hit_test(punto)`; un panel/popup es un árbol de `Surface`s anidadas con clips; el puntero de ratón es un sprite + posición de input. `Surface` puede ser planar (núcleo) o **chunky** (ver §16.1) según el `PixelFormat`.

Virtudes:
- El clip hace que dibujar un widget no salga de su rect: el GUI se construye sin lógica de recorte a mano.
- Un contexto de dibujo con coordenadas propias desacopla el código de juego del layout físico exacto.

Defectos:
- La indirección superficie→mapeo→byte físico añade coste por píxel en `set_pixel`; para bulk drawing hay que usar `blit`.
- El recorte por píxel en `fill_rect`/`draw_line` puede ser lento con clips pequeños; un recorte por bloque antes de iterar es la optimización esperada.

---

## 7. `ScrollEngine` (algoritmo, separado)

`ScrollEngine` es un strategy: dado un playfield con el layout apropiado, calcula la cámara (mapposx/y, display_offset, split) y emite los blits de tiles al `FramePlan`. `ScrollMode` especializa: corkscrew 8-way, X-only (sin split), V-only, 1-dir (sin saveword), BPLCON1 simple.

**Estado actual (2026-08-31)**: implementado completo. `engine/include/eng/field/scroll_engine.hpp` posee el `ScrollState` (cámara + dirección) y el ALGORITMO de los 4 scrollear (`scroll_right/left/up/down`), consumido por el playfield vía el concepto `ScrollSink` (template, sin virtuals): geometría, límites de mapa, `add_draw` y la costura `save_word`/`restore_saveword` (que es un seam de LAYOUT, viven en el playfield; el engine decide cuándo restaurar según `previous_xdirection`). El playfield delega con `m_scroll.scroll_right(plan, *this)`. La contracción X-only/V-only/OneDirection se resuelve en el `ScrollMode` del sink.

```
   ScrollEngine<Mode>                    FramePlan
   ┌──────────────────────────┐          ┌──────────────────────────────┐
   │  mapa + tileset + cámara │  emite   │  blit tile (x, y, mapx, mapy)│
   │  mapposx/mapposy         │ ───────► │  blit fillup (post-incremento)│
   │  display_offset / split  │          └──────────────────────────────┘
   │  ScrollView -> copper    │                └─► Playfield (destino)
   └──────────────────────────┘
```

Virtudes:
- El algoritmo es reemplazable y testeable en host sin hardware (ya se hace con `verify-corkscrew.mjs`).
- Un playfield puede no tener scroll (canvas estático) o tenerlo (BG corkscrew); la escena no distingue.

Defectos:
- El corkscrew y su layout están fuertemente acoplados (banda de staging, walk); separarlos requiere que el layout sea configurable.
- Varios `ScrollMode` significan varias rutas de test.

---

## 8. `FramePlan` (buffer de comandos de render, ligado a la escena)

Distintos módulos (scroll, surfaces, sprites, gameplay) hacen sus propias peticiones al mismo buffer, con presupuestos y trazabilidad por sección:

```cpp
struct RenderCommand {
  enum class Kind : u8 { BlitCopy, BlitMasked, SoftwareCopy, TileUpdate, PalettePatch, SetDisplay };
  Kind kind; u16 section; u16 words;   // payload con Span/Ref
};
class FramePlan { /* add_copy/add_masked/add_software_copy/add_tile_update/add_palette_patch; ok() */ };
```

```
   módulos                      FramePlan (poseído por la Scene)            Backend
   ┌─────────────┐   append   ┌────────────────────────────────────┐   ┌──────────────┐
   │ ScrollEngine│ ─────────► │ [BlitCopy | sec=0 | words=12]      │   │ Amiga: Blitter│
   │ Surface/HUD │ ─────────► │ [BlitMasked| sec=1 | words=24]     │──►│ ST: CPU/move  │
   │ SpriteMgr   │ ─────────► │ [SoftwareCopy | sec=2 | words=8]   │   │ MD: VRAM      │
   │ Gameplay    │ ─────────► │ [PalettePatch | sec=3 | words=2]   │   │ NG: planos    │
   └─────────────┘            │  budget global + por sección · ok()│   └──────────────┘
                              └────────────────────────────────────┘
```

La `Scene` posee el `FramePlan` (`scene.plan()`); cada módulo appende con su `section` (diagnóstico + presupuesto por módulo). El backend ejecuta los comandos portables (blit, copia CPU, actualización de tiles, parche de paleta).

Virtudes:
- Una única estructura de datos potente para todos los blits; trazable por módulo y con presupuestos verificables.
- Comandos portables: el mismo plan se traduce a Blitter (Amiga), moves de CPU (ST), VRAM (MD) o planos (NG).

Defectos:
- Un buffer compartido obliga a que la escena arbitre el presupuesto global; degradar/fallar es una política que hay que decidir.
- Los comandos portables abstraen mucho: la traducción por backend es trabajo real y puede perder eficiencia si el comando no captura el detalle del hardware.

---

## 9. `DisplayRequirements` / `CopperBuilder` (arbitraje del copper)

Los módulos **declaran sus necesidades** al `DisplayRequirements` (listas fijas `FixedVector<T, N>`): `PalettePatch` (línea + colores), `SpriteReq` (sprite + línea inicio/fin), `ScrollReq` (plano + offset + líneas), `CopperRule` (`SyncWithBgY`, `PaletteBand`, `PerPlaneScroll`, y **`CopperScript`** de registros por línea). El `CopperBuilder` ensambla las **zonas** verticales y **valida contra el hardware**, produciendo `Warning`s con severidad: raster no esperable (comparador de 8 bits), >8 sprites por línea, conflicto de paleta en la misma línea, mezcla DPF con plano único, coppersky pidiendo sincronizar a una Y inexistente.

```
   raster (líneas del frame PAL)
   ┌─────────────────────────────────────────────┐
   │  41  ┌───────────────────────────────────┐  │
   │      │ ZONA A · main (playfield corkscrew│  │  BPLxPT(display_offset)
   │      │ paleta principal · BPLCON1 scroll │  │
   │  233 ├───────────────────────────────────┤  │  WAIT + DIWSTOP extendido
   │      │ ZONA B · HUD (canvas 4 planos)    │  │  BPLxPT(hud) + paleta HUD
   │      │ (o sprites reprogramados, per-    │  │  per-plane scroll RoboCod
   │      │  plane scroll, coppersky,         │  │
   │      │  CopperScript: BPLCON1 en 150…)   │  │
   │  264 └───────────────────────────────────┘  │
   └─────────────────────────────────────────────┘
   CopperBuilder: valida cada WAIT <= 255 (comparador 8 bits) y cada zona
```

Virtudes:
- Detección prematura: al ser listas fijas con `N` constexpr, la cota de copper words es **exacta** y parte del arbitraje puede hacerse en compilación; lo dinámico se advierte en runtime.
- El modelo de zonas + reglas habilita RoboCod (per-plane scroll), Risky Woods (sprites reprogramados), el coppersky y el floor (BPLCON1 por línea).

Defectos:
- El `CopperBuilder` concentra mucho conocimiento del chipset (comparador de 8 bits, DMACON, zonas): es la pieza más difícil de portar y de testear.
- "Advertir y degradar" es una política que puede enmascarar bugs si la degradación no es visible; la severidad y el reporte deben ser rigurosos.

---

## 10. `ResourceLedger` (asignación estática de recursos)

Cada componente declara su `HwRequirements` (planes, sprites, chip_bytes, copper_words, blitter_words, **audio_channels**, needs_aga) como constexpr. El ledger **reparte** (`allocate_chip<M>(components...)`): ordena por prioridad (**default**: display > sprites > buffers de scroll > copper > **audio**; **override** por componente) y empaqueta en el chip budget de la `MachineProfile`, produciendo un `MemoryPlan` constexpr (offsets + tamaños) con `static_assert` si no cabe.

```
   chip budget de la máquina (ej. A500 = 512 KB)
   ┌──────────────────────────────────────────────────────────┐
   │ [ 0  display bitmap A  ] [ display B ] [ sprites ]       │
   │ [ buffers scroll ][ copper ][ audio (muestras DMA) ]     │
   │  total = plan.total  →  static_assert(total <= chip_ram) │
   └──────────────────────────────────────────────────────────┘
   La Scene hace UNA reserva runtime del total y reparte slices (Ref<Bitmap>)
```

Virtudes:
- Un solo bloque chip en runtime (sin fragmentación), tamaños/offsets conocidos en compilación, y el fallo se detecta al compilar, no al ejecutar.
- El `static_assert` por máquina convierte "no cabe en la A500" en un error de compilación; el presupuesto de canales de audio se valida igual.

Defectos:
- El empaquetado estático requiere geometría constexpr, pero el layout físico final no es determinista → los offsets son relativos al bloque.
- La prioridad default+override puede dar lugar a escenarios donde el juego fuerza un orden subóptimo sin que el ledger lo advierta.

---

## 11. `MachineProfile` + `CONFIG_ID` (perfil de máquina y build)

Cada máquina declara capacidades constexpr: `chip_ram`, `max_planes`, `max_colors`, `max_sprites`, `aga`, `direct_framebuffer`, `tile_planes`, `copper_raster_max`, `max_copper_words`, `max_blitter_words_per_frame`, **`audio_channels`**, **`input_devices`**, **`chunky_supported`**, `cpu` (`CpuClass`), `chip_bus_bits`. Perfiles: A500, A500_ECS, A1200, AtariST, Megadrive, NeoGeo.

La build elige el perfil con `-DTARGET_MACHINE=A500` (macro de build). **La macro también determina el `CONFIG_ID`** que nombra el ejecutable y aísla todos los artefactos, de forma que distintas configuraciones conviven sin pisarse:

```
CONFIG_ID = <MACHINE>_<flags>_<modo>      # ej. A500_hud_debug, A1200_dpf_release
out/build/<demo>/<CONFIG_ID>/             # .o intermedios
out/demos/<demo>/<CONFIG_ID>/<demo>.<CONFIG_ID>.exe
out/run/<demo>/<CONFIG_ID>/               # runner.uae, report, sequence/
```

Virtudes:
- Un `CONFIG_ID` = un binario = una validación estática; A500 y A1200 (o debug/release) coexisten sin conflicto de archivos intermedios.
- La macro es simple y encaja con "el linker decide": se enlaza solo el perfil objetivo.

Defectos:
- Las macros de build son menos "puras" que una selección por tipo; el token del `CONFIG_ID` hay que generarlo canónicamente y mantenerlo consistente entre build/run/analyze.
- Cada máquina nueva añade un perfil y una rama de regresión.

---

## 12. `Diagnostics` (traza con severidad, visible para dev e IA)

`Severity { Info, Warning, Error }`; la escena posee un `Diagnostics` que rutea los mensajes al periférico de depuración (0xB70000) **y al canal lateral (2346)**, de modo que los leen tanto el tooling del desarrollador como la IA. Reporta: presupuestos del `FramePlan`, warnings del `CopperBuilder`, estado de calibración de la tabla CPU/Blitter, **y telemetría de audio/input/efectos**.

```
   juego ──► Diagnostics.report(severity, msg, module)
                 │
                 ├─► periférico 0xB70000 (consola in-Amiga)
                 └─► canal lateral 2346  ──► tooling del dev  ──►  IA
```

Virtudes:
- Los mensajes del juego son observables sin tocar el juego (canal lateral) — es el mecanismo "visible por la IA" para depuración asistida.
- La severidad permite distinguir info, warning y error; el profiler de efectos (`PROFILE`) y los replayers (voz, canal) reportan por aquí.

Defectos:
- El formateo de mensajes cuesta ciclos y bytes; hay que decidir qué niveles viajan a release.
- Una abstracción de trazas puede crecer en exceso (sinks, filtros, categorías); hay que mantenerla mínima.

---

## 13. Ciclo de vida y sistema (Effect, VBlankBus, Scheduler)

Inspirado en el modelo `EffectT` de la demoscene: un efecto (o modo de escena) tiene **Load, UnLoad, Init, Kill, Render y VBlank**. Es la columna vertebral: casi todo (música, paleta, sprites, efectos) cuelga del ciclo Render/VBlank.

```
   Load (background) ──► Init ──► [ Render cada frame ] ──► Kill ──► UnLoad
                                  [ VBlank (ISR)      ]
   Load puede precalcular tablas/paletas/datos mientras OTRO efecto corre
```

- `Effect`/`SceneMode`: la `Scene` hostea **un efecto activo** (Loader → título → gameplay → créditos). `Load` precalcula en background (tarea propia); `Init` reserva memoria/copper/DMA; `Render` dibuja el frame; `VBlank` actualiza punteros, paleta, música; `Kill`/`UnLoad` liberan.
- `VBlankBus`: el VBlank es un evento con **múltiples suscriptores** registrables (música, animación de paleta, sprites, efecto). El bucle actual (`update→wait_vblank→render`) gana hooks de VBlank.
- `HardwareIRQBus`: el sistema no es solo VBlank. Hay que registrar hooks para **todas las interrupciones relevantes**: VBlank (nivel 3), **audio** (vector `$70`, nivel 4 — lo usa un mixer por software para mezclar un chunk por tick), y CIA timers. El `Scheduler` distingue VBlank (música, paleta) de audio (mixer) y de timers.
- `Scheduler`: gestión de tareas de background (Load/precalc), reparto de CPU entre ellas y el Render, y encaje con el `MachineProfile` (velocidad de CPU). Con `MULTITASK`, `TaskWaitVBlank()` cede CPU; sin multitarea es el VBlank síncrono.
- `Profiler`: el perfilado por bloques (`PROFILE`) alimenta el `Diagnostics` para medir el coste de cada efecto.

Virtudes:
- El ciclo de vida unifica la demoscene y el juego: cada "modo" es un `Effect`, con recursos que se liberan al salir (sin fugas entre pantallas).
- La carga en background oculta el tiempo de preparación (tablas, paletas, mallas) detrás del efecto en pantalla.
- El `VBlankBus` desacopla a los suscriptores (música, paleta, efecto) de la secuencia exacta del frame.

Defectos:
- El ciclo de vida añade estados (Load/Init/Running/Kill) y transiciones que hay que gestionar y testear (fallos de Init, Load cancelado).
- La multitarea de background es delicada en 68000 (compilación por registros, stacks propios); hay que medir si compensa frente al precalc síncrono.
- Múltiples suscriptores al VBlank compiten por el tiempo de la ISR; hay que presupuestar los ticks.

---

## 14. Audio

La familia de reproductores de la demoscene (Protracker, AHX, P61, Cinter) es el gap principal. El diseño:

- `AudioBackend`: hardware de sonido (Paula: 4 canales DMA, registros AUDx/volumen/periodo; ST: 3 canales; MD: FM+PSG; NG: canales propios). Traduce comandos portables a registros.
- `MusicPlayer` (interfaz: `Play/Stop/Tick(VBlank)/SetPosition/Pause`) con **replayers por formato**, seleccionados por composición estática (`MusicPlayer<Format::Protracker>`, `MusicPlayer<Format::Ahx>`, `MusicPlayer<Format::P61>`, `MusicPlayer<Format::Cinter>`). Cada formato tiene su tabla de instrumentos/muestras.
- `SoundEffect` + `AudioChannelAllocator`: reparte los `audio_channels` de la máquina entre música y SFX (presupuesto por frame, como el `ResourceLedger` pero para canales). `Diagnostics` avisa si un SFX no encuentra canal.
- `Sample` (datos de audio en Chip RAM para DMA) y `AudioData` (formatos convertidos a muestras planas en Load).

```
   MusicPlayer<PT> ──► Paula/4 canales ──► DAC
   MusicPlayer<AHX> ─┘        │
   SoundEffect ───────────────┤  AudioChannelAllocator reparte
   (SFX: disparo, salto…)     │  4 canales (OCS) entre música y SFX
                              ▼
                   VBlankBus.tick() cada frame
```

### 14.1 Mixer por software (caso de estudio: Amiga Audio Mixer V3.7)

Para juegos con más voces que canales hardware se necesita un **mixer por software** (referencia: Jeroen Knoester, "Audio Mixing for Games", proyecto AmigaAudioMixer). Un mixer mezcla N samples por adición en un buffer de salida que se reproduce en un solo canal hardware (o varios), añadiendo latencia de ~1 buffer. El mixer V3.7 es un caso de estudio perfecto de la filosofía del engine: **decide todo en ensamblado** (`mixer_config.i`) y en runtime solo cambia PAL/NTSC, volumen y qué suena.

```
   muestras fuente (Fast RAM, pre-procesadas o no)    buffer doble (Chip RAM)
   ┌────────┐ ┌────────┐ ┌────────┐
   │ SFX 1  │ │ SFX 2  │ │ SFX 3  │  ── mezcla aditiva ──► ┌──────┐ ┌──────┐
   └────────┘ └────────┘ └────────┘   (por chunk, en la    │ buf A│ │ buf B│
     (pueden vivir en Fast; solo el      IRQ de audio $70)  └──────┘ └──────┘
      buffer de salida necesita Chip)            │              │       │
                                    Paula reproduce A mientras B se mezcla (doble buffer)
```

Abstracciones nuevas derivadas del mixer:
- `AudioMixer<Type, HqMode, SwChannels, CpuClass>`: el mezclador por software. Es una plantilla sobre el tipo (`Single`/`Multi`/`MultiPaired`), el modo (`Standard` con muestras pre-procesadas, o `HQ` con limitador + error-feedback), el nº de voces software por canal HW (1–4) y la CPU (`M68000` bucles desenrollados / `M68020` bucle cache-friendly). La mezcla es **aditiva en longwords** (signed 32-bit); HQ satura (`bvs`) con error feedback. Equivale a mapear `mixer_config.i` a parámetros de plantilla C++23.
- `MixBuffer`: el buffer doble de salida en Chip RAM, reservado por el `ResourceLedger` con tamaño dependiente de `MachineProfile` (periodo y PAL/NTSC). Latencia intrínseca de ~1 buffer.
- `AudioChannelPolicy`: el reparto de voces (estado libre/activo/loop, **prioridad + edad** para *channel stealing*, y la regla de que **las voces en loop nunca se sobrescriben**). Extiende el `AudioChannelAllocator`.
- `AudioPlugin` (init + rutina por tick + *deferred*): DSP por sample dentro de la IRQ (repeat, sync, volume, pitch), con `MIX_PLUGIN_STD` (altera el buffer) y `MIX_PLUGIN_NODATA` (solo sincroniza). Es un "shader" de audio por sample.
- `SamplePreprocessor` (división o compresión): parte de la pipeline de recursos (§17), se aplica en Load para dar headroom a la suma (el mixer estándar lo exige; el HQ no).
- `AudioIRQBus`: el mixer **no** se dirige por VBlank sino por la **interrupción de audio** (vector `$70`, nivel 4); el `HardwareIRQBus` (§13) debe soportarlo. La latencia (~1 frame) y el doble buffer son decisiones de diseño documentadas.

Rutinas de referencia del mixer (para mapear): `MixerSetup` (buffer + VBR + PAL/NTSC), `MixerInstallHandler` (vector `$70`), `MixerStart/Stop`, `MixerVolume`, `MixerPlayFX`/`MixerPlayChannelFX` (selección automática por prioridad/edad o canal forzado), `MixerStopFX`, `MixerGetChannelStatus`, callbacks de fin de sample y callbacks externos de IRQ/DMA (integración OS-legal).

Virtudes:
- El mixer es estático por composición: solo se instancia el tipo/modo/nº de voces/CPU que usas (igual que `mixer_config.i` elimina código no usado).
- El triple concepto HW channel / SW channel / voz software da muchas voces (hasta 16) con pocos canales hardware; las muestras fuente pueden vivir en Fast RAM.
- La política de prioridad+edad y el "loop nunca se sobrescribe" son reglas claras y verificables.

Defectos:
- La mezcla aditiva sin saturación (modo estándar) distorsiona si el headroom no se respeta; el pre-procesado lo asegura pero baja volumen/calidad.
- El modo HQ es mucho más caro de CPU (12–18 % del frame en A500); es viable sobre todo en A1200.
- El buffer doble añade latencia de ~1 frame (intrínseca a la técnica); los bucles muy cortos consumen más CPU.

---

## 15. Input y periféricos

- `InputBackend` + `InputManager`: modelo de entrada **normalizado** (botones, ejes, posición) que mapea igual en Amiga (teclado/ratón/joystick/lightpen), ST, MD y NG. El GUI (puntero) y los juegos (joystick) dependen de esto.
- `Widget`/GUI: botones, cajas, popups son `Surface`s con `hit_test`; el puntero es un sprite + posición de input; el `InputManager` reparte eventos por hit-testing y foco.

```
   teclado ─┐
   ratón  ──┼─► InputBackend ──► InputManager (estado normalizado)
   joystick┘                        ├─► Widgets (hit_test, foco)
   lightpen                          └─► Gameplay (ejes, botones)
```

Virtudes:
- Un modelo normalizado hace que el juego no conozca el dispositivo concreto; el backend por máquina mapea.
- El GUI reutiliza la `Surface` (clip) y el sprite del puntero, sin capa nueva.

Defectos:
- Dispositivos con capacidades distintas (lightpen sin botones, joystick sin ratón) requieren que el modelo normalizado tenga "no disponible" y que la escena lo advierta.
- El polling de ratón en Amiga (control del CIAA) y su conversión a posición por frame es código de bajo nivel que hay que portar por plataforma.

---

## 16. Efectos gráficos (familias)

### 16.1 Pipeline chunky (c2p)
Plasma, fire-RGB y twister usan un buffer chunky (píxeles consecutivos, `PM_CMAP4/8/RGB12`) y conversión a planar. Abstracción: `Pixmap`/chunky `Surface` con su `PixelFormat`, y `ChunkyToPlanar` (CPU, blitter en pasadas, o tablas dual). Es la primera concesión a "formato de píxel por superficie": el `Surface` pasa de ser solo planar a tener un formato.

```
   buffer chunky (píxeles consecutivos)          bitplanes (planar)
   ┌──────────────────────────┐   c2p (CPU/    ┌──────────────────────────┐
   │  AB CD EF … (un byte px) │   blitter,     │  plano0 │ plano1 │ plano2│
   │  cálculo de plasma/fuego │   dualtab)     │  reordenación de words  │
   └──────────────────────────┘                └──────────────────────────┘
```

Virtudes:
- Desacopla el cálculo (fácil en chunky) del display (planar); es la base de fuego/plasma/efectos de píxel.
- El conversor es portable (CPU en ST, VRAM en MD si hay modo planificado).

Defectos:
- La conversión chunky→planar cuesta CPU/Blitter por frame; para resoluciones altas es el cuello de botella.
- El modo **copper-chunky** (plasma 8×4) es un display especial del Copper que el `CopperBuilder` debe poder emitir (§16.8).

### 16.2 Animación de paleta
Color-cycling, PCHG (por línea), neons (rotación en VBlank). `PaletteAnimator` (ciclo, rotación, desplazamiento, PCHG) que emite `PalettePatch` al `CopperBuilder` cada frame y se suscribe al `VBlankBus`.

Virtudes:
- El animador es declarativo (rangos de la paleta que ciclan, velocidad) y reutilizable.
- La emisión por `PalettePatch` pasa por el `CopperBuilder`, que valida conflictos de línea.

Defectos:
- El cycling por línea (PCHG) consume words de copper y puede chocar con otras zonas; hay que validar.
- La rotación en VBlank depende del tick exacto (latencia de una línea posible).

### 16.3 CopperScript (registros por línea)
Floor (BPLCON1 por línea), stripes, multipipe escriben registros en scanlines concretas. Abstracción: **programa de Copper declarativo** — `CopperRule{ CopperScript }`: una secuencia de `{ WAIT línea, MOVE registro, valor }` validada por el `CopperBuilder` (raster 8-bit, solapamiento con zonas). Es la generalización de `PerPlaneScroll` y `PaletteBand`.

```
   CopperScript (declarativo)
   { línea 120: BPLCON1 = 0x11 }
   { línea 130: BPLCON1 = 0x22 }
   { línea 140: BPLCON1 = 0x33 }   ← floor / stripes
   CopperBuilder: valida cada línea <= 255 y que no pise otras zonas
```

Virtudes:
- Un DSL pequeño cubre una familia entera de efectos de copper por línea con la validación centralizada.
- El `CopperBuilder` arbitra solapamientos entre el script y las zonas de playfield/sprites.

Defectos:
- La validación de solapamientos entre un script arbitrario y las zonas puede ser compleja (o conservadora).
- El coste en words crece con el número de líneas; hay que presupuestar.

### 16.4 3D (gfx3d)
Wireframe, flatshade, textura, UV, bobs3d. Capa de matemáticas fixed-point + `Mesh`/`Object3D` + `Camera/Proyección` + `Rasterizer` (arista, plano, texturizado) que rasteriza en `Surface`/`FramePlan`. La lib3d de la demoscene es la referencia (back-face culling, InvSqrt, SortFaces, painter's algorithm).

```
   Mesh3D ──► Object3D ──► Transform3D (objectToWorld) ──► Proyección ──► 2D
                                │                              │
                        UpdateFaceVisibility (culling+luz)   SortFaces (Z)
                                │                              │
                                └──────────► Rasterizer (wireframe | flatshade | textura)
                                                  │
                                                  ▼
                                          Surface / FramePlan
```

Virtudes:
- Fixed-point y tablas (sin floats) encajan con el no-heap y el 68000; el pipeline es componible por piezas.
- El `Rasterizer` es una plantilla por modo (solo se instancia el que usas).

Defectos:
- Es un módulo grande (matrices, mallas, proyección, ordenación, rasterizadores) con muchas rutas de test.
- El texturizado y el shading por cara consumen mucho; el presupuesto por frame hay que medirlo con el profiler.

### 16.5 Filtros / post-procesado
Blurred, darkroom, glitch, magnifying-glass (lente). `FilterChain` sobre surfaces/framebuffer: blur, distorsión, desplazamiento, contraste — el equivalente a un pipeline de shaders 2D. Cada filtro es una plantilla (`Filter<Blur>`, `Filter<Lens>`) que opera sobre un `Bitmap`/`Surface` y emite `blit_copy`/`software_copy`.

Virtudes:
- Los filtros se componen en cadena y son reutilizables entre efectos.
- Operan sobre el `FramePlan` (blit o software según dominio), sin tocar el backend.

Defectos:
- El blur y la distorsión por píxel son caros; hay que decidir resolución de trabajo y presupuesto.
- La cadena de filtros añade latencia de un frame si es multi-pase.

### 16.6 Fuentes y texto
Credits, textscroll. `Font` (bitmap, proporcional) + `TextRenderer` (scroll horizontal/vertical, parpadeo, alineación) dibujando en `Surface`. El texto es la base de HUD, menús, créditos y el propio GUI.

Virtudes:
- El texto reutiliza `Surface` (clip) y `blit`/`software_copy`; no hay capa especial de display.
- Un `Font` es un `Bitmap` de tiles (reutiliza el banco de tiles del `ScrollEngine`).

Defectos:
- Las fuentes proporcionales y la justificación añaden lógica de layout que hay que testear.
- El scroll de texto por línea (efecto clásico) es un caso del `ScrollEngine` con fuente de tiles.

### 16.7 Tiles avanzados
Tiles8/16, tilezoomer (twist, zoom por tile). Más allá del `ScrollEngine`: manipulación de tiles individuales (rotar, voltear, zoom, paleta por tile). Abstracción: `TileMap` con atributos por tile (flip, paleta, animación) y un `TileTransform` que re-encaja los tiles antes de mostrarlos.

Virtudes:
- Comparte el banco de tiles y la geometría con el `ScrollEngine`.
- Los atributos por tile (flip/paleta) son el paso natural hacia plataformas tipo MD/NG (tilemap hardware).

Defectos:
- El zoom por tile y el twist son costosos si no los hace el hardware (MD/NG sí, Amiga no).

### 16.8 Copper-chunky
Plasma 8×4: usar el Copper para mostrar datos chunky (re-encaje de palabras por línea). Es un modo de display especial: el `CopperBuilder` debe poder emitirlo (o documentarlo como efecto backend-específico de Amiga). Es la concesión máxima a la técnica: no es portable a ST/MD/NG y se declara en `BackendCapabilities` (`chunky_supported`).

---

## 17. Recursos y loader

Loader (carga de módulos/niveles), conversión de datos (obj2c, png2c, generadores de tablas). Abstracción: `Resource`/`Asset` (datos crudos, paletas, mallas, muestras) + `Loader` en background (tarea del `Scheduler`) + herramientas de conversión en build. El ciclo `Load` del efecto (§13) es el consumidor natural.

```
   asset crudo (en disco/ROM)  ──► Loader (background)  ──► Resource convertido
     .mod/.ahx/.p61/.cinter           │                     (malla, paleta, tablas,
     .obj/.png/.csv (convertidos)     │                      muestras, fuentes)
     en build: obj2c, png2c, gen-*.py └──► Chip/Fast RAM, presupuesto del ledger
```

### 17.1 Datos horneados y contrato binario versionado

El engine debe cargar **niveles y escenas desde archivos distintos al ejecutable** (idea DOOM/WAD y SCUMM). Para eso, una herramienta (el editor UAF u otra) **hornea** cada escena en un **contrato binario versionado** que el runtime interpreta sin reglas implícitas del editor:

```
[HEADER]        magic, versión, flags de modo, nº de params
[PARAM_TABLE]   id (hash), tipo (u8/u16/s16), default, offset_en_workarea
[PALETTE_DATA]  words RGB444 (COLOR00–31)
[COPPER_STATIC] instrucciones copper estáticas (pre-horneadas)
[COPPER_TEMPL]  plantilla de secciones dinámicas + tabla de patches
[BITPLANE_DATA] datos planares (tiles, bitplanes)
[TILEMAP_PF1]   mapa de tiles PF1 (índices u16, bits 14/15 flip)
[TILEMAP_PF2]   mapa de tiles PF2
[SPRITE_DATA]   datos de sprites (formato hardware)
[SCROLL_META]   tamaños de buffer, offsets, estrategia de blitting
```

Abstracciones del engine: `AssetLoader`/`ResourceManager` (resuelve `LoadAsset("level1_bg")` por id simbólico, carga en Chip/Fast, presupuestado por el ledger), y `SceneAsset` → instancia la composición de la escena (playfields, scroll, sprites, zonas, copper) **a partir de los datos**, no de código.

### 17.2 Streaming desde disquetera (perfil Amiga)

El Amiga puede **leer de disquetera mientras la CPU sigue con la lógica** (DMA trackdisk a Chip RAM + IRQ). Se quiere explícito para el perfil Amiga: `StreamLoader`/`AssetStream` — el editor declara qué assets se stream-ean (el siguiente nivel/escena) y el runtime los lee en background (hook de trackdisk en el `HardwareIRQBus`) mientras el efecto actual corre. El `ResourceLedger` reserva buffers de streaming y el ciclo de vida del efecto (Load en background) encaja con `AssetStream`.

Virtudes:
- La carga en background oculta el coste de conversión y de disco; el `ResourceLedger` reserva el espacio.
- El contrato binario versionado hace que el ejecutable sea un **intérprete de datos**: el mismo exe carga cualquier escena horneada (niveles, aventuras).
- El streaming de disquetera da niveles largos con poca RAM cargando el siguiente mientras se juega el actual.

Defectos:
- La carga en background necesita un sistema de archivos (disco/ROM) y tareas; en cartucho (MD/NG) no hay disco y hay que pre-cargar.
- La conversión en build genera headers que hay que versionar y regenerar; el contrato binario hay que versionarlo y validarlo (tests golden).
- El streaming de disquete es lento (~50 KB/s efectivo) y hay que planificar la carga para no interrumpir el frame; el hook de trackdisk y el doble buffer de streaming añaden complejidad.

---

## 18. Escenas data-driven, `runtime_params` y ScriptVM (aventuras tipo SCUMM)

Objetivo intermedio: una **versión hiper-vitaminada de SCUMM** que ejecute aventuras gráficas montadas con un editor externo. Esto exige dos piezas nuevas que convierten al engine en un intérprete de datos.

### 18.1 Escenas data-driven y `runtime_params`

Una escena horneada declara su **estrategia runtime explícita** (`runtime_scroll_contract`: modelo de scroll/framebuffer; `runtime_feature_contract`: modelo de copper/sprites/objetos) y una **tabla de parámetros runtime** (`runtime_params`): `{ id, type (u8/u16/s16), default, range }` que el gameplay o el script escriben cada frame. El Copper se hornea en `static` / `dynamic_partial` / `dynamic_full`:

- `static`: compilada en el baking, inmutable.
- `dynamic_partial`: el baking exporta una **tabla de patches** `{ copper_list_word_offset, formula, param_ids[] }`; el runtime solo **parchea los words marcados** (sine/linear/table según params) sin regenerar la lista (~100–300 ciclos/frame por efecto en A500).
- `dynamic_full`: se recalcula toda la lista cada frame (no recomendado en A500).

```
   runtime_params (workarea, offsets)        copper list horneada (chip RAM)
   ┌─────────────────────────────┐           ┌───────────────────────────────┐
   │ water_phase (u8, 0-255)     │──patch──►│ [word 12] BPLCON1 = sine(...)  │
   │ water_amplitude (u8, 0-15)  │  tabla    │ [word 40] COLOR20 = linear(..) │
   │ scroll_pf1_x (u16)          │           │ (solo words dynamic_partial)  │
   └─────────────────────────────┘           └───────────────────────────────┘
```

Los `CopperEffect` de alto nivel del editor (ColorGradient, WaterEffect, PaletteSwitch, SpriteReprogram, SineScroll, FloorPerspective, ColorCycle, DisplayWindow…) se hornean a **secuencias** y se traducen a instrucciones; el `CopperBuilder` del engine los consume como `CopperScript`/`CopperRule`. El `SpriteStrip` (multiplexado vertical de sprites, con `copper_reprogram_interval`) genera los reprogram automáticos por línea; los `LogicalBob` (operación booleana `ForceSet/ForceClear/Toggle` sobre bitplanes vía Blitter) se traducen a blits con minterm. Y la escena puede exportar su **presupuesto DMA** (`budget_hint`/`DmaBudgetReport` línea a línea) para que el runtime valide contra el hardware real (integración con `ResourceLedger`/`CopperBuilder`/`Diagnostics`).

### 18.2 ScriptVM (aventuras tipo SCUMM)

Para aventuras gráficas se necesita una **capa de scripting** (bytecode) que dirija la escena, separada del engine y de los datos:

- `ScriptVM`: intérprete de bytecode (máquina de pila, sin heap) para la lógica de la aventura. Los scripts viven en el archivo de datos (no en el exe).
- Modelo de aventura: **rooms** (escenas), **actors** (sprites/bobs), **walkboxes** (regiones convexas por donde camina el actor), **objetos** (interacción), **inventario**, **verbos** (LOOK/USE/PICK UP), **diálogos** (cajas de texto) y **subtítulos**.
- El `ScriptVM` llama a la escena: mover actores, reproducir diálogos (reutiliza `Font`/`TextRenderer` + `Surface`/GUI), transicionar de habitación (el ciclo de vida de `Effect` §13), disparar triggers (los `Trigger` del Object Layer).
- El bytecode es generado por el editor externo (como UAF) y horneado en el archivo de datos; el `ScriptVM` es estático por composición (solo se enlaza el opcode set que uses).

```
   archivo de datos (horneado)                 engine
   ┌──────────────────────────────┐    Load   ┌──────────────────────────┐
   │ rooms[].layers/objetos/params│ ────────► │ Scene (instancia)        │
   │ rooms[].scripts (bytecode)   │ ────────► │ ScriptVM (dirige)        │
   │ actores, walkboxes, verbos   │           │  └─► actores (sprites)   │
   │ diálogos, inventario         │           │  └─► diálogo (Font/Surface│
   └──────────────────────────────┘           │  └─► transición de room  │
                                              └──────────────────────────┘
```

Virtudes:
- El engine es un **intérprete de datos**: el mismo exe ejecuta cualquier aventura montada con el editor, sin recompilar.
- `runtime_params` + parcheo `dynamic_partial` dan efectos de copper reactivos al juego con coste mínimo.
- El `ScriptVM` estático por composición y el modelo de aventura (rooms/actores/walkboxes/verbos) reutilizan todas las capas previas.

Defectos:
- Un bytecode propio es un trabajo grande (compilador/editor + intérprete + depurador) y hay que validarlo en host.
- El parcheo `dynamic_partial` exige que el baking conozca la posición exacta de los words → acoplamiento editor/runtime por contrato versionado.
- El streaming y el scripting compiten por CPU en el frame; hay que presupuestar (profiler) para no romper los 50 Hz.

---

## 19. Blit híbrido CPU/Blitter (selección por CPU)

Basado en la técnica de Jeroen Knoester (Power Programs, "CPU Assisted Blitting"): en máquinas con memoria chip de 32 bits y CPU ≥ 68020 (A1200, CD32, A3000, A4000), el Blitter sin `BLTPRI` deja a la CPU ~1 de cada 4 ciclos (o 1 de 3 con canales B&D), y la CPU (que accede a chip de 32 bits, el doble de eficiente que el Blitter de 16) hace una parte del blit (líneas inferiores) mientras el Blitter hace las superiores. ~13% más de bobs/frame; requiere alineación a longword y objetos ≥ 32 px. Nota clave del autor: **los emuladores sobreestiman el 68020**; hay que calibrar en hardware real.

```
   blit de un bob (líneas)
   ┌───────────────────────┐
   │  líneas superiores    │  ◄── Blitter (16 bits)
   │   (las que dice la    │
   │    tabla de reparto)  │
   ├───────────────────────┤
   │  líneas inferiores    │  ◄── CPU 68020 (32 bits, mientras Blitter corre)
   │   (el resto)          │
   └───────────────────────┘
   tabla: BlitSplitTable<M> = { BlitKind × ancho → { lines_blitter, lines_cpu } }
   if constexpr (cpu_assisted_blit<M>) → Blitter+CPU | else → Blitter puro
```

El diseño es **híbrido en tres capas**:
1. **Default constexpr** (`BlitSplitTable<MachineProfile>`, por `BlitKind` × clase de ancho → `{ lines_blitter, lines_cpu }`). Funciona desde el primer arranque; es la línea base conocida.
2. **Calibración runtime** (build con `-DENABLE_CALIBRATION`): mide con CIA timers el throughput real del reparto y refina una tabla en Fast RAM (no chip). `Diagnostics` avisa si la calibración se hizo en emulador.
3. **Profile bake** (`tools/profile/bake-split-table.sh`): la tabla medida se emite como header con la tabla constexpr (generar el header es trivial: es texto formateado) y se hornea en el build de distribución.

El `FramePlan` referencia la tabla activa con `Ref`; `if constexpr (cpu_assisted_blit<M>)` decide en compilación si la estrategia existe (en A500 no hay tabla que mirar).

Virtudes:
- Una sola fuente (el `FramePlan`) produce código distinto por máquina según la CPU, sin `#ifdef` en el juego.
- El default + calibración + bake cubre desarrollo (rápido), hardware real (preciso) y distribución (sin coste runtime).

Defectos:
- La técnica es delicada (alineación, reparto por anchos, interrupciones); el beneficio real depende de la máquina exacta y del mix de DMA.
- La tabla horneada fija la medición a un hardware: si el juego corre en una variante distinta, la calibración puede suboptimizar; el fallback constexpr siempre está.

---

## 20. Composición estática y selección por linker

El engine es un conjunto de componentes **independientes, header-only y plantilla** (`TileScrollEngine<Mode>`, `SpriteManager<N>`, `Palette<N>`, `MusicPlayer<Format>`, `Filter<Kind>`, `Rasterizer<Mode>`, etc.). La escena del juego es un **agregado** que declara qué usa:

```cpp
struct MiComposicion {
  gfx::Playfield bg;
  gfx::TileScrollEngine<ScrollMode::XLimited8Way> scroll;
  gfx::Surface hud;
  gfx::SpriteManager<4> sprites;
  audio::MusicPlayer<audio::Format::Protracker> music;
  input::InputManager input;
};
using MiEscena = gfx::Scene<MachineProfile<A500>, MiComposicion>;
```

Con `-ffunction-sections -fdata-sections -Wl,--gc-sections`, el linker elimina las secciones no referenciadas: si no usas corkscrew, un replayer o un filtro, su código no viaja, aunque el header exista. Sin RTTI, sin excepciones, sin heap en gameplay → el binario es pequeño y el `--gc-sections` es efectivo. Las macros solo diferencian hardware del chipset, nunca features de juego.

```
   composición del juego                    binario final
   ┌──────────────────────────┐  instancia  ┌──────────────────────────┐
   │  bg: Playfield           │ ──────────► │  [Playfield]             │
   │  scroll: XLimited8Way    │   plantillas│  [ScrollEngine corkscrew]│
   │  hud: Surface            │             │  [SpriteManager<4>]      │
   │  music: Protracker       │             │  [MusicPlayer<PT>]       │
   │  sprites: SpriteManager<4>│            │  (lo que se usa)         │
   └──────────────────────────┘             │  --gc-sections descarta  │
                                            │  lo no referenciado      │
                                            └──────────────────────────┘
```

Virtudes:
- "Lo que no se usa no existe": el tamaño del binario refleja la composición, no la biblioteca.
- La selección de técnica (blit vs CPU, corkscrew vs simple, formato de música, sprites o no) es estática y verificable por el compilador.

Defectos:
- Las plantillas inflan el tiempo de compilación y los mensajes de error.
- Requiere que el toolchain soporte `--gc-sections` de forma fiable y que el código no use registros globales que el linker no pueda descartar.

---

## 21. Portabilidad a Atari ST / Megadrive / Neo-Geo

La frontera es el `FramePlan` (comandos portables) y la interfaz `Backend`:

| Comando portable | Amiga | Atari ST | Megadrive | Neo-Geo |
|---|---|---|---|---|
| `blit_copy` | Blitter | CPU/move (planar) | write VRAM | n/a (sin FB directo) |
| `blit_masked` | Blitter cookie-cut | CPU | write VRAM + paleta | concesión: plano/tile |
| `update_tilemap` | n/a | n/a | VDP tile planes | tile planes |
| `set_display` | Copper | Shifter | VDP registers | video hardware |
| `audio_play` | Paula (4) | YM2149 (3) | YM2612+PSG (6) | canales propios |
| `input_poll` | CIAA (teclado/ratón/joystick) | IKBD/joystick | VDP/ports | ports |

El mapeo lógico→físico del `Playfield` es la policy por backend; `BackendCapabilities` (direct_framebuffer, hardware_sprites, tile_planes, max_planes, max_colors, audio_channels, input_devices, chunky_supported) permite que la lógica se adapte: si `!direct_framebuffer`, una `Surface` degrada a actualización de tiles/planos o a un framebuffer interno pequeño (la concesión de Neo-Geo).

Virtudes:
- El juego es portable por construcción; solo cambia el backend y la policy de mapeo.
- `BackendCapabilities` + `ResourceLedger` detectan en compilación si un feature no existe en la plataforma objetivo.

Defectos:
- Abstraer tanto obliga a que el `FramePlan` sea el mínimo común; las técnicas específicas (per-plane scroll de RoboCod, copper-chunky de Amiga, planos de NG) necesitan comandos o reglas adicionales que engordan el modelo.
- El costo de mantener N backends es real: cada uno tiene su regresión, sus limits y sus bugs de emulador.

---

## 22. Mapeo a juegos y efectos de referencia

- **RoboCod** (5 planos sin DPF, fondo animado de 1 plano): un `Playfield` single de 5 planos; el plano 5 declara un `ScrollReq` con offset propio y el `CopperBuilder` emite los re-apuntados de `BPL5PT` por scanline mientras los 4 planos de primer plano comparten el scroll principal. El fondo animado se dibuja con `Surface`/blits.

```
   RoboCod: 5 planos single, sin DPF
   plano 1..4  ──► scroll principal (BPLCON1 + BPL1..4PT)
   plano 5     ──► ScrollReq (offset propio) ──► BPL5PT re-apuntado por scanline
   ┌──────────────────────────────────────┐
   │ BPL1..4: el nivel se mueve           │
   │ BPL5   : el fondo se mueve distinto  │
   └──────────────────────────────────────┘
```

- **Jim Powers / Risky Woods**: DPF + sprites hardware; el `SpriteManager` reporta `SpriteReq` (reprogramación a mitad de frame) y la escena monta las zonas de sprites y paleta. Los fondos animados usan `Surface` sobre playfield.
- **Scroll vertical con cambios de color por línea**: un módulo declara `PalettePatch` ligado a la Y; el `CopperBuilder` lo valida contra el raster.
- **Coppersky**: `CopperRule { SyncWithBgY }` sincroniza la paleta del cielo con la posición Y del fondo; la escena comprueba que la Y existe y es esperable.
- **Demoscene**: los efectos del catálogo (plasma, fuego, floor, wireframe, texturas, blur, color-cycling, copper-chunky, música) se montan sobre las capas de §13–§17: `Effect` (ciclo), `Pixmap`/c2p (plasma/fuego), `PaletteAnimator` (cycling/neones), `CopperScript` (floor/stripes), `gfx3d` (wireframe/flatshade/UV), `FilterChain` (blur/glitch), `Font`/texto (credits/textscroll), `MusicPlayer` (los 4 formatos) y `InputManager` (kbdtest/GUI).

---

## 23. Resumen: virtudes y defectos por decisión

| Decisión | Virtudes | Defectos |
|---|---|---|
| `Owner`/`Ref`/`Span` | sin punteros crudos; ownership explícito; no-heap | tipos propios; rama por `Ref`; arena única |
| Dominios Chip/Fast + dirty rects | usa Fast RAM; volcado barato | `software_copy` caro; doble memoria |
| `Playfield` sin primitivas | hardware limpio; mapeo portable | indirección; mapeo por backend |
| `Surface` con clip | GUI por construcción; coords propias | costo por píxel; recorte a optimizar |
| `ScrollEngine` separado | testable; reemplazable | layout acoplado; más regresión |
| `FramePlan` ligado a escena | buffer único potente; trazable | arbitraje de presupuesto; traducción por backend |
| `FixedVector` + `CopperBuilder` | cota exacta; detección prematura | mucho conocimiento del chipset; degradar puede enmascarar |
| `ResourceLedger` constexpr | reparto estático; fallo en compilación | offsets relativos; prioridad puede suboptimizar |
| `MachineProfile` + `CONFIG_ID` | un exe = una validación; configs aisladas | macros; token canónico a mantener |
| `Diagnostics` + canal lateral | visible para dev e IA; severidad | coste de formato; política release |
| Ciclo de vida (Effect/VBlankBus) | modos sin fugas; carga en background; hooks desacoplados | estados/transiciones; multitarea delicada; ISR presupuestada |
| Audio (MusicPlayer + allocator) | solo el formato usado; presupuesto de canales | replayers grandes; arbitraje música/SFX dinámico |
| Mixer por software (`AudioMixer<Type,Hq,Cpu>`) | muchas voces (hasta 16) con pocos HW; estático por composición; muestras en Fast | aditivo sin saturación distorsiona; HQ caro; latencia ~1 buffer |
| Contrato horneado + `AssetLoader` | exe = intérprete de datos; niveles/escenas en archivos externos; versionado + tests golden | contrato a versionar/validar; acoplamiento editor/runtime |
| Escenas data-driven + `runtime_params` | efectos de copper reactivos (parcheo `dynamic_partial` ~100–300 ciclos) | baking conoce los offsets de words; acoplamiento |
| `ScriptVM` (aventuras SCUMM) | aventuras montadas con editor sin recompilar; reutiliza capas previas | bytecode propio (editor+intérprete+debug); validación en host |
| Streaming desde disquetera (Amiga) | niveles largos con poca RAM; carga en background sin parar la lógica | disco lento (~50 KB/s); hook de trackdisk; planificación de carga |
| Input/Periféricos | modelo normalizado; GUI reutiliza Surface | dispositivos parciales; bajo nivel por plataforma |
| Chunky/c2p | cálculo fácil; portable | conversión cara; copper-chunky no portable |
| Animación de paleta | declarativo; validado por el builder | PCHG consume words; latencia de tick |
| `CopperScript` | DSL cubre familia de efectos; arbitraje central | solapamientos complejos; words por línea |
| 3D (gfx3d) | fixed-point; pipeline componible | módulo grande; presupuesto a medir |
| Filtros/post-procesado | componibles; sobre el FramePlan | caros; latencia multi-pase |
| Fuentes/texto | reutiliza Surface; font = tiles | layout proporcional; scroll a testear |
| Tiles avanzados | comparte banco; atributos por tile | zoom/twist caros sin HW |
| Blit híbrido CPU/Blitter | selección por CPU; default+calibración+bake | delicado; horneado fija hardware |
| Composición por tipos + `--gc-sections` | binario = composición | compilación más lenta; requiere toolchain |

---

## 24. Roadmap / puntos abiertos

1. **Verificar `--gc-sections`** en el toolchain m68k de la extensión.
2. **Implementar el `CONFIG_ID`** en `build-demo.sh`/`run-demo` (aisla configs y habilita el resto). **HECHO**: CONFIG_ID (MACHINE_flags_modo) nombra el exe y aísla intermedios; run-demo elige configuración; `--gc-sections` ya estaba en el link.
3. **Prototipo host del `ResourceLedger`** (default+override, `static_assert`) con composiciones de ejemplo RoboCod-A500 y DPF-A1200.
4. **Prototipo host del `CopperBuilder` + `Diagnostics`** (zonas, arbitraje, severidad, canal lateral, CopperScript).
5. **Diseñar el comando `BlitAssist`** con la tabla híbrida (default + calibración + bake) como primer backend condicionado por CPU.
6. **Contrato binario horneado + AssetLoader/ResourceManager** (escenas en archivos externos al exe, versionado + tests golden).
7. **Ciclo de vida**: `Effect`/`SceneMode` + `VBlankBus` + `Scheduler` (background Load) — la columna vertebral de la demoscene.
8. **Audio**: `AudioBackend` + `MusicPlayer` (Protracker primero) + `SoundEffect`/allocator de canales + **mixer por software** (`AudioMixer`, buffer doble, política de canales, plugins, `AudioIRQBus`).
9. **Input/Periféricos**: `InputBackend`/`InputManager` (teclado, ratón, joystick) + primer `Widget`/GUI.
10. **Efectos**: pipeline chunky/c2p, `PaletteAnimator`, `CopperScript`, `Font`/texto; luego 3D, filtros y tiles avanzados.
11. **Escenas data-driven + runtime_params + parcheo de copper dynamic_partial** y **ScriptVM** (aventuras tipo SCUMM: rooms, actores, walkboxes, verbos, diálogos).
12. **Streaming desde disquetera** (perfil Amiga): StreamLoader/AssetStream + hook de trackdisk.
13. **Migración de la demo 107** a `Bitmap`/`Surface`/`ScrollEngine` manteniendo la regresión verde como red de seguridad. **Hecho**: M1a (`Surface`), M1b (`Bitmap` en `CanvasPlayfield` y `XLimitedPlayfield`, con offset de fetch 0/16/48 y guardia +64), API segura (§5.1) y M2 completo (`ScrollEngine` = cámara + algoritmo de los 4 scrollear; el playfield es el `ScrollSink` por template: geometría + `add_draw` + costura saveword como seam de layout; sin virtuals en el hot path). **Demo 107 cerrada como MUESTRARIO**: default DPF 3+3 con tiles, FG con la mitad de tiles transparentes (checkerboard sobre tile 0 ⇒ PF1 deja ver PF2), ciclo 8-way de 8 fases (~48 s), sprite hardware + BOB + HUD; la regresión fuerza la línea base single (`K_DUAL=0`, pixel-exacta) y barre direcciones (`K_EFFECT`) en single, debug y release (`analyze-sequence.sh [--release]`, `EFFECT=2/4/7` verdes).

---

## Anexo A. Amiga Audio Mixer V3.7 → abstracciones del engine

Referencia: Jeroen Knoester, "Audio Mixing for Games" (Power Programs) y el proyecto `AmigaAudioMixer` (repositorio local). Este anexo mapea 1:1 los símbolos y conceptos del mixer a las abstracciones propuestas en el engine, como guía de implementación y validación del diseño.

### A.1 Correspondencia de conceptos y símbolos

| Concepto / símbolo del mixer | Abstracción del engine |
|---|---|
| `MXEffect` (length, sample_ptr, loop, priority, loop_offset, plugin_ptr) | `SoundEffect` (con modos de loop `Once/Loop/LoopOffset` y prioridad) |
| `MXMixer` / `MXMixerEntry` / `MXChannel` | `AudioMixer<...>` / `MixBuffer` / voz software |
| `MixerSetup` (buffer + plugin_buffer + VBR + PAL/NTSC) | `AudioMixer::init` + `ResourceLedger` (reserva el buffer) + `MachineProfile` (video system) |
| `MixerInstallHandler` / `MixerRemoveHandler` (vector `$70`, nivel 4) | `AudioIRQBus` (instalación del hook de audio en el `HardwareIRQBus`) |
| `MixerStart` / `MixerStop` | `AudioMixer::start/stop` (programa DMA/INTENA) |
| `MixerVolume` (0–64) | `AudioMixer::set_volume` (volumen HW global) |
| `MixerPlayFX` (selección automática por prioridad/edad) | `AudioChannelPolicy::play` (channel stealing) |
| `MixerPlayChannelFX` (canal forzado) | `AudioChannelPolicy::play_on_channel` |
| `MixerStopFX` (mask de canales) | `AudioChannelPolicy::stop` |
| `MixerGetChannelStatus` → `MIX_CH_FREE/BUSY` | estado de la voz software |
| `MIXER_SINGLE` / `MIXER_MULTI` / `MIXER_MULTI_PAIRED` | parámetro de plantilla `AudioMixerType` |
| `MIXER_HQ_MODE` (0/1) | parámetro de plantilla `AudioMixerMode` (Standard/HQ) |
| `mixer_sw_channels` (1–4) | parámetro de plantilla `SwChannels` |
| `mixer_output_channels` / `mixer_period` / `MIXER_PER_IS_NTSC` | `MachineProfile::audio_channels` + `video_system` (PAL/NTSC) |
| `MIXER_68020` / `MIXER_WORDSIZED` / `MIXER_SIZEX32` / `MIXER_SIZEXBUF` | `CpuClass` + optimizaciones de plantilla (`if constexpr`) |
| `MXPlugin` + plugins (repeat/sync/volume/pitch) | `AudioPlugin` (init + tick + deferred) |
| `ConvertSampleDivide` / `SampleConverter` | `SamplePreprocessor` (división/compresión) en la pipeline de recursos |
| `MixerEnableCallback` (fin de sample, D0/A0, devuelve 0/1) | callback de fin de sample del `AudioMixer` |
| `MixerSetIRQDMACallbacks` (6 punteros externos) | integración OS-legal: hooks del `HardwareIRQBus`/`AudioBackend` |
| `MixerCalcTicks` / `MixerResetCounter` / `mixer_ticks_*` | `Diagnostics`/profiler (medición de la IRQ) |
| `mixer_PAL_buffer_size` / `mixer_NTSC_buffer_size` / doble buffer | `MixBuffer` (tamaño según periodo y PAL/NTSC, latencia ~1 buffer) |

### A.2 Compilación vs runtime (la filosofía del engine, validada)

El mixer decide **en ensamblado** (inmutable en runtime): tipo, modo, canales HW, nº de voces software, periodo, CPU, plugins, optimizaciones y medición. En runtime solo cambian: el sistema de vídeo (PAL/NTSC en `MixerSetup`), el volumen, qué samples suenan y algunos punteros de callback. La doc del mixer lo dice explícitamente: *"la única opción que se puede cambiar en runtime es el sistema de vídeo"*.

Esto es exactamente el modelo del engine: `mixer_config.i` se mapea a **parámetros de plantilla C++23** (`AudioMixer<AudioMixerType::Single, AudioMixerMode::Standard, 4, CpuClass::M68020>`), y el runtime queda para `play/stop/volume/video_system`. Lo que el mixer hace con macros de ensamblado, el engine lo hace con `constexpr`/plantillas y `--gc-sections`.

### A.3 El modelo de canales (HW / SW / voz software)

```
   canal HW Paula (AUDx)              voces software (mixer_sw_channels)      buffer
   ┌────────────────────────┐         ┌────────────────────────────────┐
   │  DMAF_AUD0             │         │  MIX_CH0 ── sample A ──┐       │
   │  ac_ptr/ac_len/ac_per  │◄────────│  MIX_CH1 ── sample B ──┼─mezcla─► │ buf doble
   │  ac_vol                │         │  MIX_CH2 ── sample C ──┘         │ (Chip RAM)
   └────────────────────────┘         │  MIX_CH3 (libre/busy)             │
        (música usa 3 HW;             └────────────────────────────────┘
         el 4º HW reproduce el buffer
         con hasta 4 voces mezcladas)
```

`mixer_total_channels = mixer_sw_channels × mixer_output_count` (máx. 4×4 = 16). Cada voz es una `MXChannel` con `mch_status` (libre/activo/loop), `mch_priority`, `mch_age`. `MIX_CH0..3` son los canales software virtuales (16/32/64/128 en bitmask).

### A.4 La mezcla (aditiva, longwords)

- **Estándar**: suma **aditiva en longwords signed 32-bit** (opera 4 bytes a la vez). Exige muestras **pre-procesadas** (división de amplitud por nº de canales, o compresión con limitador) para que la suma nunca desborde los 8 bits de Paula. Sin saturación: si falta headroom, distorsión.
- **HQ**: suma en 16 bits con **saturación en runtime** (`bvs`) y **error feedback rounding** (los bits perdidos al limitar se suman al siguiente resultado). No necesita pre-procesado ni baja el volumen; mucho más CPU.

```
   estándar (pre-procesado, sin saturar)       HQ (limitador + error feedback)
   out = s1 + s2 + s3 + s4                     acc(16-bit) = s1 + s2 + s3 + s4
   (samples ya divididos/limitados)            if (acc > 127) { out = 127; err += acc-127 }
                                               (los bits sobrantes pasan al siguiente)
```

### A.5 Rendimiento de referencia (del artículo, CIA ticks ≈ 709 kHz)

| Mixer | Canales @ 11 kHz | A500 (% frame) | A1200 (% frame) |
|---|---|---|---|
| Estándar V2.0 | 2 / 3 / 4 | 1,9 / 2,6 / 3,2 | 1,1 / 1,4 / 1,7 |
| Estándar V1.0 | 2 / 3 / 4 | 4,1 / 5,3 / 6,6 | 3,2 / 4,4 / 5,5 |
| HQ | 2 / 3 / 4 | 12,2 / 15,1 / 18,0 | 5,4 / 6,5 / 8,3 |

A 8 kHz, 3 canales estándar: A500 ≈ 2,0 %, A1200 ≈ 1,0 %. Referencia del presupuesto de CPU: el estándar es viable en A500 (≤ 6,6 % con 4 voces); el HQ es realista sobre todo en A1200. La nota del autor (como en el CPU blit assist): **medir en hardware real; los emuladores no son fiables para 68020**.

### A.6 Decisiones de diseño que impone el mixer

- **Latencia**: el doble buffer y el tick por interrupción de audio dan latencia intrínseca de ~1 buffer (1/50 s PAL, 1/60 s NTSC). Es aceptable para juegos y hay que documentarla.
- **IRQ de audio, no VBlank**: el mixer se dirige por el vector `$70` (nivel 4), no por VBlank. El `HardwareIRQBus` (§13) debe distinguir ambas fuentes: VBlank para música/paleta, audio para el mixer.
- **Muestras fuente en Fast RAM**: la mezcla lee los samples desde cualquier RAM; solo el buffer de salida necesita Chip (reservado por el `ResourceLedger`). Encaja con los dominios Chip/Fast (§4).
- **Política de canales**: prioridad + edad para *channel stealing*; **las voces en loop nunca se sobrescriben** (solo `Stop`); `MixerPlayFX` devuelve −1 si no hay canal. La `AudioChannelPolicy` replica esta regla.
- **Tamaño de código**: el autor recomienda eliminar las variantes no usadas (unrolled 68000, HQ, plugins) del build final — el argumento exacto de la composición estática. Con plantillas y `--gc-sections` es automático.
- **Callbacks/plugins**: fin de sample y callbacks externos de IRQ/DMA (OS-legal) como punteros configurables en runtime; plugins (repeat/sync/volume/pitch) como DSP por sample con init/tick/deferred, evitando *race conditions* en la IRQ.

---

## Anexo C. Sevgi Engine → ideas transferibles

Referencia: Sevgi Engine (Alper Sonmez, 2025), motor C para Amiga + editor nativo, basado en ScrollingTricks (el mismo árbol que la demo 107). Su enfoque es complementario al del UAF: **generador de código C boilerplate** (el editor escribe código compilable) frente al **intérprete de datos horneados**. Ambas ideas se integran en el engine: configuración generada como constexpr (Sevgi) + niveles como binario (UAF).

### C.1 Ideas transferibles y su mapeo

| Idea de Sevgi | Mapeo en el engine | Virtud / Defecto |
|---|---|---|
| El editor escribe headers C compilables (`settings.h`/`assets.h`/`palettes.h`) | Configuración generada como **`constexpr`** (validada con `static_assert`); niveles como binario | Virtud: transparente y debuggeable. Defecto: sincronización manual con el motor |
| IDs de función → tablas de punteros con índice 0 = NULL | Serializar comportamiento por **enum + tabla de punteros/switch constexpr** | Virtud: sin punteros ni RTTI en datos serializados. Defecto: el índice 0 se reserva |
| `struct LevelData` como contrato declarativo (editor replica el layout) | Contrato de escena con **`static_assert` de offsets** o generación desde una única fuente | Virtud: simple. Defecto: frágil si se sincroniza a mano |
| **CopOps** (`struct CopOp {wait,size,pointer}`): metadatos reordenables en Fast, instrucciones en Chip | `CopperBuilder`: representar la escena como structs movibles y materializar el buffer final en Chip | Virtud: no ordenar sobre chip RAM. Defecto: dos buffers (control + instrucciones) |
| Scroll en dos partes (`scroll` + `scrollRemaining` al inicio del siguiente frame) | **Colas de trabajo diferidas al siguiente frame** para presupuestar CPU/blitter | Virtud: sin bloqueos. Defecto: complejidad de timing |
| Precálculo `consteval` por configuración (`tilesPerStep`, `posPerStep`, fades, BPLXMOD) | Todo lo derivado de Settings se calcula en compile-time | Virtud: cero coste runtime. Defecto: recompilar al cambiar settings |
| UI declarativa por macros con herencia de layout (`UIOF_INHERIT_*`) y cycle-chain de foco | Árbol de widgets **`consteval`** + layout en compile-time + "métodos" como punteros a función | Virtud: sin `std::function`, navegable por joystick. Defecto: sin reflow dinámico |
| Smart Sprites (multiplexado vertical, `behindList`/`rasterList`, HSN) | Extiende `SpriteManager`: prioridad y solapamiento por línea | Virtud: 8 sprites cubren más pantalla. Defecto: reprogram por línea cuesta copper |
| BOBs beam-chasing (`waitVBeam` antes del blit) | Scheduler de blits: esperar al haz para single-buffer sin tearing | Virtud: sin tearing. Defecto: acopla el blit al raster |
| `ColorTable` de fades no-FPU (punto fijo precalculado, una suma por matiz) | Extiende `PaletteAnimator`: fades de registros/CLP/gradientes | Virtud: barato. Defecto: precisión 8/20 bits a decidir |
| **CLP (CopperList Palette)**: paleta en la copperlist | `CopperBuilder`/`Palette`: paleta por franja, fades independientes, evita glitches de escritura directa | Virtud: robusto en aceleradoras. Defecto: más words de copper |
| `ScrollInfo {up,down,left,right}` + `MAX_SCROLL_SPEED` | La API de scroll pide "cuánto por dirección" y el motor clamp/distribuye | Virtud: API mínima y robusta. Defecto: velocidad acotada |
| "Módulos incluidos textualmente" para el game loop (`#include` de `level_display_loop.c`) | **Evitar**: en C++23 usar un **context object**/lambdas, testable | Defecto de Sevgi: acoplamiento total al `STATIC` |
| "Fatty structs" (campos comunes duplicados por tipo de widget) | **Evitar**: `std::variant` o base + CRTP en C++23 | Defecto de Sevgi: duplicación |
| Paleta como `{num_colors-1, R,G,B,...}` (semántica n-1) | Representación plana de paleta (tamaño en un byte) | Virtud: micro-optimización |

### C.2 La filosofía: generador frente a intérprete

- **UAF** (anexo al §17/§18): el ejecutable es un **intérprete de datos horneados** (escenas, params, scripts). Máxima flexibilidad; requiere un contrato binario versionado.
- **Sevgi**: el editor genera **código compilable** ajustado al juego. Máxima eficiencia y legibilidad; el código generado es parte del proyecto.
- **Síntesis para el engine**: **configuración y contratos como código constexpr** (lo estático, validado por el compilador) + **niveles/escenas como binario horneado** (lo dinámico, cargado en runtime). Así se combina lo mejor de ambos: la validación estática de Sevgi con la carga de datos del UAF.

---

*Borrador técnico en revisión. Ajustable tras la discusión de enlaces externos.*
