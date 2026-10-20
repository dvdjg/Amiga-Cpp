# Roadmap: importación de aventuras clásicas y previewer (pipeline host)

Plan del **pipeline de PC** que convierte juegos de aventura clásicos (SCUMM, empezando por **V5** —Monkey Island 2, Fate of Atlantis— y **V4** —Monkey Island 1 floppy/Amiga—; y después V6/DOTT, AGI, SCI y Cinématique) en **proyectos nativos del engine**: código C++ de `eng::adv` + datos cocinados + assets, compilables con el toolchain normal. El diseño de la capa destino está en [`ADVENTURE_LAYER.md`](../../engine/architecture/ADVENTURE_LAYER.md) y el plan de la capa en [`ROADMAP_ADVENTURE.md`](ROADMAP_ADVENTURE.md).

**Estrategia**: ScummVM y sus herramientas se usan como **oráculo de formato y comportamiento en el host**, no como runtime que se porta; el formato canónico propio es el **SIM** (Semantic Intermediate Model — la idea «AIR» de la consulta externa), independiente del motor original y **ampliable con información Amiga** (capas de scroll, transparencias por color, cues de Copper, preferencia de materialización). La traducción de guiones es **semiautomática**: reglas deterministas para los patrones comunes + IA local **opcional y offline** (solo produce propuestas revisables), nunca un paso del build.

## Decisiones fijadas

- **Sin VM ni bytecode en el Amiga**: la salida del pipeline es C++ compilado + tablas ROM/UAF (misma decisión que `RPG_LAYER.md` §5.7). El SIM existe solo en el host; un intérprete del SIM (si se implementa) es la herramienta de *preview*/oráculo del PC, no un componente del juego.
- **Builds deterministas**: el traductor por defecto es de reglas (golden files); la IA es un modo `--ai` off-line cuyas propuestas se revisan y se commitean como código, de forma que el build **no depende** de un modelo ni de la red.
- **Higiene de licencias**: ScummVM/GPL y las herramientas de extracción se usan como **proceso externo** o como referencia para reimplementar parsers pequeños en `tools/`; nada de código GPL se enlaza en `engine/` ni en el binario Amiga. Las atribuciones se anotan en la sección *Créditos* de `docs/README.md` al incorporar cada pieza.
- **Herramientas host, código objetivo freestanding**: el pipeline (TypeScript/C++ de PC) puede usar STL/Node; lo **generado** para el Amiga respeta las reglas del engine (capacidades fijas, `Span`, cero heap, sin `malloc`, sin 68020).
- **SIM versionado** (esquema con número de versión) y validado contra un esquema; fixtures de test **sintéticos y propios** (nada de datos de juegos comerciales versionados en el repo; las ejecuciones locales reales no se commitean).
- **Enriquecimiento Amiga con revisión humana**: separar fondos en capas de parallax, marcar máscaras de color y proponer cues de Copper son **sugerencias asistidas** que se editan y versionan en el SIM, con **modo diff** contra la versión importada.

## Fuentes locales del oráculo (host)

Referencias locales de trabajo (fuera del repositorio; rutas relativas a la raíz de `Amiga-Cpp/`):

```text
../SCUMM/
├── scummvm-2026.4.0/           código completo de ScummVM: oráculo de formato y comportamiento
│   └── engines/
│       ├── scumm/              familia SCUMM (V0–V8 + HE; aquí interesan V3/V4/V5)
│       ├── agi/                familia AGI (I5)
│       ├── sci/                familia SCI (I5)
│       └── cine/               familia Cinématique (Operation Stealth, Future Wars)
└── scummvm-tools-2.9.0/        herramientas host (proceso externo o referencia)
    ├── engines/scumm/          descumm (descumm.cpp, descumm6.cpp) y extractores (extract_*)
    └── decompiler/             framework de descompilado compartido
```

Mapa rápido «qué leer para qué» en I0/I1 — salvo indicación, bajo `../SCUMM/scummvm-2026.4.0/engines/scumm/`:

| Necesito… | Fichero |
|---|---|
| Confirmar edición y versión de cada copia por md5 | `scumm-md5.h` (+ `detection_tables.h`; la tabla incluye entradas ES para Amiga) |
| Carga de sala (fondos, z-planes, objetos, escalado) | `room.cpp` |
| Walkboxes (BOXD/BOXM) y navegación | `boxes.cpp` |
| Opcodes por versión | `script_v3.cpp`, `script_v4.cpp`, `script_v5.cpp`, `script_v6.cpp` (+ defines `scumm_v3.h`…`scumm_v6.h`) |
| Actores, costumes y animación | `actor.cpp`, `costume.cpp` |
| Estados de objeto | `object.cpp` |
| Texto y charsets | `charset.cpp`, `string.cpp` |
| Verbos y diálogos | `verbs.cpp`, `dialogs.cpp` |
| iMUSE (música) y reproductores | `imuse/`, `players/` |
| Modelo de estado del original (referencia del SIM) | `saveload.cpp`, `serializer.h` |
| Rutinas Amiga del motor original | `m68k/` |
| Descompilado legible (salida de descumm) | `../scummvm-tools-2.9.0/engines/scumm/{descumm,descumm6,descumm-common}.cpp` + `../scummvm-tools-2.9.0/decompiler/` |
| AGI / SCI / Cinématique (I5) | `../scummvm-2026.4.0/engines/{agi,sci,cine}/` |

Ediciones de las copias locales (verificado en `scumm-md5.h`/`detection_tables.h` de la fuente local): **Monkey Island 2 Amiga y Fate of Atlantis Amiga son SCUMM V5**; Monkey Island 1 es **V4** (floppy Amiga/PC; el CD/SE es V5); **Indy3 y Loom son V3** (Loom VGA/CD, V4); **Operation Stealth y Future Wars son Cinématique** (`cine`); las ediciones *Ultimate Talkie Edition* del material PC de trabajo (MI1, MI2) son datos **SCUMM V5** con habla añadida. No hay binarios precompilados (los oficiales de Windows dan falsos positivos de antivirus): `descumm` se compila en local con la receta del repositorio `tools/adventure/build-scummvm-tools.sh` (solo sus fuentes + `common/`, sin el árbol completo de configure+make) o se sustituye por un extractor propio (licencia: ver «Decisiones fijadas»). Las copias de trabajo están en ADF/WHDLoad y en ficheros PC (`.000/.001`): el volcado a directorio se hace con `xdftool` (amitools), los `.zip`/`.lha` de WHDLoad con 7-Zip y el ADF bootable de *salida* con `exe2adf.exe` del plugin Bartman — comandos verificados en [`tools/adventure/README.md`](../../../tools/adventure/README.md) §«Herramientas de disco Amiga». Dato de formato para I0: las copias **Amiga V3/V4** guardan las salas en contenedores **`.LFL`** (p. ej. `Rooms/000.LFL` de MI1 Amiga) y las **PC V5** en **`.000/.001`**; ambos llevan índice y cifrado ligero cuyo parser de referencia es `resource*.cpp` del oráculo.

## Estado

| Hito | Contenido | Estado |
|---|---|---|
| **I0 — SIM y extracción** | Esquema SIM + extractor SCUMM (V5 primero —MI1/MI2/FoA—, luego V4/V3): salas, walkboxes, objetos, costumes, textos | **CASI CERRADO** — extractor V5 verificado (MI1 UE: 86 salas, 748/748 guiones; MI2 UE: 110 salas, 1006/1006); **SIM v2** semántico (`decoded/sim.json`: tamaño, walkboxes+adyacencia, paleta, objetos con hotspots, guiones) con fixture y golden; **contenido C++ generado** (`codegen/emit-content.mjs` → `eng::adv`, probado end-to-end con MI1) y capa de contenido del engine con test (**HOST-473**). **Normalización a formatos estándar**: imágenes (`RMIM`/`IMxx` → **PNG indexado sin pérdidas**) para MI1 86/86 salas + 697 objetos y MI2 110/110 + 596 (visión local OK); audio `.sof` → **FLAC sin recodificar** (4393 pistas en MI1, ffprobe OK); **costumes `COST` → hojas PNG** (`decode/costume.mjs`: RLE column-major fiel, 3 714 cels en MI1 (117/123 costumes) y MI2 157/162, tests + visión local OK). **Frontend V3/V4 `.LFL`** (`frontends/scumm_v4_lfl/`): índice (tablas `0R/0S/0N/0C/0O`), salas `NNN.LFL` con bloques (`RO`→`HD`/`BX`/…, y `SC`/`SO`/`CO` tras el `RO`) y guiones `descumm -3/-4` (`SCRP_<id>` vía tabla `S0`, locales y `EX`/`EN`), con cifrado XOR 0x69 detectado — verificado sobre Indy3 FM-Towns (87 salas, 490 guiones); V4 Amiga pendiente de ADF materializado (SIN VERIFICAR) |
| **I1 — Traducción de guiones** | Descompilado → AST semántico → C++ de `eng::adv` (reglas; IA opcional con revisión) | **HECHO (base)** — parser descumm→AST (`adv-ast/1`; MI1 92.9 %, MI2 91.5 % de líneas) y **traductor AST→C++** (`cook/cpp/scripts/`, contrato `eng/adv/script_hooks.hpp`): **748/748 guiones MI1** y 1004/1006 MI2 generados; goto/unless → etiquetas C++, `raw` contabilizado. Semántica mapeada: **25 433 llamadas** MI1 y 35 871 MI2 (4 803 / 7 451 no soportadas, con informe por guion); `ActorOps`/`VerbOps` → una llamada tipada por sub-operación (`ActorOp`/`VerbOp`), listas del original → `w.list`, `print` estático → réplicas. Corpus completo con syntax-check en host; ejecución verificada en guiones sin espera (sala 4). Pendiente: revisión semántica/IA opcional y seguir ampliando mapeos |
| **I2 — Cocina de assets** | Fondos a planar/tiles/EHB, costumes a animaciones del engine, audio al pipeline | Pendiente |
| **I3 — Previewer Amiga-aware** | Visor/editor de PC que reutiliza la lógica pura del engine y muestra presupuesto | Pendiente |
| **I4 — Enriquecimiento y presupuesto** | Capas/transparencias/cues asistidos + informe de presupuesto + modo diff | Pendiente |
| **I5 — Segundo motor y segundo juego** | AGI/SCI/Cinématique sobre el mismo SIM; validación de genericidad | Pendiente |

---

## I0 — SIM y extracción (SCUMM V5 primero, luego V4/V3)

**Objetivo**: extraer un juego completo a un modelo semántico limpio, sin ejecutar nada del motor original.

**Juego piloto**: MI1 *Ultimate Talkie Edition* es el primer objetivo completo (decisión del usuario; datos SCUMM V5 ya extraídos en `out/adventure/mi1/`); MI2 *Ultimate Talkie Edition* el segundo (misma base V5 de MI2) y Fate of Atlantis CD el tercero (V5 CD con `monster.so3`). Indy3 FM-Towns (V3, ficheros `.LFL`) estrena el frontend LFL (`frontends/scumm_v4_lfl/`: índice/salas/bloques y 490 guiones `descumm -3`).

**Estado (parcial, verificado)**: el frontend V5 existe (`tools/adventure/frontends/scumm_v5/extract.mjs`) con workspace canónico por juego ([`WORKSPACE.md`](../../../tools/adventure/WORKSPACE.md)) y está probado sobre **MI1 UE** (86 salas con datos, **748/748 guiones** descompilados) y MI2 UE (110 salas, **1006/1006 guiones**); el decodificador (`decode/`) produce el **SIM v2** semántico (MI1: 792 walkboxes, 1027 objetos, paleta por sala) y `codegen/emit-content.mjs` genera las **tablas C++ del engine** (`eng::adv::RoomContent`, `find_room`) — verificado end-to-end compilando el contenido de MI1 contra el engine host. La capa de contenido del engine (`eng/adv/{types,walkbox,hotspot,room}.hpp`) está cubierta por **HOST-473** (dos escalares). Informe de opcodes (`analyze/opcode-usage.mjs`: MI1 usa 182 de 256 bytes, top-30 = 83.8 %). SIM v1 y v2 con fixture sintético y golden tests. Referencia de la VM: [`vm-v5.md`](../../reference/scumm/vm-v5.md); formato y comandos: [`frontends/scumm_v5/README.md`](../../../tools/adventure/frontends/scumm_v5/README.md); esquema: [`sim/SCHEMA.md`](../../../tools/adventure/sim/SCHEMA.md).

**Entregables**:

1. `tools/adventure/sim/`: tipos del SIM + esquema versionado + validador (`node --test`) — salas (fondo, walkboxes, depth zones, hotspots, scripts por objeto), actores/costumes, textos, verbos, diálogos y metadatos de juego. Salida en JSON legible (`sim.json`) para editar y versionar **en local** (`out/adventure/`); al repositorio solo entran esquema, herramientas e informes (nada de contenido de juegos con derechos; ver «Decisiones fijadas»).
2. `tools/adventure/frontends/scumm_v5/` (y después `scumm_v4/`, `scumm_v3/`): extractores de rooms/objects/costumes/BOXD-BOXM/textos; la referencia de formato es el código de ScummVM (`../SCUMM/`, ver «Fuentes locales») y el descompilador `descumm` (compilado en local con `tools/adventure/build-scummvm-tools.sh`) o equivalentes externos (NUTCracker) se invocan como **procesos externos** (ruta configurable) o se reimplementan los parsers mínimos.
3. **Informe de extracción**: por juego, cobertura (número de salas/objetos/costumes/textos extraídos vs. esperados) y lista de elementos marcados `needs_review`; se guarda como texto canónico en `docs/` al cerrar cada juego (la media va a `out/adventure/<juego>/`, gitignored).
4. Fixtures sintéticos propios (un «mini-juego» SCUMM-like generado por nuestras herramientas) para golden tests deterministas.

**Verificación**: suite `node --test` del SIM (esquema, casos límite) + golden del fixture + una extracción real local completa de un juego V5 (p. ej. MI2 o FoA Amiga) como prueba de aceptación (no versionada).

## I1 — Traducción de guiones a C++

**Objetivo**: `descumm`/NUTCracker → AST semántico → funciones C++ registradas contra `eng::adv`, con trazabilidad de lo no traducido.

**Entregables**:

1. `tools/adventure/translate/`: parser de la salida descompilada → AST propio; **reglas deterministas** para los patrones núcleo (`walkActorTo`/`putActor` → `actor.walk_to`/`set_position`; `startScript`/`stopScript` → llamadas a guiones; `setState`/`getState` → estado de objeto; `sayLine`/`printEgo` → `say`; `loadRoom` → `change_room`; condiciones/variables → hechos/`CondKind`), emitiendo C++ con la API de la capa.
2. **Modo `--ai` (opcional, offline)**: fragmentos no cubiertos por reglas se envían al modelo local con el contexto del objeto/sala y la API de `eng::adv`; la respuesta se guarda como propuesta con nivel de confianza y **requiere revisión humana** antes de commitearse (los `.cpp` revisados son la fuente, no el modelo).
3. **Marcas de revisión**: opcode raro o condición ambigua → `// REVISAR:<motivo>` + entrada en el `report.md`; métrica de cobertura (% de guiones traducidos sin marca; objetivo 80–90 % de lo jugable en V3–V5, el resto explícito).
4. **Gates de codegen**: el C++ generado se compila con el toolchain y pasa `asm-audit.mjs`/`codegen-report.mjs` (sin 68020/FPU, sin libcalls nuevas no justificadas), como cualquier código del engine.

**Verificación**: golden de traducción del fixture (entrada descompilada → C++ esperado) + compilación m68k del proyecto generado + una sala real jugable (local) como aceptación.

## I2 — Cocina de assets

**Objetivo**: convertir el material extraído al pipeline del engine, con round-trip verificado.

**Entregables**:

1. **Fondos**: imagen de sala → paleta del engine (mediancut/dither del pipeline de tiles), bitmap planar a pantalla completa o tiles interleaved según la clase de la sala (decisión del usuario en el SIM: full-bitmap para salas «de aventura», tilemap cuando convenga scroll infinito).
2. **Costumes → animaciones**: frames por dirección/acción a `ActorDesc`/animaciones de `scene::Actor` (BOB/sprite) con anclas y máscaras; respeta el par *attached* de sprites cuando aplique (derivado del arte, como ya hace el engine).
3. **Objetos/props** → BOBs/sprites cocinados; **textos** → tabla de strings del juego (con acentos/LATIN-1) y fuentes bitmap → `Font8`/glifos.
4. **Audio**: SFX → `tools/audio/prep-sample.ts` (formato del mixer); música: SCUMM usa iMUSE/MIDI — **no se emula**: el informe marca cada tema como `needs_music` y el juego aporta módulos P61/pt propios o transcripciones (decisión explícita, no un fallo del pipeline).
5. **Empaquetado**: chunks UAF-R para todo lo nuevo (bitmaps, sprites, walkboxes/hotspots como chunk de aventura); el formato se documenta en la sección de aventura de [`UAF_PACK.md`](../../tools/UAF_PACK.md) al implementarlo.

**Verificación**: round-trip al 100 % para imágenes (regla del pipeline de tiles: cuantizar antes de extraer, comparar al 100 %, [`REGLAS_PIPELINE_TILES.md`](REGLAS_PIPELINE_TILES.md)) + goldens de tamaños + vision check de una sala cocinada en el previewer.

## I3 — Previewer Amiga-aware (PC)

**Objetivo**: ver y editar la aventura **antes** de pasarla al Amiga, reutilizando la lógica real del engine (no un simulador aparte).

**Entregables**:

1. `host-tools/adv-preview/` (C++23 + SDL3, como `host-tools/audio-compressor`): carga SIM + datos cocinados y dibuja la sala con el software rasterizador (`CanvasPlayfield`-style) usando el **código puro del engine compilado en host**: `scene::LayerPlan`/`SceneStrategy`, `adv::composer` y `adv::camera`, walkboxes/hit-test — la misma planificación que correrá en Amiga.
2. **Overlays de autoría**: walkboxes, hotspots, depth zones, bordes de banda y toggles por capa/efecto; panel de **presupuesto** (bytes Chip, palabras de Copper/línea, palabras de Blitter/frame estimadas, canales de sprite) con semáforo por perfil A500/A1200.
3. **Límites declarados**: el previewer muestra el *plan* y su coste, no el timing real del chipset; la verdad de hardware sigue siendo WinUAE (build→run→analyze) y las demos del roadmap de la capa.
4. Edición mínima: mover walkboxes/hotspots y anotar cues en el SIM, guardando el JSON versionable.

**Verificación**: checklist de fidelidad contra la demo 004 (misma sala/capas reproducen la misma composición en WinUAE) + tests del panel de presupuesto contra el informe de I4.

## I4 — Enriquecimiento Amiga, presupuesto y diff

**Objetivo**: añadir lo que hace única la versión Amiga (multi-plano real, transparencias por color, Copper) con presupuesto **validado en el build**.

**Entregables**:

1. **Asistente de capas**: heurísticas que proponen separar el fondo importado en planos (cielo/mar/lejanos/figura), marcar máscaras de color clave y sugerir cues de Copper (salvo slots del HUD); las sugerencias se puntúan, se editan en el SIM y se versionan.
2. **Informe de presupuesto** `tools/adventure/report/`: por sala, calcula bitplanes + buffers de scroll + sprites + copperlist + HUD con las constantes del engine (`RegionCost`, `eng/hw/bus_budget.hpp`) y **falla con mensaje claro** si la sala no cabe en el perfil elegido; salida `budget.json` + `report.md` consumibles por las demos y el slice de A5.
3. **Modo diff** SIM vs SIM (importado vs editado): secciones y entradas cambiadas, para revisar el enriquecimiento sin perder el original.
4. Segundo juego de prueba con el mismo pipeline (p. ej. otro título V5/V6) para validar que el SIM no quedó atado a un título.

**Verificación**: informe validado contra dos salas contrastadas a mano (una dentro y una fuera de presupuesto) + el vertical slice A5 lo consume como gate de contenido.

## I5 — Segundo motor y segundo juego

**Objetivo**: generalizar el SIM y el traductor a más familias (AGI primero; después SCI y Cinématique) y demostrar la genericidad con un segundo juego.

**Entregables**: frontend AGI (simple, bueno para fijar el SIM), frontend SCI y/o Cinématique (`cine`: Operation Stealth, Future Wars), y una sala jugable de cada familia con el mismo backend de generación; el camino V3 lo ejercen ya Indy3/Loom dentro de I0/I1, sin frontend aparte; actualización del informe de cobertura.

**Verificación**: sala jugable por familia + golden tests por frontend; si un concepto del SIM no cubre una familia sin parches, se corrige el esquema (con versión nueva), no se abren caminos paralelos.

---

## Dependencias y riesgos

- **Capa destino**: todo el pipeline depende de `eng::adv` (roadmap A0–A4); I0–I2 pueden avanzar en paralelo a A0–A1 porque solo necesitan el SIM y las convenciones de assets.
- **Licencias**: la decisión de proceso externo/reimplementación se aplica por herramienta; nunca se enlaza GPL en el binario.
- **iMUSE/música**: no se emula; cada tema queda marcado y se resuelve con módulos propios (el coste se declara por juego).
- **Fidelidad de comportamiento**: el oráculo es ScummVM corriendo el juego original en PC; cuando un guion traducido difiera, la referencia es la ejecución del original (no la intuición), y el caso se documenta en `docs/debugging/` si revela una rareza de formato.
- **Ruido de artefactos**: las extracciones reales van a `out/adventure/` (gitignored); al repo solo entran informes, esquemas, herramientas y fixtures sintéticos.

## Prioridad de continuación

1. **Cerrar I0 en el frontend V4 Amiga** materializando el ADF local y validando el índice `.LFL`, las salas y guiones contra los mismos contadores de aceptación del caso V3; I0 ya tiene SIM v2, fixtures y extracción V5 completas, por lo que este es el hueco de cobertura señalado.
2. **Ampliar I1 por valor jugable**: atacar primero las familias de llamadas no soportadas que bloquean una sala o puzzle de MI1/MI2 y mantener las demás marcadas `REVISAR`; el reporte actual registra 4 803 llamadas MI1 y 7 451 MI2 sin mapeo, separadas de las 25 433 y 35 871 llamadas ya mapeadas.
3. **Completar I2**: cocina de fondos/costumes/props a los formatos reales del engine, empaquetado UAF-R y round-trip visual al 100 %; esto convierte extracción y codegen en contenido consumible por el slice.
4. **Construir I3 después del compositor A3**: el previewer debe cargar el mismo `adv-stage/1` y reutilizar el Stage Composer puro; adelantar el editor duplicaría una composición que todavía no existe.
5. **Hacer I4 gate del contenido de A5** y dejar I5 (AGI/SCI/Cinématique adicionales) para cuando el SIM y la traducción de la primera familia estén estables; el informe de presupuesto debe detectar salas que exceden el perfil A500 antes del build.

## Próximo paso verificable

Materializar una copia local del ADF V4 Amiga pendiente y ejecutar el frontend `frontends/scumm_v4_lfl/` para comprobar cobertura de salas, objetos y guiones con fixtures/goldens deterministas. La evidencia debe mantenerse fuera de git en `out/adventure/`; si la copia no está disponible, registrar el bloqueo de datos y seguir con I1 sobre fixtures propios, sin declarar cerrado el frontend V4 Amiga.
