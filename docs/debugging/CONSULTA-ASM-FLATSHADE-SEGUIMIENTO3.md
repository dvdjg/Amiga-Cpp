# Seguimiento 3 para grok — cámara corregida, visibilidad OK, draw sigue negro

**Tu diagnóstico era correcto** y lo apliqué.

## Fix aplicado

En `fs_update_face_visibility` recargo la cámara **cada cara** (d2/d3/d4 se usan de temps):

```asm
	move.w	86(a3),d2			/* cx */
	move.w	88(a3),d3			/* cy */
	move.w	90(a3),d4			/* cz */
```

## Resultado: la visibilidad ya está bien

A `m_angle` variable, contando aristas con `flag != 0`:
- **antes**: 1
- **después**: **24** (C++ ~21-25 segun angulo).

Así que `fs_update_face_visibility` / `fs_update_edge_visibility_convex` ya producen flags
sanos (mismo orden de magnitud que C++). El off-by-1-2 y el culling de `f2210` deberían
haber desaparecido con `cz` estable.

## PERO el render SIGUE negro

- Histograma de una captura: **solo fondo** `(0,17,34)` (430k px) + un pequeño bloque
  `(0,0,0)` (5k px). **0 píxeles con color de balón.**
- El profiler dice `edges=0 nEdges=0 nLines=0` en el frame negro.
- La demo **no crashea**: `state=Ready`, `frames` avanza.

Es decir: **los flags de arista están puestos (24) pero `draw_edges` encuentra 0** (o no se
pinta nada). Es tu hipótesis 2 (pipeline) o algo del render.

## La duda concreta

Pipeline por frame (demo):

```
1. wait_blitter
2. install_copper_list(show)        // muestra el buffer dibujado el frame anterior
3. draw_edges(buf)  -> lee edge->flags, dibuja, LOS PONE A 0, y setea nEdges/nLines
4. area_fill
5. m_angle += 8; update_object_transformation; face/edge/transform  // edge_visibility SETEA los flags
6. pre-clear
```

Con `m_angle` **constante** (`m_angle = 1000`) el estado debería repetirse, y aun así el
render negro persiste con 17-24 aristas marcadas. Y leyendo `g_eng_prof.v[10]` (`nEdges`)
da **0**.

**Preguntas:**

1. En el pipeline de arriba, `draw_edges` (paso 3) lee los flags que puso el paso 5 del
   frame **anterior**. ¿Hay algún motivo para que en el frame del draw esos flags ya estén a
   0 (p. ej. que `draw_edges` corra antes del primer `edge_visibility`, o que el buffer que
   se dibuja no sea el que se pre-limpió)? ¿Cómo lo verificarías (volcar `nEdges` en varios
   frames, o un watchpoint sobre el primer `edge->flags` en `draw_edges`)?
2. Dado que los **flags de arista** son correctos pero `nEdges=0`, ¿el problema es que el
   **contorno se dibuja con `vertex.x/y` mal** (los vuelca el transform) o que el **Blitter
   no pinta** (registros/copper)? ¿Qué mirarías primero: los `vertex.x/y` que consume
   `draw_edges`, o los registros del Blitter (`BLTCON`/`BLTCPT`/`BLTSIZE`) en el frame?
3. ¿Puede ser que el ASM haya dejado el **Blitter o el copper en mal estado** en el frame
   anterior (el transform no los toca, pero cambia el timing del pre-clear), de forma que el
   `draw_edges` del frame siguiente no pinte? (Descarté el negro "limpio permanente" antes,
   pero ahora que la visibilidad es correcta, encaja más.)

Repro: `-DK_FLATSHADE_ASM=1` sobre `demos/amiga/116_flatshade_convex`.
