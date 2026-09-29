# Prompts de visión y flujo híbrido

Prompts canónicos para las consultas al modelo de visión local (Ollama) y descripción del flujo
**híbrido** (determinista → visión) que usan `flicker-check.mjs` y `essential-frames.mjs`. El código
es la fuente ejecutable; este documento es el contrato de los prompts para poder revisarlos y
versionarlos sin leer el JS.

## Principios

1. **Referencia + comparación.** Se pasa siempre un **frame de referencia** (comportamiento esperado)
   y el/los frames a evaluar, y se pide **comparar contra la referencia**, no describir cada frame
   aislado. Los VLM fallan más en glitches temporales cuando se les pide "describir".
2. **Respuesta estructurada y restringida.** Formato fijo: `sí/no` + tipo + zona + confianza +
   explicación breve. Evita respuestas vagas.
3. **Regiones relativas, nunca píxeles.** El modelo no entiende la imagen en coordenadas; pedirle
   píxeles produce alucinaciones (`y=715` en una imagen de 576 px). Se prohíbe explícitamente.
4. **Sospecha ya localizada.** La capa determinista decide *dónde* mirar; el modelo solo
   **confirma o descarta**. No busca a ciegas.
5. **Contexto del dominio.** Se indica que es una demo de Amiga 500 (lowres, paleta limitada) para
   que no interprete el banding/paleta como anomalía.
6. **Modelo adecuado.** **Qwen3-VL** (8B/32B) como principal; Qwen2.5-VL y Gemma 3 como
   alternativas. Evitar **LLaVA clásico** (el que más alucina).

## Prompt de parpadeo/glitch (estructurado)

Usado por `flicker-check.mjs` cuando hay candidatos deterministas (se envía la región candidata con
frames de referencia+contexto):

```text
Estos son frames CONSECUTIVOS de la demo "<demo>" (Amiga 500, lowres, paleta limitada).
Frame <X> es la referencia (comportamiento esperado).
El detector determinista (diferencia + optical flow) señala una posible anomalía tipo
"<flicker|tearing|corruption>" en la zona relativa "<zona>"; el movimiento coherente NO está señalado.
Confirma o descarta ESA sospecha a partir de los frames.

¿Hay parpadeo, flickering, tearing, corrupción de tiles, objetos que aparecen/desaparecen
de forma no justificada por el movimiento legítimo, o cualquier artefacto no explicable?

Responde SOLO en este formato (sin píxeles, sin coordenadas numéricas):
- Anomalía: sí / no
- Tipo: parpadeo / tearing / corrupción / desaparición / otro / ninguno
- Zona relativa: (arriba-izquierda / centro / abajo-derecha / pantalla completa / …)
- Confianza: alta / media / baja
- Explicación (máximo 2 frases)
```

Sin candidatos deterministas (fallback por rejilla de luminancia), se sustituye la segunda línea por
la zona de mayor oscilación expresada **en tercios** (p. ej. «1/3 izquierda, 2/3 superior»).

## Prompt de frames esenciales (`vision-points.json`)

`essential-frames.mjs` declara qué debe verse en frames concretos; el modelo **confirma o niega** esa
expectativa (no la describe libremente). El `expect` de cada punto es la afirmación a validar.

## Enfoque híbrido (pipeline)

```text
secuencia PNG
   │
   ├─ frame-diff.mjs        → px cambiados + bbox por par (referencia determinista)
   │
   ├─ temporal-detect.py    → candidatos flicker/tearing/corruption (OpenCV:
   │                          oscilación A-B-A + optical flow + bloques; descarta movimiento)
   │
   └─ flicker-check.mjs
        ├─ si hay candidatos → VLM SOLO sobre la región candidata (+ contexto) [prompt estructurado]
        └─ si no           → VLM sobre la rejilla de luminancia (fallback)
   │
   └─ informe: frame-diff + candidatos + respuesta del modelo
```

Regla de decisión: la capa **determinista** es la referencia; la respuesta del modelo es **apoyo**.
Ante discrepancia, prevalece el frame-diff. Ningún VLM open-weight es fiable al 100 % en glitches
temporales finos (parpadeo de 1–2 frames, tearing sutil, corrupción de copperlist).

## Few-shot (opcional, para parpadeos conocidos)

Si un parpadeo concreto es difícil (p. ej. un plano de bits que parpadea o un sprite que se
corrompe), incluir en el prompt **2–3 ejemplos** de frame bueno vs. malo de esa misma demo, con la
etiqueta esperada. Estabiliza bastante la respuesta del modelo. Formato sugerido:

```text
Ejemplos de referencia de esta demo (ya etiquetados):
- Ejemplo OK:  <frame sano> — el panel se ve completo, sin bandas.
- Ejemplo MAL: <frame defectuoso> — banda negra vertical en el borde derecho.
Ahora evalúa los frames siguientes con el mismo criterio.
```

## Relación

- Tooling: `tools/vision-review/{frame-diff.mjs,temporal-detect.py,flicker-check.mjs,essential-frames.mjs}`.
- Metodología: `docs/guides/methodology/PROTOCOLO_ETAPAS_GRAFICOS.md`, `DEMO_VISUAL_DEBUG.md` §2.1.
- Integración: `tools/test-regression.sh` (`--flicker`, `--require-flicker-ok`).
