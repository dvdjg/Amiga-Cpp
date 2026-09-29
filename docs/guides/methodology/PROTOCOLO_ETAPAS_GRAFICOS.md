# Protocolo de construcción por etapas (gráficos y hardware)

Protocolo obligatorio para construir cualquier funcionalidad gráfica o de hardware (blitter, copper, 3D, efectos) **por capas verificables**, de modo que nunca se llegue a una implementación compleja que no se puede depurar. Complementa la metodología incremental general ([development-methodology.md](development-methodology.md)) con el **orden concreto de etapas** y las reglas de actuación ante fallos. Se apoya en las herramientas que ya existen en el repo: tests host (`tests/host/`), demos (`demos/`), análisis visual con Ollama (`tools/vision-review/`) y el contrato técnico por fases ([amiga-lowlevel-technique-contract-template.md](amiga-lowlevel-technique-contract-template.md)).

## 1. Principios rectores

1. **No empezar por el final.** El resultado pedido (p. ej. un dodecaedro rotando con caras sólidas por Blitter) es la meta, no el punto de partida. Primero las capas que lo componen, cada una verificada.
2. **Cada paso debe ser verificable de forma independiente.** Antes de pasar a la siguiente capa, la actual produce un resultado observable y correcto. Si no se puede verificar, no se avanza.
3. **Primero CPU, después hardware especializado.** La lógica se implementa en CPU (software) **antes** de trasladarla al Blitter, al Copper o a cualquier recurso de hardware. Así se separan los errores de **algoritmo** de los errores de **configuración de registros**. La versión CPU queda como **referencia canónica** de equivalencia (y sirve a un futuro backend de otra plataforma, p. ej. Atari ST); se conserva incluso cuando ya existe la versión acelerada.
4. **Verificación visual obligatoria.** Cuando el resultado es gráfico o animado, se generan capturas o secuencias cortas y se pasan por el modelo de visión local (Ollama) con una **pregunta crítica y específica**. No basta con "parece que funciona": ver [DEMO_VISUAL_DEBUG.md](DEMO_VISUAL_DEBUG.md).
5. **Estudiar código funcional equivalente antes de escribir.** Antes de una rutina no trivial (Blitter, copperlist, transformaciones 3D), buscar y analizar ejemplos que ya funcionen (en el repo o en fuentes autoritativas) y detectar las diferencias entre el enfoque propio y el que funciona. Regla de contexto de la fuente: [LECCION-CONTEXTO-DE-LA-FUENTE.md](LECCION-CONTEXTO-DE-LA-FUENTE.md).

## 2. Protocolo de etapas (obligatorio)

Seguir siempre este orden. Cada etapa se cierra solo cuando ha pasado su verificación. **No se avanza con una etapa anterior a medias.**

**Etapa 0 — Contexto y alcance.** Antes de escribir código: resumir en pocas frases qué se construye y qué partes del hardware intervienen; identificar las dependencias (modo gráfico, planos, Blitter, Copper…); definir el **resultado mínimo verificable** de la primera etapa. En trabajo Amiga, esto se concreta rellenando §1–§2 del [contrato técnico](amiga-lowlevel-technique-contract-template.md).

**Etapa 1 — Configuración gráfica básica.** El modo de pantalla más simple posible (p. ej. lowres 320×200, 1–2 planos), limpiar la pantalla con un color sólido. Verificación: la pantalla muestra el color esperado y el sistema no se cuelga.

**Etapa 2 — Primitivas de dibujo en CPU.** Líneas y polígonos simples **solo con CPU** (sin Blitter). Empezar por un triángulo estático relleno, luego un cuadrado o polígono de más lados. Verificación: los polígonos se ven correctos, **sin huecos ni solapamientos erróneos**. En el repo, esta capa corresponde a `field::CpuRaster` sobre `Surface`; el test host de equivalencia es el patrón (ver [RASTER.md](../../engine/architecture/RASTER.md)).

**Etapa 3 — Transformaciones y proyección 3D en CPU.** Matrices de rotación/traslación/proyección en software sobre un objeto mínimo (cubo o tetraedro), dibujado con las rutinas de la etapa 2. Verificación: el objeto rota de forma coherente, sin deformaciones ni saltos; generar una secuencia corta y analizarla con el modelo de visión.

**Etapa 4 — Sustitución progresiva por hardware.** Solo con las etapas anteriores funcionando: sustituir el relleno de polígonos por el Blitter **manteniendo el resto en CPU** y verificar el **mismo objeto simple** (equivalencia con la versión CPU, byte a byte o píxel a píxel). Después incorporar el Copper si hace falta (paleta, waits…). **Cada sustitución se verifica de forma aislada.**

**Etapa 5 — Composición y optimización.** Unir las piezas ya verificadas, y **solo entonces** introducir el objeto complejo (dodecaedro u otro). Optimizar o reducir ciclos únicamente después de que el comportamiento sea correcto.

## 3. Cómo encaja con las herramientas del repo

Cada etapa se materializa con la herramienta canónica correspondiente, para que el cierre sea evidencia, no opinión.

| Etapa | Herramienta de verificación |
|---|---|
| 0 | §1–§2 del contrato técnico; §3 del DOC-MAP (buscar técnica/demo existente) |
| 1 | demo en hardware + captura; `tools/test-regression.sh --demo …` |
| 2 | **test host** de la primitiva CPU (p. ej. HOST-212) + análisis propio de la demo |
| 3 | test host de las transformaciones + secuencia de frames con Ollama |
| 4 | **test host de equivalencia** CPU vs Blitter (p. ej. HOST-268); `asm-audit` sin 68020+/FPU |
| 5 | demo completa en hardware + gate visual (`tools/vision-review/`) + profiler |

Reglas de uso:

- **La versión CPU es la referencia de equivalencia.** El test de la etapa 4 compara el resultado acelerado contra ella (mismo rectángulo/salida). Sin ese test, la optimización puede ocultar un defecto (ver [m68k-gcc.md](../../reference/toolchain/m68k-gcc.md) §3).
- **En el camino CPU, cada primitiva reutiliza lo que ya existe.** Antes de escribir una rutina, comprobar si `Surface`/`Rasterizer` ya la cubren (regla «buscar antes de crear» de `AGENTS.md` §1.6).
- **La decisión CPU→Blitter es por viabilidad.** Se implementa la variante acelerada cuando aporta rendimiento o fidelidad; si no se consigue, la versión CPU **se mantiene como procedimiento canónico de referencia** (y como base de un backend portable, p. ej. Atari ST), nunca se descarta.

## 4. Reglas de actuación ante errores

- Si algo falla, **no añadir más complejidad**: retroceder a la última etapa que funcionaba y aislar el cambio que introdujo el problema.
- Cuando el error sea de hardware (registros, timing, Blitter), generar primero la **versión CPU equivalente** y comparar resultados píxel a píxel o frame a frame.
- **Limitar los intentos.** Si tras **tres o cuatro** iteraciones un mismo error no se resuelve, detenerse, resumir el estado actual y **pedir orientación explícita** antes de continuar.
- **No usar prueba y error masivo.** Cada cambio debe tener una **hipótesis clara** de por qué debería funcionar.
- Aplicar la §8 (autopsia) del contrato técnico antes de rediseñar.

## 5. Uso del modelo de visión local (Ollama)

Para cualquier resultado gráfico o animado:

1. Generar uno o varios frames (o una secuencia corta) con `tools/run/run-demo.sh --sequence-frames`.
2. Pasar las imágenes al modelo de visión con una **pregunta concreta** («¿el polígono está completamente relleno sin huecos?», «¿la rotación es suave y coherente?», «¿hay artefactos en los bordes?»), usando `tools/vision-review/` o `tools/analyze/ollama-desc.mjs`.
3. Considerar la etapa superada **solo** cuando la respuesta confirma el comportamiento esperado. La visión artificial **alucina y es amable**: preguntar por defectos concretos y su zona, y mirar los frames personalmente (ver [DEMO_VISUAL_DEBUG.md](DEMO_VISUAL_DEBUG.md)).

## 6. Ejemplo: dodecaedro rotando con caras sólidas por Blitter

Orden correcto de trabajo, ilustrativo del protocolo:

1. Configurar pantalla lowres y limpiar con color sólido.
2. Dibujar un triángulo relleno en CPU.
3. Dibujar un polígono de más lados en CPU.
4. Implementar rotación y proyección de un tetraedro o cubo en CPU y dibujarlo.
5. Sustituir el relleno de caras por el Blitter, manteniendo las transformaciones en CPU.
6. Verificar la rotación del objeto simple con el Blitter (equivalencia con CPU).
7. Solo entonces introducir el dodecaedro y sus caras.
8. Añadir optimizaciones o efectos adicionales.

Si en cualquier momento el código se vuelve imposible de depurar, se vuelve a la última etapa estable y se reconstruye desde ahí.

## 7. Relación con otros documentos

- Metodología general (no big-bang, fases, tests): [development-methodology.md](development-methodology.md).
- Contrato técnico por fases (estado, superficies, paralelismo de chips, evidencias): [amiga-lowlevel-technique-contract-template.md](amiga-lowlevel-technique-contract-template.md).
- Diseño y depuración visual de demos (secuencias, Ollama, checklist de fallos): [DEMO_VISUAL_DEBUG.md](DEMO_VISUAL_DEBUG.md).
- Contexto de la fuente al portar código de terceros: [LECCION-CONTEXTO-DE-LA-FUENTE.md](LECCION-CONTEXTO-DE-LA-FUENTE.md).
- Dibujo CPU/Blitter tras `Surface` y equivalencia: [RASTER.md](../../engine/architecture/RASTER.md).
- Riesgo de optimizaciones que ocultan defectos y auditoría de codegen: [m68k-gcc.md](../../reference/toolchain/m68k-gcc.md) §3.
