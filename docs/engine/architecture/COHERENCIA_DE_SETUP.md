# Coherencia de setup: que los cabos sueltos no lleguen a pantalla

Este documento define el **sistema de coherencia de configuración** del engine: cómo se evita que una incoherencia entre lo que una demo *cree* configurar y lo que *realmente* programa en el hardware pase en silencio hasta verse como basura en pantalla (o no verse). Nace de dos fallos reales de la demo 066 y del estudio de cuántos casos similares existen hoy.

## 1. Los fallos que lo motivan

- **«Plano fantasma» (066)**: la demo emitía `BPLCON0` con `BPU=5` mientras la lista solo cargaba punteros de 4 planos. El 5.º plano leía `BPL5PT` (basura) y aparecían colores 16-31 espurios. El valor estaba **cableado a mano** en la demo, divergiendo de su fuente de verdad (el nº de planos).
- **«L0/S0» (066)**: `execute_frame_plan_async` devolvía `false` porque el **slot de IRQ de blit** ya estaba ocupado por el bombeo de fondo que instala `Engine::run_frames`. El resultado se ignoraba y la cadena de blits no se ejecutaba: la escena parecía congelada sin ningún aviso.

Ambos comparten la misma clase: **un valor/resultado que debía ser coherente con otro se decidió en un sitio distinto y nadie lo verificó**.

## 2. Taxonomía y catálogo actual (estudio)

| Clase | Casos encontrados (2026-10) | Estado |
|---|---|---|
| **A. Constante/derivación duplicada** — el mismo valor se calcula en varios sitios y puede divergir | `BPLCON0` calculado a mano en **8 sitios** (3 del engine: `strip_composer`, `xlimited_composer` ×2, `xlimited_scene`; 5 demos: 070, 071, 113, 114, 115) + el cableado de 066. `bplcon0_for` existía pero no se usaba en ninguno | **Corregido**: `bplcon0_for` es el constructor único (movido a `composition/limits.hpp`, ampliado a DPF) y todos los sitios lo usan |
| **A′. Estado duplicado que debe ser UN mismo global** — dos nombres para lo mismo, uno sin asignar | `takeover_system` guardaba `OpenLibrary("graphics.library")` en `g_sys.gfx` mientras las macros del SDK (`LoadView`/`WaitTOF`/`OwnBlitter`) llaman por el **global `GfxBase`** (a 0) → `JSR (a6,-0xde)` con `a6=0`, PC `0xFFFFFF22`: **crash real de la 066** visible solo como «no llega a READY» | **Corregido**: `GfxBase` es la fuente única en takeover/restore; documentado en `amiga.cpp` |
| **B. Resultado falible ignorado** — una operación puede fallar y nadie lo mira | ~25 funciones `bool` de setup/servicio en `backend.hpp`; ignoradas en demos: `execute_frame_plan` (078, 275 ×2, 117), `set_blit_service` (080), `background_timer_start` (081), `set_blit_service` del engine. Y el **`bob_draw` que devuelve `false` en silencio** para combinaciones no cubiertas (cookie-cut interleaved con máscara suelta: caminante y patrullero de la 066 sin dibujar) | **Corregido**: `[[nodiscard]]` en la API de setup/servicios + `-Werror=unused-result`; `check_bob_plan` (capa 2) detecta la combinación no cubierta en setup |
| **C. Contrato de layout no verificado** — offsets/tamaños calculados a mano que pueden rebasar su bloque | hojas de BOB de 066 (suma de offsets contra un bloque de 12288 B), bloques de arena de todas las demos. **El propio mapa cazó un bug de unidades real**: `sheet_advance` sumaba el offset en palabras a una dirección en bytes → regiones a mitad de camino y solapadas (`0x7C001`) | **Corregido en 066** (mapa de regiones con solape parcial); el resto de demos puede adoptarlo al tocarlas |
| **D. Orden/tiempo de hardware no verificado** — la lista es sintácticamente válida pero el hardware la ejecuta distinto | WAITs del Copper que retroceden (fallo real de 065: bandas que «no se veían»); armado de sprites antes de `VSTART` | **Corregido** el orden de WAITs (validador + chequeo en setup de 066); el arming queda cubierto por la regla y el chequeo visual |
| **E. Recurso exclusivo compartido** — dos dueños del mismo slot (IRQ, DMA, registro) | slot de IRQ de blit (bombeo de fondo vs feeder async: el fallo «L0/S0»); slot de audio; timer de CIA | **Mitigado**: `has_blit_service()` + contador de conflictos + contrato documentado; la exclusividad sigue siendo responsabilidad del llamador |

## 3. Las tres capas

```text
  capa 1  COMPILACIÓN   tipos + constructor único + [[nodiscard]] + -Werror=unused-result + static_assert
          │             lo que se puede decidir en compilación, no compila si está mal
  capa 2  SETUP         eng/setup/coherence.hpp: regiones, BPLCON0, orden de WAITs (chequeo runtime barato)
          │             lo que depende de valores runtime, falla ANTES de mark_ready con código y nombre
  capa 3  TESTS HOST    los validadores puros se prueban con los fallos históricos como casos
                        una regla no puede degradarse sin que un test falle
```

### Capa 1 — el compilador

- **Constructor único por registro crítico**: `composition::bplcon0_for(mode, planes)` es el único camino a `BPLCON0`; el campo nunca se cablea a mano (regla de revisión: un literal `0x4?00`/`0x5?00`/`0x6?00` en código nuevo es sospechoso de duplicar el constructor).
- **`[[nodiscard]]` + `-Werror=unused-result`**: toda operación de setup/servicio que devuelve `bool` es **error de compilación** si se ignora. El escape explícito es `(void)` **con comentario del motivo**.
- **`static_assert` sobre los validadores puros**: cuando la configuración es `constexpr` (el caso de las demos con `kPlanes`), el chequeo se congela en compilación:
  ```cpp
  constexpr eng::u16 kBplcon0 = composition::bplcon0_for(SceneMode::Standard, kPlanes);
  static_assert(eng::setup::check_bplcon0(kBplcon0, kPlanes).ok());
  ```
- **Formas parciales de operación de hardware: no existen.** Si el hardware exige varios elementos acoplados, la API los pide **todos** (tipados) y no publica variantes con un subconjunto (AGENTS §1.10): armar/rearmar un canal de sprite exige `POS`+`CTL`+**`SPRxPT`→estructura tipada** (`emit_sprite_horizontal_rearm(r, ChipView<SpriteTag>)`; la variante POS+CTL+DATA sin PT — que sí existió — era **incorrecta por construcción**: el fetch del DMA pisa los registros, `winuae/sprite-dma.md` §Armado de un canal, bug 087/223) y `SpriteLineLayer::configure` **rechaza** configurarse sin su tabla de DATA (tipo obligatorio + tamaño validado). Al diseñar una API, la pregunta de gate es: «¿puede el llamador configurar esto a medias?» → si sí, cerrar la forma (tipo/argumento) o añadir validador.
- Ya existía para escenas: `composition::valid_scene(res, limits)` (`consteval`) valida `SceneResources` contra el perfil de la máquina.

### Capa 2 — setup (runtime, antes de `mark_ready`)

`eng/include/eng/setup/coherence.hpp` (puro, `constexpr`, sin hardware ni heap):

- **`RegionTable<Max>`**: la demo registra bloques de arena **y sub-regiones** (hojas de BOB, etc.) con nombre, dirección y tamaño. `validate()` detecta **solape parcial** (una hoja que rebasa su bloque e invade la siguiente; el copper encima de los planos), permitiendo **contención** padre-hijo; `validate_within()` añade el límite del banco. Un fallo se convierte en `mark_failed` con código (`0x0007c0nn`) en vez de basura en pantalla.
- **`check_bplcon0` / `check_bplcon0_mode`**: `BPU` debe casar con los planos emitidos; bit COLOR presente; HAM=6 planos; DBLPF par; EHB=6 planos.
- **`check_bob_plan`**: la combinación de campos de un `Bob` (draw/layout/mask_pack/máscara) está cubierta por el ejecutor; si no, `bob_draw` devolvería `false` en silencio (caso real: caminante/patrullero de 066 sin dibujar).
- **`check_copper_wait_order`**: los WAIT de la lista emitida no pueden retroceder (un WAIT «ya pasado» espera al frame siguiente: el efecto desaparece). Entiende el par de overflow PAL (`0xffdf`/`0xfffe`) y el `END`.
- **`check_sprite_line_data`**: el parcheo de `SPRxDATA`/`DATB` a media línea (patrón Brian the Lion) exige ≤5 planos de display (sin hueco de DMA no llega el ciclo del sprite; `custom.cpp:10111`/`10130-10131`); `SpriteLineLayer::configure` lo aplica.
- **`check_sprite_line_hpos`**: en un **rearmado por línea** la X del primer tramo debe caer **tras el fetch de sprites** (`kSpriteLineMinHstart` = 0x81 + `DDFSTRT`/2; `custom.cpp:10111`); `SpriteLineLayer::configure` lo aplica solo con `per_line_rearm` (el armado único no tiene esa carrera).
- **Servicios exclusivos**: `has_blit_service()` dice si el slot de IRQ de blit está ocupado; `blit_service_conflicts()` cuenta los rechazos (la incoherencia queda diagnosticable).

Regla operativa: el `init` de una demo termina con los chequeos de coherencia de lo que ha construido; solo entonces `mark_ready`.

### Capa 3 — tests host

`tests/host/debug/441_setup_coherence` ejercita cada validador **con el fallo histórico como caso** (BPU=5 con 4 planos, WAIT que retrocede tras el de las bandas de 065, hoja que rebasa el bloque, región fuera del banco). Si un validador se relaja, el test cae.

## 4. Qué NO cubre (y su vía)

- **Registros fuera de la lista de validadores** (DDF vs DIW, paleta vs planos): el patrón es extensible; añadir un validador puro + su caso en HOST-441.
- **Errores de arte** (valores de píxel fuera de la paleta): cubierto por HOST-439 y la validación visual.
- **Armado de sprites fuera de ventana**: la X del rearmado por línea ya se valida (`check_sprite_line_hpos`); el resto del contexto de raster (VSTART vs armado) queda para el `SpriteScene`/drivers.
- **Chequeo de todas las llamadas falibles del backend** (blitter_* individuales): hoy `[[nodiscard]]` cubre la API de setup/servicios; ampliarlo al resto es mecánico y puede hacerse por tandas (cada tanda = arreglar los ignorados con `(void)`+motivo o con manejo real).
- **`(void)` como escape**: es deliberadamente visible en revisión; un `(void)` sin comentario del motivo es un hallazgo de revisión.

## 5. Referencias

- Validador: `engine/include/eng/setup/coherence.hpp`; test: `tests/host/debug/441_setup_coherence/`.
- Constructor único: `bplcon0_for` en `engine/include/eng/graphics/composition/limits.hpp` (junto a `SceneMode` y `validate`/`valid_scene`).
- Casos: `docs/debugging/investigaciones/066-async-blit-irq-bajo-el-so.md` (plano fantasma, L0/S0) y la lección de orden de WAITs de 065 (`PROCEDIMIENTO_DEMOS_Y_JUEGOS.md` §3.15-20).
- Enforce de compilación: `tools/build/build-demo.sh` (`-Werror=unused-result`).
