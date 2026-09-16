# CONSULTA — Split vertical del corkscrew por encima de la línea 255 (límite OCS)

> **Estado: resuelta.** La limitación es de **hardware** (OCS/ECS/AGA): no existe un `WAIT` de
> Copper fiable en líneas raster ≥256, así que un split móvil no puede colocarse ahí. Para un
> viewport de scroll de 256 px la vía es `linear_display` (espejo). Referencia canónica:
> `docs/engine/architecture/AMIGA_8WAY_SCROLLING.md` §13 y
> `docs/reference/amiga/techniques/robocod-layered-scroll.md` §3.2. Este documento conserva el
> enunciado y la respuesta de la consulta a una IA externa que ratificó la conclusión.

## 1. Contexto

- Engine de juegos retro para **Amiga 500 (OCS, PAL, 68000, 312 líneas)**. Se valida en
  WinUAE-DBG (fork con `custom.cpp`/`coppercomp`) y con el flujo `build -> run -> analyze`.
- Técnica de scroll: **XYLimited tipo corkscrew** (port de `Scroller_XYLimited` de las
  *ScrollingTricks* de Steger), con buffer circular vertical y **split de Copper**.
- Convención de direcciones: `DIWSTRT = 0x2981` ⇒ **DIWSTRT_y = 41** (la ventana visible empieza
  en la línea raster 41).

## 2. El problema

Geometría del corkscrew (tile 16×16):

```
display_height = viewport_h + 2*tile_height                 // alto del anillo/bucle
display_offset = (videoposy + tile_height) % display_height // 0 .. display_height-1
split_line     = display_height - display_offset            // fila del wrap dentro de la ventana
split_active   = split_line < viewport_h                    // hace falta el split
raster(split)  = DIWSTRT_y + split_line = 41 + (display_height - display_offset)
```

Con viewport 256 (anillo 288), `raster` recorre **42..296**; cuando `display_offset ∈ [33,73]` el
split cae por encima de 255. El comparador de `WAIT` del Copper es de **8 bits** con semántica
`>=` (`vp = (vpos & 0xFF) >= vcmp`), así que el corte se recorta a la línea 255 y aparece una
banda de staging visible al pie (~14 % de las fases). Es el mismo degradado que sufre el
`XYLimited` original.

## 3. La pregunta

¿Existe alguna técnica de hardware para colocar un split móvil del Copper en una línea raster
**≥256** en OCS? En particular: ¿sirve el truco de anclaje del AHRM (`WAIT` a `$FF` + `WAIT` al
byte bajo)? ¿Cambia algo en ECS/AGA? ¿Hay alternativa (compare-enable, doble `WAIT`, reordenar la
copperlist)?

## 4. Respuesta de la IA externa

> **No.** En OCS (Agnus/Denise de un A500) no es posible colocar un `WAIT` de Copper que dispare
> de forma exacta y fiable en una línea raster concreta ≥256 (256–296 en PAL). El comparador
> vertical del Copper es estrictamente de **8 bits (V7–V0)**: no hay bit V8 en la instrucción
> `WAIT`/`SKIP` y la semántica es `>=` sobre esos 8 bits.
>
> **El truco del AHRM no resuelve el split móvil.** El contador vertical real llega a 312 (PAL),
> pero el comparador solo ve los 8 bits bajos; la línea 256 aparece como 0, la 257 como 1, la 296
> como 40, etc. Para ejecutar algo después de la 255 se usa el anclaje:
> `WAIT $FFDF,$FFFE` (espera a que `vpos & 0xFF == 0xFF`) y después `WAIT $xx01,$FFFE` con
> `xx = línea - 256`. Eso permite que el Copper **siga ejecutándose** en 256–311, pero el segundo
> `WAIT` sigue comparando solo el byte bajo: no distingue la línea 40 real (arriba) de la
> 296 = 40+256, y si el Copper llega al segundo `WAIT` **antes** de pasar la 255, la comparación
> `>=` se cumple de inmediato y el split se adelanta. El anclaje solo es seguro cuando el Copper
> ya está por detrás de la 255; sirve para el final del frame, no para un split móvil cuyo
> `raster = 41 + split_line ∈ [42,296]`.
>
> Las **compare-enable bits** tampoco ayudan: no se puede enmascarar V7 de forma útil para saltar
> el wrap y no existe un enable de V8 porque el hardware OCS no lo tiene en el comparador.
>
> **ECS / AGA.** En ECS (Agnus 8372) el comparador del Copper **sigue siendo de 8 bits**: lo único
> que aparece es V8/V9/V10 en `VPOSR` (lectura) y en `DIWHIGH` (rango de `DIWSTRT`/`DIWSTOP`), que
> el Copper no usa. En AGA (Alice) el comparador **también es de 8 bits**. Es una constante de
> diseño del Copper desde OCS hasta AGA.
>
> **Alternativas viables, por coste/beneficio (50 fps + Chip RAM limitada):**
>
> 1. **Campo de scroll 208 + HUD 48** (solución canónica de Steger/ACE/amiga-stuff): anillo 288,
>    el split nunca supera 255; cero artefacto, cero blits extra, Chip RAM mínima. Coste: 48 px de
>    área de juego.
> 2. **Espejo vertical (`linear_display`)**: el bitmap duplica el bucle, el wrap se lee contiguo y
>    no hace falta split. Coste: ~2× Chip RAM del bitmap y cada escritura incremental se blittea
>    dos veces (bucle + espejo). Alternativa pura para pantalla completa 256, paramétrica.
> 3. **Doble buffer completo + conmutación por `COP1LC`**: elimina el split móvil, pero 2× Chip RAM
>    del bitmap; solo viable con memoria libre.
> 4. **Clamp + compensación por software**: aceptar el artefacto o corregir la banda al pie;
>    normalmente peor que el espejo o el campo corto.
> 5. **Descartables**: reordenar la copperlist o usar `SKIP`/`COPJMP` para consumir el wrap (no
>    elimina la ambigüedad del byte bajo); cambiar `DIWSTRT`/`DIWSTOP` dinámicamente; HAM.
>
> **Recomendación**: si basta con 208 px de scroll, campo corto + HUD. Si se necesitan 256, espejo
> vertical (`linear_display`), midiendo el coste de los blits dobles. No invertir tiempo en forzar
> un `WAIT` ≥256 en OCS.

## 5. Conclusión y decisión

- El límite de **208 px con split móvil** (~215 con clamp agresivo) es real en **OCS, ECS y AGA**:
  no hay solución de hardware, solo **cambiar de técnica**.
- En el engine esto no cambia nada para los casos canónicos: **campo corto + HUD** (201/202/110/
  112) y **`linear_display`** ya implementado para pantalla completa (107, `K_LINEAR=1`).
- Si en el futuro se necesita un viewport de scroll de **256 px**, la vía recomendada es
  **`linear_display`**. Ver la entrada de roadmap correspondiente en `ROADMAP_UNIFICADO.md`.

## 6. Cómo verificarlo

- En WinUAE-DBG, observar el momento de los `MOVE` de `BPLxPT` (debugger de DMA o breakpoint en
  `coppercomp`).
- En hardware real: escribir un valor distintivo en `COLOR00` tras el `WAIT` del split y ver en
  qué línea real aparece; registros observables `VHPOSR`/`VPOSR` y los `BPLxPT` tras el split.
- En el engine: `split_always_waitable()` (`viewport_h + 40 <= 255`) y el fallo rápido del
  compositor con campo >214.
