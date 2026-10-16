# VALIDATION — 121_proc_corner_layers

Informe de validación (F3/F4) de la demo del **autotiling por esquinas (dual-grid)** y el
**mapa por capas con actor que pasa por debajo de las copas**. Todo lo medido es sobre
`A500_debug` (warp) con la escena generada en compilación; el alcance de cada afirmación
está acotado en §5.

## 1. Intención (F0)

Escena estática de 320×240 (20×15 tiles de 16 px, 32 colores) compuesta por capas —
río de norte a sur y lago con isla por transiciones de esquinas; flores (decal) y copas
(overhead con transparencia)— y un **bicho blanco** (único objeto en movimiento, color
`#eeeeee`, exclusivo) que camina por el carril (fila 11, y=176) y **pasa por debajo de las
copas**: al cruzar cada copa su cuerpo queda total o parcialmente oculto y reaparece al
salir. Criterio de éxito visual: cuerpo completo → tapado por la copa (trozo oculto) →
visible; río y copas siempre presentes; sin parpadeo.

## 2. Gates técnicos (F3)

- `bash tools/test-regression.sh --demo demos/techniques/amiga/effects/121_proc_corner_layers --warp` → **Regression OK**
  (`out/regression/20261016-151144/regression-report.md`: build/run/analyze ok).
- `node tools/debug/measure-fps.mjs 121_proc_corner_layers A500_debug --json` →
  **49,37 fps / 1,013 campos/frame / 143.683 ciclos/frame** (989 frames, 20 s;
  `detail=0x100b9` = actor x=185, dir=+1). La cadencia incluye el amortiguamiento de la
  carga (C2P única del fondo + parcheo de la franja), como la 119 (49,85 fps).
- **Presupuesto por elemento**: el único trabajo por frame es el parcheo planar del
  actor (restauración del carril 16 palabras × 16 filas × 5 planos = 1.280 palabras +
  máscara combinada 32 palabras + escritura enmascarada) y el contador del HUD (1–2
  glifos por parcheo de texto); no hay C2P ni composición por frame. Rutas no frecuentes:
  reversión del sentido (misma ventana) y cruce de palabra (ventana contigua) — sin pico.
- `bash tools/test-regression.sh --flicker --require-flicker-ok --demo ... --warp` →
  **Flicker ok** (`out/vision-review/121_proc_corner_layers/`): 0 candidatos del detector
  temporal; frame-diff 159/159 pares con cambio, bbox del cambio avanzando 2 px (2x) por
  frame = **1 px Amiga/frame** (el actor); `flicker-baseline.json` declarado
  (`max_candidates: 0`).
- Tests host del dominio: `HOST-442/443/444/445/448/450` en verde (incluye la banda
  planar y la equivalencia de la variante `Span` de composición).

## 3. Contratos objetivos (secuencia de 160 frames, paso de 1 frame juego)

`node tools/analyze/check-elements.mjs <seq> "#eeeeee,#1144cc,#007700" --expect "#1144cc=2000" --expect "#007700=150"` → exit 0 (presencia de río y copas en **todos** los frames).

| Medición (por frame) | Resultado |
|---|---|
| Blanco del actor (`#eeeeee`) | 552 estable → **mínimo 228** (41 %) en `frame_051_f0080` → recupera 552 |
| Copas (`#007700`) | **2108 constante** (fondo íntegro, sin erosión a su paso) |
| Río (`#1144cc`) | presente en los 160 frames (≥ 5.400) |

Perfil del blanco (cada 8 frames): `552 552 552 552 552 536 268 344 264 384 552 552 552
552 552 536 268 380 552 552` — **dos dips** por barrido, uno por grupo de copas del carril.

Bbox del actor (análisis propio del PNG, escala 2x del capture):

| frame | blanco | % del máximo | copa dentro del bbox del actor | x bbox (2x) |
|---|---|---|---|---|
| `frame_000_f0029` | 552 | 100 % | 0 | 202–229 |
| `frame_051_f0080` | 228 | 41 % | **256 px** | 304–331 |
| `frame_075_f0104` | 484 | 88 % | 108 px | 354–379 |

El desplazamiento del bbox entre f0029 y f0080 es **+51 px Amiga en 51 frames = 1 px/frame
exacto** (contrato de movimiento, medido sobre el color exclusivo del actor).

## 4. Visión (Ollama local, `qwen3-vl:8b-instruct-q8_0`, protocolo de 3 pases)

### A · Inventario sin contexto (`frame_000_f0029.png`)

Prompt: «Describe qué se ve en esta imagen: formas, colores y estructuras que reconozcas.
No adivines el propósito, describe solo lo observable.»

Respuesta (cruda, extracto): «…una franja vertical [azul] en el lado izquierdo, y otra
mancha irregular en la parte derecha, con un hueco en el centro. […] un pequeño personaje
blanco con dos puntos negros (ojos) en la parte inferior izquierda, cerca de la franja de
agua. […] detalles pequeños en rojo, que parecen ser puntos dispersos sobre el suelo
verde. […] texto en blanco que dice: "PROC CORNER LAYERS v1.0.402F000030"».

Interpretación: identifica río continuo, lago con isla (el «hueco en el centro»), las
flores («puntos en rojo»), el actor y la franja de versión — **sin que se le dijera qué
esperar**.

### C · Verificación dirigida (recortes ampliados 3x alrededor del actor; el modelo falló con el actor pequeño en el frame completo y se repitió con detalle, regla 21)

- `frame_001.png` (= `frame_051_f0080`, blanco al 41 %): «El cuerpo blanco del bicho **NO
  está completo**. Le falta una parte: **la parte inferior derecha del cuerpo**, que está
  **tapada por una mancha verde oscura** (una mancha que representa una copa de árbol).
  […] Se ve la mitad izquierda del cuerpo, y la mitad superior derecha.»
- `frame_002.png` (= `frame_075_f0104`, blanco al 88 %): «…está **casi completo**, pero le
  falta un trozo visiblemente tapado por una copa verde oscura. […] La copa está situada
  **a la izquierda del bicho**, cubriendo parcialmente su lado izquierdo y la parte
  inferior izquierda» (consistente: el actor va hacia la derecha y sale de la copa por su
  lado izquierdo).

### B · Seguimiento (posición en el encuadre, un frame por llamada)

- `frame_000_f0029`: «en la parte inferior **izquierda**, cerca de la franja de agua».
- `frame_075_f0104`: «**en el centro**, más precisamente en la mitad inferior derecha».

Contraste con lo objetivo: el bbox avanzó +51 px Amiga (izquierda→centro) ✓.

## 5. Conclusiones y alcance

- **Autotiling por esquinas VERIFICADO en hardware en su alcance**: las orillas del río y
  del lago se describen como orgánicas y continuas (visión), sin huecos; las copas son
  estáticas e íntegras (conteo de color constante) y las transiciones presentes en los
  160 frames (gate de presencia).
- **Mapa por capas + actor por debajo de las copas VERIFICADO en hardware en su alcance**:
  el blanco del actor cae de 552 a 228 px (41 %) **con 256 px de copa dentro de su bbox**
  justo bajo la copa y recupera al salir; el modelo describe el trozo oculto y su lado
  (inferior derecho al entrar, «casi completo» al salir). El movimiento es 1 px/frame
  exacto y el fondo no se erosiona (copas constantes tras el fix de la banda de carril).
- **Marco**: 49,37 fps (1,013 campos/frame) y 0 candidatos de flicker; el único cambio
  por frame en el frame-diff es el actor y el contador del HUD.
- **SIN VERIFICAR**: (a) los 16 desplazamientos del actor solo se ejercitan en los
  subpíxeles recorridos por el barrido capturado (todo el rango, pero sin forzar cada
  valor individualmente contra un golden); (b) el camino `restore/patch` con ventanas
  solapadas al **cruzar palabra** se cubre en el barrido (el bbox avanza 1 px/frame
  cruzando múltiplos de 16) pero no se ha instrumentado un contador de fronteras;
  (c) el C2P de 5 planos ya estaba verificado por la 119 (no se re-verifica aquí), y el
  display de 6 planos sigue sin demo.
- **Deuda abierta**: ninguna nueva de la demo. Los hallazgos m68k de este hilo están en
  `ROADMAP_DEUDA_TECNICA.md` DT-018/DT-019 (cerrados en esta pasada).
