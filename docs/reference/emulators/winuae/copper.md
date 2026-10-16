# WinUAE — Copper: autoridad de escritura (`CDANG`), decodificación y mapa de registros

Fuente de la **tabla de registros escribibles** y de la **decodificación de instrucciones** que
usan `copper::copper_register_access`/`copper_register_writable` y
`eng::setup::check_copper_list`/`check_copper_column`.

## 1. Autoridad de escritura (`CDANG`)

El Copper solo puede escribir **algunos** registros custom. La comprobación está en
`WinUAE-DBG/custom.cpp:2841-2852` (`test_copper_dangerous`):

```c
int addr = reg & 0x01fe;
if (addr < ((copcon & 2) ? (ecs_agnus ? 0 : 0x40) : 0x80)) {
    cop_state.state = COP_stop;   // detiene el Copper
    copper_enabled_thisline = 0;
    return 1;
}
```

- Con **`COPCON` bit 1 (`CDANG`) = 0**, el Copper no puede escribir ningún registro con
  `offset < 0x80` (incluye los del **Blitter**, `$DFF040-$DFF074`) y, si lo intenta, el Copper se
  **detiene** (`COP_stop`) — no ignora la escritura.
- Con **`CDANG` = 1**, el umbral baja a `0x40` (OCS) o `0` (ECS Agnus): el Copper ya puede
  escribir los registros del Blitter.

Consecuencia para el engine: `takeover_display` (`amiga.cpp`) activa `CDANG`
(`COPCON = $0002`) antes de arrancar la lista. Sin él, un `CopperIntentKind::BlitterJob` detenía
el Copper en la primera escritura (`BLTCON0`) y la lista nunca llegaba al final (síntoma: la
pantalla quedaba con el último color escrito y el blit no ocurría).

## 2. Decodificación de instrucciones (la que implementa el validador)

`custom.cpp:9225-9262` (`COP_read2`):

- MOVE ssi `bit 0` de la **primera** word = 0; entonces el destino es `reg = word0 & 0x1fe`
  (`custom.cpp:9266`) — los bits 15-9 de la word de registro no existen en el bus y son
  «don't care, should be 0» (AHRM 3.ª Table 2-2): una lista escrita a mano con ellos encendidos
  es basura, y `check_copper_list` la rechaza.
- WAIT/SKIP ssi `bit 0` de la primera word = 1; **WAIT vs SKIP lo decide el bit 0 de la
  segunda word** (`custom.cpp:9249`: `| 1` = SKIP).
- `vcmp = (word0 & (word1 | 0x8000)) >> 8`, `hcmp = word0 & word1 & 0xfe`
  (`custom.cpp:9255-9256`) y en `coppercomp` (`custom.cpp:4416-4458`)
  `vp = vpos & (((word1 >> 8) & 0x7f) | 0x80)`: el bit 15 de la segunda word es **BFD**
  (1 = no esperar al Blitter) y el bit 7 del comparador V **no se puede enmascarar**.
- El chipset reconoce como fin de lista el par `0xffff`/`0xfffe` (`custom.cpp:9542`,
  `COP_waitforever`); `0xffff` con otra máscara nunca satisface la comparación (espera
  infinita) y el validador lo marca como terminador mal formado.
- Un WAIT ya pasado **no vuelve a esperar** (`custom.cpp:4450-4454`: `vp > vcmp` ⇒ 0), así que
  varios WAIT en la misma posición comparten el presupuesto HBlank de esa línea (lo modela el
  acumulador de `compile_intents` y la `Timeline`).

## 3. Mapa de registros escribibles (identify.cpp)

`WinUAE-DBG/identify.cpp:135-395` (tabla `custd`) lleva los flags por registro:
`CD_WO` = write-only, `CD_ECS_AGNUS`/`CD_ECS_DENISE`/`CD_AGA`, y **flags 0 = registro de
lectura**. Los `case` de `custom_wput_agnus` (`custom.cpp:7316-7613`) confirman qué offsets
tienen efecto y su `default` documenta el resto: «writing to read-only register causes read
access» (`custom.cpp:7603-7611`). La tabla del engine (`copper.hpp`,
`copper_register_access`) codifica ese mapa como clases `Writable`/`EcsOnly`/`AgaOnly`/
`ReadOnly`/`Reserved`; hechos usados por el validador:

- `BPLCON3` = `$106` (ECS Denise) y `BPLCON4` = `$10c` (AGA): confundirlos cablea el registro
  equivocado — el enum `Register` del engine usa los nombres reales.
- Punteros `BPL7/8PT` (`$0f8-$0fe`), `BPLCON4`, `CLXCON2`, `BPL7/8DAT` y `FMODE` (`$1fc`) son
  **AGA**; `HTOTAL`..`VBSTOP`, `BEAMCON0`, `DIWHIGH`, `BPLxHPT`… son **ECS** (`$1c0-$1ee`).
- `$1c0-$1fe` en OCS, `$110-$11e`… pueden existir como huecos: escribirlos no hace nada.

**EHB (nota de la investigación)**: el half-brite **no** lo selecciona `BPLCON3`; sale de
`BPLCON0` con 6 planos y HAM=0 (AHRM 3.ª cap. 3, «Color selection in Extra Half Brite mode»;
`drawing.cpp:2741`: `bplehb = denise_planes == 6 && !bplham && !bpldualpf && …`). En `BPLCON3`
el bit 9 es `KILLEHB` (AHRM Appendix C) y el bit 0 `ENBPLCN3` habilita las funciones ECS
(`drawing.cpp:2999`).

## Validación

- `demos/techniques/amiga/blitter/210_copper_blitter`: `BlitterJob` en el borde inferior copia 256 words; la copia
  se verifica (`RunStatus.detail = 0x21F00`) y el overlay muestra `copper blit: OK`.
- `tests/host/graphics/260_copper_blitter`: emisión (`BLTSIZE` al final) + ventana segura.
- `tests/host/graphics/446_copper_list_safety`: casos positivos/negativos de
  `check_copper_list`/`check_copper_column` y mapa de registros por chipset.

## Referencias

- `WinUAE-DBG/custom.cpp:2835-2846` (`test_copper_dangerous`); `custom.cpp:9225-9262` y
  `4416-4458` (decodificación/comparador); `custom.cpp:7316-7613` (`custom_wput_agnus`);
  `identify.cpp:135-395` (tabla `custd`).
- `docs/guides/roadmap/ROADMAP_BLITTER_COPPER.md` (Técnica A).
- `docs/reference/amiga/techniques/copper-timing-and-budget.md` §6.
- `docs/engine/architecture/COHERENCIA_DE_SETUP.md` §3 (dónde se usan estos validadores).
