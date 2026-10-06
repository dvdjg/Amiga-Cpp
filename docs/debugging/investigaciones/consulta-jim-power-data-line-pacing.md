# Consulta (castellano): Jim Power — DATA por línea + POS repetido (pacing del Copper)

> Registro local; se envía a Grok en inglés
> (`consulta-jim-power-data-line-pacing-en.md`), como exige `AGENTS.md` §1.3. Demo afectada:
> `demos/techniques/amiga/sprites/215_jim_power` (NO VERIFICADA).

## Contexto

OCS/A500, playfield de 4 planos 320x256, `BPLCON2=$0024`. Objetivo: reproducir el fondo de
**Jim Power**: **dos sprites no attached** (canales 6 y 7) forman un patrón de **4 colores** de
**32 px**; el Copper, **por línea**, (a) carga la **DATA** de cada canal y (b) **reposiciona**
cada canal a lo ancho (`SPR6POS`, `SPR7POS`, paso 16 px) para repetir el patrón hasta cubrir
320 px. La DATA se **anima por línea** (distinta por línea y por frame; sin pre-shift).

Funciona: canales alimentados por Copper con estructuras DMA OFF válidas y `CTL` de banda
(comparador vivo); parcheo de DATA por frame; primer `WAIT` en `arm_hpos=$40` (tras el fetch
DMA, como 207/213) para que el DMA no pise la DATA.

## Pregunta

Secuencia **exacta** por línea (MOVEs/WAITs y head-starts) y reglas de orden para cubrir
320 px con dos canales de 16 px desde un patrón de 32 px, cargando la DATA una vez por línea y
repitiendo solo `SPRxPOS`:

1. **Orden y head-start**: sabiendo que **1 MOVE = 8 px lores** de haz, con período de 32 px y
   2 canales el Copper va ~2x más rápido: ¿`WAIT` cada N períodos con qué head-start, período
   de 16 px (1 MOVE por canal y columna, alternancia estricta) o DATA por columna?
2. **Punto de carga de DATA**: ¿en la primera columna del canal en la línea (arma el shifter
   en esa X) o una vez arriba de banda y reutilizada, reescribiendo solo POS por línea? ¿Qué
   estado de `SPRxCTL`/`SPRxPT` exige el re-armado POS-only para un canal **no attached**?
3. **Fallos observados**: (a) burst con todos los POS sin WAITs → solo la **última** X visible;
   (b) `WAIT` por período (receta de la 208, `x-32`) → pinta la primera columna y el Copper se
   cuelga en el siguiente WAIT (llega tarde). ¿Fórmula correcta del head-start?
4. Si el caso 32 px/2 canales exige **4 canales** o período de 16 px para ir pareado con el haz
   (`2N MOVE = period`), decirlo y dar la disposición mínima para cubrir el ancho.

Pedir un fragmento de copperlist de referencia y citar la regla documentada (AHRM cap. 4
«Reusing Sprite DMA Channels», análisis de Jim Power de codetapper).

## Seguimiento con resultados medidos (tras aplicar la receta)

Implementado: por línea `WAIT (vpos, hpos $20)`; DATA de cada canal **servida por el DMA**
una vez por línea (estructura con cabecera y `CTL` de banda; sin MOVEs de DATA que compitan
con el fetch) + **ráfaga pura** `$40..$d8` (+$8, sin WAITs intermedios), 160 líneas,
`BPLCON2=$0024`, 4 planos, DDFSTRT=$38.

**Medido: solo se pintan las ~2 últimas columnas de 20.** Añadir/quitar 4 MOVEs dummy tras
el `WAIT` (simulando la recarga de DATA real) no cambia el resultado: el Copper adelanta al
haz mucho más de un período. Preguntas de seguimiento: coste real de un `MOVE` con 4 planos
+ DMA de sprites activos (robo de bus), head-start correcto (¿empezar *detrás* del haz es el
régimen real?), por qué el `WAIT` varía por línea ($1c/$20/$24) y si el fallback correcto es
`POS+DATB+DATA` por columna (Free Form, demo 213).
