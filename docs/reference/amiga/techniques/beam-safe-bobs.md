# BOBs de bitmap seguros frente al haz

Esta ficha fija el diseño correcto para actualizar **BOBs de bitmap en un único framebuffer** sin tearing ni restos, con el Blitter compitiendo con la DMA de display. Está pensada para OCS de 5 planos y para BOBs opacos de un plano, pero el invariante y la política de grupos son generales.

## 1. Invariante

«El haz está fuera de la caja en el instante de enviar el blit» **no es suficiente**. El invariante correcto es:

> Cuando el Blitter escribe la última palabra del rectángulo destino, el haz no está dentro de esa caja ni entra en ella mientras el blit sigue vivo.

Para una caja `[top, bottom]` en líneas de display, con `top_abs = DIWSTRT + top`:

```text
line >= bottom_abs + margin                 -> Now   (el haz ya pasó)
line + blit_lines + margin < top_abs        -> Now   (termina antes de que llegue)
line < top_abs                              -> Defer (demasiado cerca; conservar arte viejo)
resto                                       -> Wait  (el haz está dentro; esperar o diferir)
```

Un blit que empieza por encima de `top` pero termina después de que el haz entra en la caja debe ser `Defer` o `Wait`, nunca `Now`. La estimación de `blit_lines` debe ser pesimista.

## 2. Huella real de un BOB opaco de un plano

Para un destino **interleaved de 5 planos** con `row_bytes = 40`, una fila de pantalla ocupa `40 * 5 = 200` bytes. Un BOB de un plano que escribe en `plane p` debe avanzar 200 bytes por fila, no 40.

Valores correctos para el BOB actual (arte 8×8 dentro de hoja 12×12, desplazamiento fino):

| Registro | Valor | Motivo |
|---|---:|---|
| Canales | A + D | minterm `D = A` (`0xF0`) |
| `words_per_row` | 2 | imagen + guarda |
| `BLTSIZE` | `(height << 6) \| 2` | altura en píxeles; ancho 2 palabras |
| `BLTAMOD` | `sheet_row_bytes - 4` | hoja planar de 1 plano; 4 bytes por fila |
| `BLTDMOD` | **196** | `(40 * 5) - 4` = 200 − 4 |
| `interleaved` | false | un plano dentro de un destino interleaved |

`BLTDMOD = 36` solo sería válido para un destino planar con planos contiguos. En un destino interleaved, 36 hace que el Blitter caiga en el plano siguiente de la misma línea y corrompe los planos vecinos. Este valor está cubierto por **HOST-504**.

La hoja debe tener padding cero alrededor del arte (2 px por lado para pasos de hasta 2 px). Sin padding, la guarda contiene arte viejo después del shift y deja estelas diagonales. El footprint real es de **2 palabras (32 px)** cuando `x` no está alineado; colisión, beam y límites deben usar esa huella, no el tamaño del arte.

## 3. Calibración del coste

Método, una vez por máquina y modo de display:

```text
u16 start = current_raster_line();   // región tranquila o parada en VBlank
submit known blit (channels, words, height, BLTPRI state);
wait_blitter();
u16 end = current_raster_line();
measured_lines = (end - start) & 0x1ff;  // manejar wrap
```

Repetir para el tipo exacto de job (A+D, 2 palabras, un plano), dentro y fuera del display, con BLTPRI on/off. Guardar una tabla `(channels, words, inside/outside) -> lines` y usar una cota conservadora.

Medición actual en 5 planos, 320×256: **24 palabras → 6 líneas**, es decir, 4 palabras/línea. `kBeamWordsPerLine = 4`; `beam_group_decision` añade 2 líneas fijas y un margen de 4. No usar las constantes experimentales 48/3/1.

Presupuesto medido con BOBs de un plano, hoja 12×12 con padding:

| BOBs | fps | campos/frame |
|---:|---:|---:|
| 6 | 49,93–49,97 | 1,001 |
| 8 | 46,16 | 1,083 |
| 12 | ~32 | ~1,56 |

El límite práctico de esta técnica en un único buffer de 5 planos es **6–8 BOBs**; 12 no cabe sin reducir coste.

## 4. Unidad atómica y política

El grupo atómico es **un BOB = uno o más jobs que no se separan** (por ejemplo restore + save + draw; en el BOB opaco actual, un solo blit). El ejecutor nunca envía medio grupo.

- Los grupos se ordenan por `top` ascendente (deadline más temprano primero).
- Un grupo que ya está detrás del haz se ejecuta.
- Un grupo que cabe antes del deadline se ejecuta.
- Si no, se difiere entero y se conserva el arte anterior.
- Los grupos solapados forman un conjunto de conflicto: se difiere el conjunto o se aplica una prioridad estable.

La implementación actual (`execute_frame_plan_groups`) ya separa jobs no-BOB, recoge grupos, los ordena por `top`, aplica `Now/Wait/Defer` y expone `deferred_bobs`, `deferred_groups`, `max_lateness` y `max_group_lines`. No mantiene todavía un set persistente de grupos pendientes: la demo vuelve a emitir el plan cada frame, que es suficiente para BOBs opacos sin save-under.

## 5. Grupos diferidos

Si un grupo se difiere:

- no se toca el bitmap; el arte anterior permanece completo;
- el plan del frame siguiente lo vuelve a emitir en la posición actual;
- para BOBs con save-under hay que conservar la caja, los punteros, el shift, los módulos, el id y una edad; no se puede reutilizar un save-under desincronizado;
- un deadline suave (`age > N`) puede forzar `Wait` para que un BOB hambriento aparezca, aceptando un posible tear corto.

Telemetría recomendada: `deferred_groups`, `max_lateness`, `groups_executed`, `total_words_blitted`, `ever_deferred` y edad por grupo.

## 6. Verificación de BPL y copperlist

Nunca usar `BPLxPTH/PTL` leídos durante display activo como base estable: Agnus los actualiza con la DMA corriente. Métodos válidos:

1. parar en VBlank o en una línea de blanking conocida y volcar `COP1LC` y la copperlist;
2. watchpoint de escritura sobre los cinco `BPLxPT` y `COP1LC`;
3. test estático con canarios, dos BOBs separados y píxeles esperados (`007_bob_plane_base`);
4. volcado por canal lateral solo si `VPOSR` está fuera de la ventana visible.

## 7. Pruebas y gates

- **HOST-504**: `BLTDMOD=196`, footprint de 2 palabras, caja beam.
- **007_bob_plane_base**: dos BOBs estáticos, canarios antes/después del bitmap y de la hoja, `detail=31`.
- **210**: seis BOBs con padding, secuencia frame-step sin restos ni barras, 49,9 fps; 8 con el scheduler actual baja a 33,3 fps y muestra tearing (ver §7.1).
- Cualquier cambio de este camino debe pasar el [protocolo de cambios de render](../../../guides/methodology/RENDER_CHANGE_PROTOCOL.md).

## 7.1 Límite medido (2026-10-30)

Medición sobre `210` con hoja 12×12 (arte 8×8, padding 2), 5 planos y fondo plano:

| Configuración | fps | Campos/frame | Resultado visual |
|---|---:|---:|---|
| 6 BOBs, sin grupos (`L0`) | 49,9 | 1,00 | limpio |
| 6 BOBs, grupos beam-aware | **42,7** | 1,17 | **banda en la base** |
| 8 BOBs, grupos beam-aware | **33,3** | 1,50 | **banda/tearing en la base y flicker** |
| 8 BOBs, sin grupos (`L0`) | 45,8 | 1,09 | limpio, pero **sin guarda de haz** |

Cada grupo de 2 palabras × 12 filas mide **13–14 líneas de raster** (`max_group_lines`), no las 24 palabras teóricas: con 5bpp el Blitter solo obtiene ~3 palabras por línea. El perfil de 8 BOBs reparte el frame en ~62k ciclos de emisión, ~44k de ejecución, ~18k de física/colisiones y ~20k de HUD; el frame supera un campo. El `D0/L14` confirma que no se difirió ningún grupo: el problema es el coste, no `BLTDMOD` (HOST-504 sigue pasando).

La banda de la base **desaparece si se borra el bitmap completo antes del frame**: es un **fantasma/resto de posiciones anteriores**, no un blit nuevo mal recortado. El demo actual no borra la caja previa; el padding del BOB solo limpia dentro de la huella nueva. Con grupos el frame cambia de ritmo y el fantasma se hace visible en la base.

Conclusión: **el camino con grupos no se acepta todavía, ni con 6 ni con 8**; 6/8 sin grupos son solo referencias legacy limpias, no beam-safe. Antes de subir el límite hay que resolver el borrado de la caja previa y el coste de sincronización, no subir `kBeamWordsPerLine` a ojo.

## 8. Límites y alternativas

Manteniendo BOBs de bitmap:

- modelo de coste calibrado + grupos ordenados: recupera margen sin cambiar el arte;
- reducir a 4 planos o usar DPF con BOBs en el campo de menor DMA: más slots libres, menos color;
- blits disparados por Copper: timing exacto, copperlist más compleja;
- dirty spans verticales: solo las líneas cambiadas, más contabilidad;
- doble buffer de 5 planos 320×256: elimina la restricción de haz, a cambio de ~100 KB y tráfico de flip; no es necesario para 6–8 BOBs.

## 8.1 Restricciones compile-time/setup para subir el límite

No basta con `static_assert(N <= 8)`: hay que reducir el coste real y rechazar schedules imposibles antes de mostrar basura.

- **Compilación**
  - `static_assert(kBobCount * kMaxWordsPerBob * kMaxRows <= kBeamFrameWords)` con el presupuesto calibrado, no con el teórico.
  - exigir `kWordsPerRow == 1`: BOB de una palabra, `x` alineada a 16 y arte ≤16 px; elimina la guarda y casi duplica el presupuesto de grupos.
  - limitar altura (≤8–10), un solo plano y `BobDraw::Opaque`; el backend no debe aceptar mallas de varios planos por BOB en este camino.
  - capacidad fija de grupos (`kMaxBeamGroups`) y de jobs; sin arrays locales de tamaño variable.
- **Setup**
  - construir una tabla de grupos por banda de raster y validar `sum(ceil(words/3)+2+margin)` contra el campo; si no cabe, fallar el setup (no arrancar).
  - rechazar solapes de cajas y BOBs fuera de la ventana `top/bottom`; la huella debe salir de `bob_footprint`.
  - reservar el **último tercio** del campo para el HUD/Copper y no programar BOBs que crucen el final visible.
  - medir una calibración con VPOSR en el setup y fijar `kBeamWordsPerLine`/margen por máquina, no por estimación.

Con 8 grupos actuales el presupuesto no cierra; las restricciones anteriores son las que pueden acercarlo a 50. Sin ese rediseño, el límite seguro es **6 BOBs** (8 solo legacy/sin guarda).

## 9. Fuentes

- AHRM 3.ª: blits sincronizados con el haz, `VPOSR`, Copper WAIT/SKIP, prioridades DMA.
- WinUAE-DBG: `custom.cpp` (`VPOSR`, `BLTSIZE`), `blitter.cpp` (arranque/estado), `drawing.cpp` (fetch por línea).
- Código: `engine/include/eng/graphics/beam_schedule.hpp`, `engine/include/eng/graphics/bob.hpp`, `engine/src/platform/amiga/amiga_blitter.cpp`, `demos/techniques/amiga/playfield/210_beam_aware_bobs/src/main.cpp`.
- Consulta y respuesta de Grok: `docs/debugging/investigaciones/consultas/consulta-beam-aware-bobs-en.md`.
