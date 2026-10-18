# VALIDATION - 086_bob_objects

## Contenido (regla 25)

La escena tiene **fondo estructurado pintado en el bitmap** (regla 25): suelo verde (índice 5), línea de horizonte, tres montañas (índice 6), sol (índice 7) y cinco árboles; los índices 5..7 no los toca el copper (solo reescribe COLOR00 y COLOR01..04), así que se ven estables. Los tres BOB (discos de 32×32 con sus tres políticas: cookie-cut con máscara, OR aditivo y opaco + save-under) recorren **lissajous con x e y** y **ritmo por objeto** (pares/impares a distinta velocidad).

## Medidas (A500_debug, ventana de 8 s)

| Métrica | Valor | Fuente |
|---|---|---|
| `fps10` | **415** (41,5 fps; el reloj del periférico puede exagerar la pérdida — familia DT-036) | contador 0 |
| `bob_ok` | **1005** (≈ 335 frames × 3 BOB) | contador 1 |
| `bob_bad` | **0** | contador 2 |

**Self-check de memoria (regla 25):** cada frame se lee el **píxel central** de cada BOB en los planos reales y se comprueba que lleva el índice de su variante (tolerante al OR aditivo sobre fondo claro). `bob_bad = 0` en toda la ventana: los objetos están **dibujados en memoria**, no solo "vistos" en vídeo (lección DT-035).

## Visión (prompt canónico; `qwen3-vl:8b-instruct-q8_0`)

Respuesta cruda en `src/VISION.md`. Observado: cielo con bandas (degradado por línea), suelo, tres montañas, postes verticales, y los círculos móviles con **contornos limpios, sin halos ni restos** de posiciones anteriores; los cambios entre frames (posición y tono de los círculos — el degradado de copper por objeto cambia con su Y) descritos como movimiento. Los "desaparecer/reaparecer" que menciona el modelo son la lectura de los **degradados de color** de cada objeto: el self-check de memoria (`bob_bad=0`) confirma que los tres BOB están presentes **todos** los frames.

## Notas

- La captura por paso de este entorno puede congelar el vídeo (DT-035): la secuencia de visión usa captura por intervalos.
- La demo no pinta texto en pantalla (anterior a la norma de versión en pantalla); su depuración va por la **ventana de debug** (run-status con `detail` + contadores `fps10`/`bob_ok`/`bob_bad`), alternativa admitida por AGENTS §0.
