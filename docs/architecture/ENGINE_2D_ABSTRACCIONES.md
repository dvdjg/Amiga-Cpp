# Engine 2D — Abstracciones (borrador en revisión)

> **Estado**: BORRADOR en revisión. Puede sufrir cambios menores tras discutir
> enlaces externos (referencias a otros engines y técnicas). Este documento es la
> especificación técnica interna; de aquí se derivará después una descripción de
> alto nivel para el gran público con artículos por tema concreto.
>
> Normas del engine que condicionan el diseño: gnu++23, sin excepciones, sin
> RTTI, sin asignación dinámica en gameplay, header-only en lo posible, y
> composición por tipos (el linker decide qué código viaja al binario).

---

## 1. Objetivos

- **Portabilidad**: un mismo juego debe poder generarse para Amiga OCS/AGA, Atari
  ST, Megadrive y Neo-Geo (esta última con concesiones por no tener acceso
  directo al framebuffer).
- **Composición estática**: las técnicas que no se usan no deben existir en el
  binario final; la selección la decide el compilador/linker (no `#ifdef`
  muertos). Sin DLL ni plugins dinámicos.
- **Detección prematura de errores**: lo que es estático se valida en compilación
  (`static_assert`); lo dinámico se advierte en runtime con severidad y es
  visible para el desarrollador y para la IA (canal lateral).
- **Soporte a juegos de referencia**: scroll 8-way (corkscrew), fondos por planos
  independientes (RoboCod: 5 planos sin DPF con plano de fondo animado), DPF +
  sprites hardware reprogramados a mitad de frame (Jim Powers, Risky Woods),
  playfields virtuales en Fast RAM, y un futuro GUI (botones, cajas, popups,
  puntero de ratón).

---

## 2. Visión general: las cinco capas

| Capa | Qué es | Responde a |
|---|---|---|
| `Bitmap` | memoria Chip/Fast + layout (interleaved/separate) + planos | ¿dónde vive la memoria? |
| `Playfield` | un `Bitmap` + mapeo lógico→físico + config de display + `hardware_view()` | ¿cómo se muestra / a qué dirección física va un píxel lógico? |
| `Surface` | subregión rectangular (origen + tamaño + clip) sobre un playfield, con las primitivas de dibujo | ¿dónde dibujo y qué recorto? |
| `ScrollEngine` | algoritmo de scroll por tiles (mapa + tileset + modo) que mueve la cámara y emite blits | ¿cómo scrollea este playfield? |
| `Scene` | composición: playfields, scroll engines, surfaces, sprites, paleta, zonas, copper | ¿qué hay en pantalla y cómo se combina? |

Regla central: **`Playfield` no dibuja** (hardware + mapeo); **`Surface` es el
único contexto de dibujo** (con clip); **`ScrollEngine` es un strategy separado**
(no un tipo de playfield); **`Scene` compone** piezas independientes (no un
struct de configuración monolítico).

---

## 3. Modelo de propiedad: `Owner` / `Ref` / `Span`

La API pública no expone punteros crudos (`T*`, `T&`). Se usan:

- `eng::Ref<T>` — referencia no-propietaria, no-nula, comprobada (rol de
  `observer_ptr`/`gsl::not_null`). Para relaciones (`Surface→Bitmap`,
  `Playfield→Bitmap`, `FramePlan→tabla`).
- `eng::Owner<T>` — propiedad exclusiva, no-copia, movible, **respaldada por
  arena o valor** (sin `operator new` en gameplay). Para componentes que poseen
  a otros.
- `eng::Span<T>` — rango contiguo (ya existe) para paletas, tilesets, datos de
  sprite, mapas y payloads.

Virtudes:
- API sin punteros crudos; el ownership está explícito; no hay fugas ni `delete`
  a mano; `Ref` no-nula elimina la familia de bugs de punteros colgantes en las
  relaciones entre capas.
- Respeta la regla "sin heap en gameplay": `Owner` vive en una arena de largo
  plazo (init) o como miembro de valor.

Defectos:
- `Ref`/`Owner` son tipos propios (no `std::unique_ptr`): hay que mantenerlos y
  documentarlos; el ecosistema estándar espera `std::` y puede haber fricción de
  estilo si se integra código externo.
- `Ref` no-nula con comprobación en runtime añade una rama por acceso (mínima en
  Amiga, pero existe); si se abusa, cuesta ciclos.
- La arena única de composición obliga a pensar en "quién construye y en qué
  orden" en init; no hay destrucción ordenada por el RAII estándar.

---

## 4. `Bitmap` y dominios de memoria (Chip / Fast)

El Blitter solo alcanza Chip RAM; la CPU alcanza ambas. `BitmapConfig` declara
`MemoryDomain { Chip, Fast, Any }` y `Bitmap` expone `blitter_accessible()`.

- Un playfield **virtual** vive en Fast RAM: se dibuja por CPU (o con blits si el
  destino fuese Chip) y **no se puede mostrar directamente**
  (`hardware_view()` inválida).
- La `Scene` ofrece `composite(src, dst, region, plan)`: copia una subregión
  (dirty rect) del virtual al playfield mostrado. El backend ejecuta `blit_copy`
  si el origen es Chip, o un comando `software_copy` (CPU) si cruza dominios.

Virtudes:
- Permite jugar con la memoria: Fast RAM es abundante (los A500 con expansión) y
  la CPU puede pintar ahí sin competir con el Blitter.
- Los dirty rects hacen que volcar un virtual grande cueste solo lo que cambió.

Defectos:
- El `software_copy` cruza el bus de forma menos eficiente que el Blitter (la CPU
  mueve datos de Fast→Chip); para pantallas completas puede ser el cuello de
  botella.
- Doble buffer de memoria (virtual + mostrado) duplica el coste de la técnica en
  RAM y en lógica de sincronización.

---

## 5. `Playfield` (display + mapeo, sin primitivas)

Representa "una forma de hardware del framebuffer": un `Bitmap` + el mapeo
lógico→físico (el corkscrew tiene walk/costura/espejo; el lienzo plano es
identidad; en MD/NG el mapeo es a tiles) + la config de display (viewport,
fetch, modulos) + `hardware_view()` para el copper.

Virtudes:
- Separa el hardware (memoria + dirección física) de la lógica de dibujo; un
  playfield es agnóstico de quién pinta.
- El mapeo es una policy del backend/layout: el mismo concepto sirve para Amiga
  (interleaved+walk), ST (planar), MD/NG (tilemap).

Defectos:
- El mapeo corkscrew es complejo y está acoplado al layout interleaved; extraerlo
  como policy obliga a que cada backend implemente su propia versión (no se
  reutiliza el mismo código).
- Sin primitivas en `Playfield`, hay una indirección más (todo dibujo pasa por
  `Surface`); para scripts pequeños puede parecer burocrático.

---

## 6. `Surface` (contexto de dibujo con clip)

Subregión rectangular sobre un playfield: `SurfaceConfig { Ref<Playfield> target;
Point origin; Size size; Rect clip; }`. Primitivas `set_pixel`, `fill_rect`,
`draw_line`, `blit`, `blit_masked` — todas recortadas contra `clip` y enrutadas
por el mapeo del playfield.

Es la base del GUI futuro: un `Widget` es una `Surface` + `draw()` +
`hit_test(punto)`; un panel/popup es un árbol de `Surface`s anidadas con clips;
el puntero de ratón es un sprite + posición de input.

Virtudes:
- El clip hace que dibujar un widget no salga de su rect: el GUI se construye sin
  lógica de recorte a mano.
- Un contexto de dibujo con coordenadas propias desacopla el código de juego del
  layout físico exacto.

Defectos:
- La indirección superficie→mapeo→byte físico añade una multiplicación/división
  por píxel en `set_pixel` (cara si se dibujan muchos píxeles por CPU); para
  bulk drawing hay que usar `blit` (que sí es por bloque).
- El recorte por píxel en `fill_rect`/`draw_line` puede ser lento si se dibujan
  rects grandes con clip pequeño; un recorte por bloque (clip del rect antes de
  iterar) es la optimización esperada.

---

## 7. `ScrollEngine` (algoritmo, separado)

`ScrollEngine` es un strategy: dado un playfield con el layout apropiado, calcula
la cámara (mapposx/y, display_offset, split) y emite los blits de tiles al
`FramePlan`. `ScrollMode` especializa: corkscrew 8-way, X-only (sin split),
V-only, 1-dir (sin saveword), BPLCON1 simple.

Virtudes:
- El algoritmo es reemplazable y testeable en host sin hardware (ya se hace con
  `verify-corkscrew.mjs`).
- Un playfield puede no tener scroll (canvas estático) o tenerlo (BG corkscrew);
  la escena no distingue.

Defectos:
- El corkscrew y su layout están fuertemente acoplados (banda de staging, walk);
  separarlos en "algoritmo" y "playfield" requiere que el layout sea
  configurable, y el conocimiento experto del truco queda en el `ScrollEngine`
  (no se comparte con otras técnicas).
- Varios `ScrollMode` significan varias rutas de test (la regresión debe cubrir
  cada modo).

---

## 8. `FramePlan` (buffer de comandos de render, ligado a la escena)

Distintos módulos (scroll, surfaces, sprites, gameplay) hacen sus propias
peticiones al mismo buffer, con presupuestos y trazabilidad por sección:

```cpp
struct RenderCommand {
  enum class Kind : u8 { BlitCopy, BlitMasked, SoftwareCopy, TileUpdate, PalettePatch, SetDisplay };
  Kind kind; u16 section; u16 words;   // payload con Span/Ref
};
class FramePlan { /* add_copy/add_masked/add_software_copy/add_tile_update/add_palette_patch; ok() */ };
```

La `Scene` posee el `FramePlan` (`scene.plan()`); cada módulo appende con su
`section` (diagnóstico + presupuesto por módulo). El backend ejecuta los comandos
portables (blit, copia CPU, actualización de tiles, parche de paleta).

Virtudes:
- Una única estructura de datos potente para todos los blits; trazable por módulo
  y con presupuestos verificables.
- Comandos portables: el mismo plan se traduce a Blitter (Amiga), moves de CPU
  (ST), VRAM (MD) o planos (NG).

Defectos:
- Un buffer compartido obliga a que la escena arbitre el presupuesto global; si
  un módulo excede, degradar/fallar es una política que hay que decidir (no es
  automático).
- Los comandos portables abstraen mucho: la traducción por backend es trabajo
  real y puede perder eficiencia si el comando no captura el detalle del
  hardware (p. ej. canales del Blitter, alineación).

---

## 9. `DisplayRequirements` / `CopperBuilder` (arbitraje del copper)

Los módulos **declaran sus necesidades** al `DisplayRequirements` (listas fijas
`FixedVector<T, N>`): `PalettePatch` (línea + colores), `SpriteReq` (sprite +
línea inicio/fin), `ScrollReq` (plano + offset + líneas), `CopperRule`
(`SyncWithBgY`, `PaletteBand`, `PerPlaneScroll`). El `CopperBuilder` ensambla las
**zonas** verticales y **valida contra el hardware**, produciendo `Warning`s con
severidad: raster no esperable (comparador de 8 bits), >8 sprites por línea,
conflicto de paleta en la misma línea, mezcla DPF con plano único, coppersky
pidiendo sincronizar a una Y inexistente.

Virtudes:
- Detección prematura: al ser listas fijas con `N` constexpr, la cota de copper
  words es **exacta** y parte del arbitraje puede hacerse en compilación; lo
  dinámico se advierte en runtime.
- El modelo de zonas + reglas es lo que habilita RoboCod (per-plane scroll),
  Risky Woods (sprites reprogramados) y el coppersky (sync con la Y del fondo).

Defectos:
- El `CopperBuilder` concentra mucho conocimiento del chipset (comparador de 8
  bits, DMACON, zonas): es la pieza más difícil de portar y de testear.
- "Advertir y degradar" es una política que puede enmascarar bugs si la
  degradación no es visible; la severidad y el reporte deben ser rigurosos.

---

## 10. `ResourceLedger` (asignación estática de recursos)

Cada componente declara su `HwRequirements` (planes, sprites, chip_bytes,
copper_words, blitter_words, needs_aga) como constexpr. El ledger **reparte**
(`allocate_chip<M>(components...)`): ordena por prioridad (**default**:
display > sprites > buffers de scroll > copper; **override** por componente) y
empaqueta en el chip budget de la `MachineProfile`, produciendo un `MemoryPlan`
constexpr (offsets + tamaños) con `static_assert` si no cabe.

Virtudes:
- Un solo bloque chip en runtime (sin fragmentación), tamaños/offsets conocidos
  en compilación (los BPL pointers pueden derivarse), y el fallo se detecta al
  compilar, no al ejecutar.
- El `static_assert` por máquina convierte "no cabe en la A500" en un error de
  compilación.

Defectos:
- El empaquetado estático requiere que la geometría sea constexpr (lo es si la
  composición es un tipo), pero el layout físico final del sistema (dónde cae el
  bloque chip) no es determinista → los offsets son relativos al bloque, no
  absolutos.
- La prioridad default+override puede dar lugar a escenarios donde el juego
  fuerza un orden subóptimo sin que el ledger lo advierta.

---

## 11. `MachineProfile` + `CONFIG_ID` (perfil de máquina y build)

Cada máquina declara capacidades constexpr: `chip_ram`, `max_planes`,
`max_colors`, `max_sprites`, `aga`, `direct_framebuffer`, `tile_planes`,
`copper_raster_max`, `max_copper_words`, `max_blitter_words_per_frame`, `cpu`
(`CpuClass`), `chip_bus_bits`. Perfiles: A500, A500_ECS, A1200, AtariST,
Megadrive, NeoGeo.

La build elige el perfil con `-DTARGET_MACHINE=A500` (macro de build). **La macro
también determina el `CONFIG_ID`** que nombra el ejecutable y aísla todos los
artefactos, de forma que distintas configuraciones conviven sin pisarse:

```
CONFIG_ID = <MACHINE>_<flags>_<modo>      # ej. A500_hud_debug, A1200_dpf_release
out/build/<demo>/<CONFIG_ID>/             # .o intermedios
out/demos/<demo>/<CONFIG_ID>/<demo>.<CONFIG_ID>.exe
out/run/<demo>/<CONFIG_ID>/               # runner.uae, report, sequence/
```

Virtudes:
- Un `CONFIG_ID` = un binario = una validación estática; A500 y A1200 (o
  debug/release) coexisten sin conflicto de archivos intermedios.
- La macro es simple y encaja con "el linker decide": se enlaza solo el perfil
  objetivo.

Defectos:
- Las macros de build son menos "puras" que una selección por tipo; el token del
  `CONFIG_ID` hay que generarlo canónicamente (orden de flags, sanitización) y
  mantenerlo consistente entre build/run/analyze.
- Cada máquina nueva añade un perfil y una rama de regresión.

---

## 12. `Diagnostics` (traza con severidad, visible para dev e IA)

`Severity { Info, Warning, Error }`; la escena posee un `Diagnostics` que rutea
los mensajes al periférico de depuración (0xB70000) **y al canal lateral (2346)**,
de modo que los leen tanto el tooling del desarrollador como la IA. Reporta:
presupuestos del `FramePlan`, warnings del `CopperBuilder`, estado de
calibración de la tabla CPU/Blitter.

Virtudes:
- Los mensajes del juego son observables sin tocar el juego (canal lateral) —
  es el mecanismo "visible por la IA" que necesitamos para depuración asistida.
- La severidad permite que la escena distinga info (calibración sin hacer),
  warning (emulador vs hardware real) y error (conflicto imposible).

Defectos:
- El formateo de mensajes cuesta ciclos y bytes; debe compilarse fuera en
  release si no se quiere (pero entonces se pierde la telemetría del canal
  lateral). Hay que decidir qué niveles viajan a release.
- Una abstracción de trazas puede crecer en exceso (sinks, filtros, categorías);
  hay que mantenerla mínima.

---

## 13. Blit híbrido CPU/Blitter (selección por CPU)

Basado en la técnica de Jeroen Knoester (Power Programs, "CPU Assisted
Blitting"): en máquinas con memoria chip de 32 bits y CPU ≥ 68020 (A1200, CD32,
A3000, A4000), el Blitter sin `BLTPRI` deja a la CPU ~1 de cada 4 ciclos (o 1 de
3 con canales B&D), y la CPU (que accede a chip de 32 bits, el doble de eficiente
que el Blitter de 16) hace una parte del blit (líneas inferiores) mientras el
Blitter hace las superiores. ~13% más de bobs/frame; requiere alineación a
longword y objetos ≥ 32 px. Nota clave del autor: **los emuladores sobreestiman
el 68020**; hay que calibrar en hardware real.

El diseño es **híbrido en tres capas**:

1. **Default constexpr** (`BlitSplitTable<MachineProfile>`, por `BlitKind` ×
   clase de ancho → `{ lines_blitter, lines_cpu }`). Funciona desde el primer
   arranque; es la línea base conocida.
2. **Calibración runtime** (build con `-DENABLE_CALIBRATION`): mide con CIA
   timers el throughput real del reparto y refina una tabla en Fast RAM (no
   chip). `Diagnostics` avisa si la calibración se hizo en emulador.
3. **Profile bake** (`tools/profile/bake-split-table.sh`): la tabla medida se
   emite como header con la tabla constexpr (generar el header es trivial: es
   texto formateado) y se hornea en el build de distribución.

El `FramePlan` referencia la tabla activa con `Ref`; `if constexpr
(cpu_assisted_blit<M>)` decide en compilación si la estrategia existe (en A500
no hay tabla que mirar).

Virtudes:
- Una sola fuente (el `FramePlan`) produce código distinto por máquina según la
  CPU, sin `#ifdef` en el juego.
- El default + calibración + bake cubre desarrollo (rápido), hardware real
  (preciso) y distribución (sin coste runtime).

Defectos:
- La técnica es delicada (alineación, reparto por anchos, interrupciones); el
  beneficio real depende de la máquina exacta y del mix de DMA (la DMA adicional
  suele mejorar la ganancia).
- La tabla horneada fija la medición a un hardware: si el juego corre en una
  variante distinta (p. ej. un A1200 con accelerator), la calibración puede
  suboptimizar; el fallback constexpr siempre está.

---

## 14. Composición estática y selección por linker

El engine es un conjunto de componentes **independientes, header-only y
plantilla** (`TileScrollEngine<Mode>`, `SpriteManager<N>`, `Palette<N>`, etc.).
La escena del juego es un **agregado** que declara qué usa:

```cpp
struct MiComposicion {
  gfx::Playfield bg;
  gfx::TileScrollEngine<ScrollMode::XLimited8Way> scroll;
  gfx::Surface hud;
  gfx::SpriteManager<4> sprites;
};
using MiEscena = gfx::Scene<MachineProfile<A500>, MiComposicion>;
```

Con `-ffunction-sections -fdata-sections -Wl,--gc-sections`, el linker elimina
las secciones no referenciadas: si no usas corkscrew, su código no viaja, aunque
el header exista. Sin RTTI, sin excepciones, sin heap en gameplay → el binario es
pequeño y el `--gc-sections` es efectivo. Las macros solo diferencian hardware
del chipset, nunca features de juego.

Virtudes:
- "Lo que no se usa no existe": el tamaño del binario refleja la composición, no
  la biblioteca.
- La selección de técnica (blit vs CPU, corkscrew vs simple, sprites o no) es
  estática y verificable por el compilador.

Defectos:
- Las plantillas inflan el tiempo de compilación y los mensajes de error (un
  `static_assert` fallido puede ser críptico).
- Requiere que el toolchain soporte `--gc-sections` de forma fiable (hay que
  verificar en el toolchain m68k de la extensión) y que el código no use
  registros globales que el linker no pueda descartar.

---

## 15. Portabilidad a Atari ST / Megadrive / Neo-Geo

La frontera es el `FramePlan` (comandos portables) y la interfaz `Backend`:

| Comando portable | Amiga | Atari ST | Megadrive | Neo-Geo |
|---|---|---|---|---|
| `blit_copy` | Blitter | CPU/move (planar) | write VRAM | n/a (sin FB directo) |
| `blit_masked` | Blitter cookie-cut | CPU | write VRAM + paleta | concesión: plano/tile |
| `update_tilemap` | n/a | n/a | VDP tile planes | tile planes |
| `set_display` | Copper | Shifter | VDP registers | video hardware |

El mapeo lógico→físico del `Playfield` es la policy por backend; `BackendCapabilities`
(direct_framebuffer, hardware_sprites, tile_planes, max_planes, max_colors)
permite que la lógica se adapte: si `!direct_framebuffer`, una `Surface` degrada
a actualización de tiles/planos o a un framebuffer interno pequeño (la concesión
de Neo-Geo).

Virtudes:
- El juego es portable por construcción; solo cambia el backend y la policy de
  mapeo.
- `BackendCapabilities` + `ResourceLedger` detectan en compilación si un feature
  no existe en la plataforma objetivo.

Defectos:
- Abstraer tanto (de Blitter a planos de NG) obliga a que el `FramePlan` sea el
  mínimo común; las técnicas específicas de cada plataforma (per-plane scroll de
  RoboCod, planos de NG) necesitan comandos o reglas adicionales que engordan el
  modelo.
- El costo de mantener N backends es real: cada uno tiene su regresión, sus
  limits y sus bugs de emulador.

---

## 16. Mapeo a juegos de referencia

- **RoboCod** (5 planos sin DPF, fondo animado de 1 plano): un `Playfield` single
  de 5 planos; el plano 5 declara un `ScrollReq` con offset propio y el
  `CopperBuilder` emite los re-apuntados de `BPL5PT` por scanline mientras los 4
  planos de primer plano comparten el scroll principal. El fondo animado se
  dibuja con `Surface`/blits.
- **Jim Powers / Risky Woods**: DPF + sprites hardware; el `SpriteManager`
  reporta `SpriteReq` (reprogramación a mitad de frame) y la escena monta las
  zonas de sprites y paleta. Los fondos animados usan `Surface` sobre playfield.
- **Scroll vertical con cambios de color por línea**: un módulo declara
  `PalettePatch` ligado a la Y; el `CopperBuilder` lo valida contra el raster.
- **Coppersky**: `CopperRule { SyncWithBgY }` sincroniza la paleta del cielo con
  la posición Y del fondo; la escena comprueba que la Y existe y es esperable.

---

## 17. Resumen: virtudes y defectos por decisión

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
| Blit híbrido CPU/Blitter | selección por CPU; default+calibración+bake | delicado; horneado fija hardware |
| Composición por tipos + `--gc-sections` | binario = composición | compilación más lenta; requiere toolchain |

---

## 18. Roadmap / puntos abiertos

1. **Verificar `--gc-sections`** en el toolchain m68k de la extensión.
2. **Implementar el `CONFIG_ID`** en `build-demo.sh`/`run-demo` (aisla configs y
   habilita el resto).
3. **Prototipo host del `ResourceLedger`** (default+override, `static_assert`)
   con composiciones de ejemplo RoboCod-A500 y DPF-A1200.
4. **Prototipo host del `CopperBuilder` + `Diagnostics`** (zonas, arbitraje,
   severidad, canal lateral).
5. **Diseñar el comando `BlitAssist`** con la tabla híbrida (default +
   calibración + bake) como primer backend condicionado por CPU.
6. **Migración de la demo 107** a `Bitmap`/`Surface`/`ScrollEngine` manteniendo
   la regresión verde como red de seguridad.

---

*Borrador técnico en revisión. Ajustable tras la discusión de enlaces externos.*
