# VALIDATION — Demo 085 (`copper_plan_scene`)

Informe de validación (F4/F5 de `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md`).
**Estado: NO VERIFICADA** — deuda abierta en `docs/guides/roadmap/ROADMAP_DEUDA_TECNICA.md` **DT-007**.

Intención esperada: escena con `copper::Plan`: cielo de 16 franjas de `COLOR00` en degradado que
cicla y un BOB (disco) que se mueve en Lissajous aportando intenciones de color ancladas a su Y.

## Mediciones

| Medida | Comando | Resultado | Lectura |
|---|---|---|---|
| fps debug | `node tools/debug/measure-fps.mjs 085_copper_plan_scene A500_debug --json` | **16,62 fps · 3,01 fields · 426.733 ciclos/frame** | 3× el presupuesto → NO VERIFICADA por F3 |
| elementos (8 frames de paso `fNNNN`) | `node tools/analyze/check-elements.mjs out/run/085_copper_plan_scene/A500_debug/sequence ffffff,00ff00,ff0000` | blanco **alterna 1088 ↔ 2204** (frames 1 y 5 doblan); rojo estable 324 | sospecha: el contorno blanco del disco se duplica/marca de más en ciertos frames |
| captura | `run-demo.sh … --sequence-step-frames 8` | `frame_NNN_fNNNN.png` consecutivos | determinista (usa `eng_debug_ready_probe`) |

Hotspot candidato (evidencia previa `asm-audit`): `eng::graphics::effects::RasterGradientEffect::rebuild()`
(peso 16, 2 `mul` y 2 `mod`) — el `mod` en el bucle del cielo es caro; confirmar con perfil/ablación en F1.

### Ablaciones F1 (medidas)

| Ablación | fps | fields | ciclos/frame | Atribución |
|---|---|---|---|---|
| baseline | 16,62 | 3,01 | 426.733 | — |
| fase del cielo congelada (sin `rebuild` por frame) | 16,62 | 3,01 | 426.733 | `rebuild()` **no** es el hotspot |
| sin intents del BOB | **24,99** | **2,00** | **283.920** | el BOB cuesta **~143k ciclos/frame** (33%) |
| — | — | — | 284k restantes | base (plan + 16 intents de cielo + materialize/commit) **también 2× presupuesto** |

Conclusión F1: hay **dos** focos de coste (BOB ≈ 143k y base del plan ≈ 284k); el ciclo del cielo (`rebuild`)
queda descartado. El blanco alternante (1088↔2204) pasa a constante 1088 con la fase congelada: era el
**ciclo de paleta del disco** (animación esperada), no corrupción → la «mancha blanca» de la visión es
probablemente contenido del disco en ciertas fases.

## Prompts y respuestas (crudos)

### A · Inventario sin contexto, una imagen (frame 4)

**Prompt:** «Es UNA captura de una animacion. No conoces el programa […]: 1) Inventario de elementos (forma, color, tamaño, cuántos). 2) ¿Algo que parezca basura, ruido, corte o zona incoherente? 3) ¿Hay un objeto nítido con forma propia sobre el fondo? Descríbelo. No inventes coordenadas.»

**Respuesta** (cruda, extracto literal):
> Bandas horizontales de color, al menos 12 […]. Un círculo blanco con borde blanco, ubicado en la esquina superior derecha, que contiene 8 franjas horizontales multicolores […] y un pequeño círculo rojo en el centro. […] **Sí. En la zona superior derecha, justo en el borde del círculo, hay una pequeña mancha de color blanco irregular que parece un artefacto de pixelización o un error de renderizado.** También […] el círculo no está alineado perfectamente con las bandas horizontales.

**Contraste:** acierta el inventario (franjas + disco multicolor con centro rojo) y **señala una mancha blanca** en el borde del disco, que casa con el conteo objetivo (blanco 1088→2204 en frames 1 y 5). Queda como **sospecha bloqueante** hasta caracterizarla (DT-007).

### A′ · Misma escena como hoja de contacto etiquetada (3 paneles)

**Respuesta** (cruda, extracto literal):
> Se observan tres paneles rectangulares […] cada panel dividido en dos franjas horizontales de color sólido […]. Los colores cambian de panel a panel […]. No se observan cortes, zonas incoherentes o elementos de "basura".

**Lectura:** el montaje **pierde la resolución** necesaria para esta escena (no ve franjas ni disco: solo “dos bandas”). Confirma la regla de §6.4: montaje para inventario/presencia de elementos grandes, no para detalle; para detalle, **una imagen por llamada**.

## Conclusiones

1. La escena es **visualmente coherente** (franjas + disco con degradado), con una **sospecha de mancha blanca** en el borde del disco que coincide con el conteo objetivo.
2. **NO VERIFICADA por rendimiento**: 3,01 fields/frame (~3× presupuesto). Hipótesis de hotspot: `RasterGradientEffect::rebuild()`; medir por ablación en F1.
3. Captura determinista por paso de frame ya disponible (`--sequence-step-frames`).

## Deuda registrada

DT-007 (rendimiento 085 + mancha blanca del disco) · DT-003/§6.4 (límites del montaje vs imagen única).
