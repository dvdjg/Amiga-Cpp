# Roadmap de importacion desde demoscene-repo

Documento maestro para incorporar de forma sistematica los programas de prueba de:

- `C:\Users\dvdjg\Documents\programa\AI\Amiga-C++\demoscene-repo`
- tutoriales base en `C:\Users\dvdjg\Documents\programa\AI\Amiga-C++\demoscene-repo\docs\tutoriales`

La estrategia cambia respecto al enfoque anterior: en lugar de pedir a la IA que infiera tecnicas directamente del manual de hardware para cada caso, este roadmap toma como **fuente funcional primaria** los programas ya operativos del repositorio de origen y los convierte en:

1. casos de bateria del proyecto,
2. documentacion tecnica adaptada a nuestros estandares,
3. APIs reutilizables del engine cuando la tecnica ya este cerrada.

Seguimiento operativo efecto a efecto en [demoscene-repo-coverage-index.md](demoscene-repo-coverage-index.md).

## 1. Principios de importacion

### 1.1 Regla general

Cada programa del repositorio origen debe pasar por estas etapas:

1. **Referencia funcional importada**
   Se identifica el efecto original, sus archivos clave y su tutorial.
2. **Caso de bateria en Cursor-Amiga-C**
   Se crea un caso reproducible con nuestra estructura, evidencia y contrato tecnico.
3. **Tecnica documentada**
   El caso incorpora:
   - documentacion del proyecto de origen,
   - contrato tecnico de este repo,
   - costes por frame y reparto CPU/DMA/blitter/copper,
   - invariantes y evidencia.
4. **Promocion al engine**
   Si la tecnica demuestra reutilizacion real, se extrae a `engine/`.

### 1.2 Regla de no-salto

No se salta directamente de "codigo funcional en origen" a "API final del engine".

Primero:

- importar,
- aislar,
- validar en nuestra bateria,
- documentar costes y ownership,
- y solo despues promover a `engine/`.

### 1.3 Regla de fuente de verdad

Para estas tecnicas, la fuente de verdad inicial no es el AHRM sino:

1. el programa funcional del repositorio origen;
2. su tutorial asociado;
3. la validacion viva en nuestro pipeline.

El manual de hardware y otras fuentes autoritativas siguen siendo utiles para aclarar detalles o generalizar la tecnica, pero dejan de ser el punto de partida principal cuando ya existe una implementacion funcional conocida.

## 2. Estandar obligatorio para cada importacion

Cada tecnica importada debe acabar con:

- carpeta de caso en `tests/amiga-battery/`;
- `README.md` con objetivo, origen y estado;
- `docs/technique.md` con:
  - descripcion de la tecnica del repo origen,
  - configuracion de registros o estructuras relevantes,
  - adaptacion al contexto de nuestro engine,
  - coste aproximado CPU por frame,
  - uso de DMA y riesgo de saturacion,
  - papel del blitter,
  - papel del copper,
  - papel de la CPU;
- `case.json` con validacion;
- `evidence/` con evidencia viva;
- enlace al tutorial original y a los archivos de origen;
- decision explicita de si la tecnica:
  - queda solo como caso de bateria,
  - se convierte en helper reusable,
  - o sube a API del engine.

## 3. Pipeline de importacion por efecto

Para cada programa:

1. **Auditoria**
   - tutorial origen;
   - carpeta de codigo origen;
   - maquina/chipset esperado;
   - si usa copper, blitter, sprites, Paula, dual playfield, HAM, AGA o loaders especiales.
2. **Contrato tecnico local**
   - usar `doc/amiga-lowlevel-technique-contract-template.md`;
   - declarar configuracion de bitplanes, ownership, persistencia y frame loop.
3. **Caso de bateria importado**
   - adaptar lo minimo necesario para que corra en nuestro harness;
   - preservar la tecnica original antes de refactorizar.
4. **Validacion y autopsia**
   - build, run, evidencia interna, vision y registros.
5. **Extraccion de reusable**
   - mover a `engine/` solo la parte realmente repetible.
6. **Actualizacion documental**
   - matriz, roadmap y docs del subsistema.

## 4. Politica de promocion al engine

Subir una tecnica al engine cuando cumpla al menos una:

1. aparece en dos o mas efectos importados;
2. ya existe una necesidad equivalente en `app/` o en otros tests del engine;
3. su codigo hardware es peligroso o repetitivo y conviene encapsularlo;
4. la misma configuracion de registros o buffers se reutiliza de forma casi literal.

Mantener fuera del engine cuando:

- sea muy especifica de un efecto;
- dependa de assets o formatos muy propios;
- o aun no este claro el contrato publico correcto.

## 5. Oleadas de integracion

### Oleada 0: infraestructura de importacion

Objetivo: dejar un flujo estable para importar muchos efectos sin improvisacion.

Entregables:

- plantilla de caso "importado desde demoscene-repo";
- indice de cobertura `origen -> Cursor-Amiga-C`;
- plantilla de `technique.md` con seccion fija de "origen";
- convencion de naming para mapear `tutorial -> caso bateria -> API engine`;
- indice de cobertura `origen -> estado en Cursor-Amiga-C`.

### Oleada 1: fundamentos de display, loader e input

Objetivo: consolidar contratos base que muchos efectos comparten.

Efectos fuente prioritarios:

- `01-empty`
- `10-loader`
- `37-gui`
- `38-kbtest`

Resultado esperado:

- baseline de efecto importado;
- flujo `Load/Init/Render/Kill/UnLoad` bien documentado;
- base de input/GUI/loader como referencia de integracion.

### Oleada 2: display, paleta y copper de bajo riesgo

Objetivo: importar primero las tecnicas que mas valor dan al engine con menor complejidad combinatoria.

Efectos fuente prioritarios:

- `02-circles`
- `03-color-cycling`
- `04-plasma`
- `08-floor`
- `09-textscroll`
- `12-stripes`
- `39-layers`
- `50-roller`
- `53-showpchg`
- `61-transparency`

APIs candidatas:

- `engine_palette_*`
- `engine_copper_line_bands_*`
- `engine_display_dualpf_*`
- `engine_scroll_*`
- `engine_textscroll_*`

### Oleada 3: blitter 2D, BOBs, overlays y tilemaps

Objetivo: formar un bloque solido de tecnicas de runtime reutilizables para juegos 2D.

Efectos fuente prioritarios:

- `07-shapes`
- `11-game-of-life`
- `14-metaballs`
- `16-abduction`
- `17-anim`
- `19-ball`
- `20-blurred`
- `41-magnifying-glass`
- `43-neons`
- `48-plotter`
- `52-sea-anemone`
- `57-thunders`
- `58-tiles8`
- `59-tiles16`
- `60-tilezoomer`
- `67-weave`

APIs candidatas:

- `engine_blit_fill_*`
- `engine_blit_masked_*`
- `engine_bob_*`
- `engine_tiles_*`
- `engine_anim_*`
- `engine_overlay_*`

### Oleada 4: 3D y librerias geometricas

Objetivo: importar conocimiento funcional para las futuras capas 2D/3D del engine.

Efectos fuente prioritarios:

- `06-wireframe`
- `18-anim-polygons`
- `21-blurred3d`
- `22-bobs3d`
- `24-butterfly-gears`
- `29-dna3d`
- `30-flatshade`
- `31-flatshade-convex`
- `42-multipipe`
- `49-prisms`
- `51-rotator`
- `55-stencil3d`
- `56-texobj`
- `64-uvlight`
- `65-uvmap`
- `66-uvmaprgb`

APIs candidatas:

- `engine_2d_*` y `engine_3d_*`
- `engine_mesh_*`
- `engine_polyfill_*`
- `engine_uv_*`

### Oleada 5: audio y reproduccion musical

Objetivo: cubrir Paula y wrappers de reproductores sin mezclarlo prematuramente con graficos complejos.

Efectos fuente prioritarios:

- `44-playahx`
- `45-playcinter`
- `46-playp61`
- `47-playprotracker`

APIs candidatas:

- `engine_audio_*`
- wrappers de init/play/stop/vblank

### Oleada 6: efectos complejos o muy compuestos

Objetivo: dejar para el final lo que mezcla demasiadas tecnicas o tiene mas deuda de arquitectura.

Efectos fuente prioritarios:

- `05-fire-rgb`
- `13-highway`
- `23-bumpmap-rgb`
- `25-carrion`
- `26-cathedral`
- `28-darkroom`
- `32-floor-old`
- `33-forest`
- `34-glitch`
- `35-glitches`
- `36-growing-tree`
- `40-lines`
- `54-spooky-tree`
- `57-thunders`
- `62-turmite`
- `63-twister-rgb`

Notas:

- varios de estos casos son mejores como "tecnica compuesta" que como API estable inmediata;
- `63-twister-rgb` y `66-uvmaprgb` exigen tratar el chipset y el modo de color con mucho mas cuidado;
- `13-highway` y `39-layers` son claves para scroll/zonas/copper, pero conviene entrar en ellos cuando la base de playfield y overlay ya sea mas fuerte.

## 6. Matriz resumida de importacion

| Grupo | Efectos | Valor principal para el engine | Prioridad |
|------|---------|---------------------------------|----------|
| Baseline / sistema | 01, 10, 37, 38 | flujo de efecto, loader, GUI, input | Alta |
| Paleta / copper / display | 02, 03, 04, 08, 09, 12, 39, 50, 53, 61 | display base, dual PF, copper por linea, scroll | Muy alta |
| Blitter / 2D / tiles | 07, 11, 14, 16, 17, 19, 20, 41, 43, 48, 52, 57, 58, 59, 60, 67 | runtime 2D reusable | Muy alta |
| 3D / geometria | 06, 18, 21, 22, 24, 29, 30, 31, 42, 49, 51, 55, 56, 64, 65, 66 | futuras libs 2D/3D y pipelines de relleno | Media |
| Audio | 44, 45, 46, 47 | Paula y wrappers de players | Media |
| Casos compuestos / avanzados | 05, 13, 23, 25, 26, 28, 32, 33, 34, 35, 36, 40, 54, 62, 63 | tecnicas de alto acoplamiento | Media-Baja |

## 7. Mapa completo de efectos

| # | Efecto origen | Carpeta origen | Destino inicial recomendado | Posible promocion al engine |
|---|---------------|----------------|-----------------------------|-----------------------------|
| 01 | Empty | `effects/empty` | caso baseline importado | plantilla de efecto |
| 02 | Circles | `effects/circles` | test tecnico 2D simple | draw/blit primitives |
| 03 | Color-cycling | `effects/color-cycling` | caso paleta | `engine_palette_rotate` |
| 04 | Plasma | `effects/plasma` | caso copper visual | copper por bandas |
| 05 | Fire RGB | `effects/fire-rgb` | tecnica compuesta tardia | c2p / blit helpers |
| 06 | Wireframe | `effects/wireframe` | caso 3D base | `engine_3d_*` |
| 07 | Shapes | `effects/shapes` | caso 2D relleno | clip/fill 2D |
| 08 | Floor | `effects/floor` | caso scroll/copper | scroll + copper |
| 09 | TextScroll | `effects/textscroll` | caso texto+scroll | `engine_textscroll_*` |
| 10 | Loader | `effects/loader` | referencia loader | loader APIs |
| 11 | Game of Life | `effects/game-of-life` | caso blitter creativo | minterms reusable |
| 12 | Stripes | `effects/stripes` | caso copper por linea | color bands |
| 13 | Highway | `effects/highway` | tecnica compuesta | zonas + sprites + copper |
| 14 | Metaballs | `effects/metaballs` | caso blobs | blob/blit masks |
| 15 | Indice resto | `docs/tutoriales/15-indice-resto.md` | N/A | N/A |
| 16 | Abduction | `effects/abduction` | caso anim/sprites | anim+layers |
| 17 | Anim | `effects/anim` | caso anim blitter | `engine_anim_*` |
| 18 | Anim-polygons | `effects/anim-polygons` | caso 2D geom | import pipeline SVG/data |
| 19 | Ball | `effects/ball` | caso movimiento base | blit sprite runtime |
| 20 | Blurred | `effects/blurred` | caso blur | blur buffer ops |
| 21 | Blurred3D | `effects/blurred3d` | caso 3D compuesto | 3D + accumulation |
| 22 | Bobs3D | `effects/bobs3d` | caso 3D BOB | object lists |
| 23 | Bumpmap RGB | `effects/bumpmap-rgb` | tecnica tardia | lighting helpers |
| 24 | Butterfly-gears | `effects/butterfly-gears` | caso optimizacion | loops especializados |
| 25 | Carrion | `effects/carrion` | tecnica compuesta | blit por trozos |
| 26 | Cathedral | `effects/cathedral` | tecnica avanzada | ray casting + copper |
| 27 | Credits | `effects/credits` | caso texto | texto/paleta |
| 28 | Darkroom | `effects/darkroom` | caso LUT/fade | palette LUT |
| 29 | Dna3D | `effects/dna3d` | caso 3D+paleta | depth palette |
| 30 | FlatShade | `effects/flatshade` | caso 3D fill | fill tri API |
| 31 | FlatShade-convex | `effects/flatshade-convex` | caso 3D fill | convex shortcut |
| 32 | Floor-old | `effects/floor-old` | referencia historica | scroll comparisons |
| 33 | Forest | `effects/forest` | tecnica compuesta | scene manager hints |
| 34 | Glitch | `effects/glitch` | caso experimental | debug/guardrails |
| 35 | Glitches | `effects/glitches` | caso experimental | debug/guardrails |
| 36 | Growing-tree | `effects/growing-tree` | caso lineas | draw line helpers |
| 37 | GUI | `effects/gui` | referencia UI | `engine_ui_*` |
| 38 | Kbtest | `effects/kbtest` | test input | `engine_input_*` |
| 39 | Layers | `effects/layers` | caso dual playfield | `engine_display_dualpf_*` |
| 40 | Lines | `effects/lines` | caso draw line | `engine_blit_line_*` |
| 41 | Magnifying-glass | `effects/magnifying-glass` | caso copia/zoom | blit zoom helpers |
| 42 | MultiPipe | `effects/multipipe` | caso 3D bandas | 3D fill pipeline |
| 43 | Neons | `effects/neons` | caso capas+paleta | glow/palette cycling |
| 44 | PlayAHX | `effects/playahx` | caso audio | `engine_audio_ahx_*` |
| 45 | PlayCinter | `effects/playctr` | caso audio | `engine_audio_cinter_*` |
| 46 | PlayP61 | `effects/playp61` | caso audio | `engine_audio_p61_*` |
| 47 | PlayProtracker | `effects/playpt` | caso audio | `engine_audio_pt_*` |
| 48 | Plotter | `effects/plotter` | caso lineas/puntos | plotter helpers |
| 49 | Prisms | `effects/prisms` | caso 3D tris | mesh fill |
| 50 | Roller | `effects/roller` | caso copper bplpt | scanline bplpt |
| 51 | Rotator | `effects/rotator` | caso roto | rotator helpers |
| 52 | Sea-anemone | `effects/sea-anemone` | caso lineas radiales | draw line/fx tables |
| 53 | ShowPCHG | `effects/showpchg` | caso palette change | palette per line |
| 54 | Spooky-tree | `effects/spooky-tree` | tecnica compuesta | line/fade helpers |
| 55 | Stencil3D | `effects/stencil3d` | caso 3D masking | minterm 3D masks |
| 56 | TexObj | `effects/texobj` | caso UV base | `engine_uv_*` |
| 57 | Thunders | `effects/thunders` | caso line/fill FX | flashes + lines |
| 58 | Tiles8 | `effects/tiles8` | caso tilemap 8x8 | `engine_tiles8_*` |
| 59 | Tiles16 | `effects/tiles16` | caso tilemap 16x16 | `engine_tiles16_*` |
| 60 | TileZoomer | `effects/tilezoomer` | caso tile FX | tile zoom helpers |
| 61 | Transparency | `effects/transparency` | caso dual PF/transparency | minterm/transparency API |
| 62 | Turmite | `effects/turmite` | caso automata | CPU buffer policies |
| 63 | Twister RGB | `effects/twister-rgb` | caso chipset avanzado | AGA/RGB path |
| 64 | UVLight | `effects/uvlight` | caso paleta | UV palette helpers |
| 65 | UVMap | `effects/uvmap` | caso UV | `engine_uv_*` |
| 66 | UVMapRGB | `effects/uvmap-rgb` | caso UV avanzado | RGB/AGA path |
| 67 | Weave | `effects/weave` | caso bands/blit | painter ordering |

## 8. Orden practico recomendado para empezar

Primer lote real:

1. `01-empty`
2. `10-loader`
3. `03-color-cycling`
4. `39-layers`
5. `58-tiles8`
6. `46-playp61`

Motivos:

- cubren esqueleto de efecto, loader, paleta, dual playfield, tilemap y audio;
- son muy representativos de lo que luego necesitara un juego;
- y permiten extraer reusable al engine sin saltar todavia a los casos mas explosivos.

Segundo lote:

1. `04-plasma`
2. `08-floor`
3. `09-textscroll`
4. `11-game-of-life`
5. `59-tiles16`
6. `37-gui`

Tercer lote:

1. `06-wireframe`
2. `30-flatshade`
3. `56-texobj`
4. `44-playahx`
5. `61-transparency`
6. `50-roller`

## 9. Gaps ya detectados entre origen y engine actual

El repositorio origen ya resuelve o ilustra mejor que el engine actual varias zonas:

- flujo completo de efectos `Load/Init/Render/Kill/UnLoad`;
- copper por linea y listas mas ricas;
- dual playfield y prioridad visual;
- tilemaps y texto con scroll;
- wrappers de audio reales;
- librerias 2D/3D mas completas;
- GUI e input mas maduros.

Por tanto, este roadmap debe leerse tambien como un plan de:

1. importar casos funcionales;
2. detectar helpers existentes que conviene reusar o traducir;
3. elevar al engine solo la parte reusable y bien entendida.

## 10. Casos especiales

- `effects/starfox` existe en el repo origen pero no aparece en el indice de tutoriales consultado. Tratarlo como efecto fuera de roadmap principal hasta documentarlo mejor.
- `effects/playctr` corresponde al tutorial `45-playcinter`.
- `effects/playpt` corresponde al tutorial `47-playprotracker`.
- `effects/uvmap-rgb` corresponde al tutorial `66-uvmaprgb`.

## 11. Entregable siguiente recomendado

Tras aprobar este roadmap, el siguiente entregable natural es un **indice de cobertura origen -> Cursor-Amiga-C** con una fila por efecto y columnas:

- tutorial origen,
- carpeta origen,
- estado importacion,
- caso bateria destino,
- API engine candidata,
- evidencia viva,
- notas de adaptacion.
