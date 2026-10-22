# VALIDATION — 224_sprite_reuse_carousel

## Estado

**Parcialmente verificada.** A y C medidas a 49,92 fps con el camino de emisión estático; tras ampliar la X a todo el ancho, el arnés de `measure-fps` dejó de observar avance de `run-status` en esta sesión, así que la re-medida queda pendiente. B y D SIN VERIFICAR.

## Matriz de ejecución

| Perfil | Build/run | fps / fieldsPerFrame | Ciclos/frame | Flicker | Placements | Canales | Reuso | Visión |
|---|---|---:|---:|---|---:|---|---:|---|
| A — 3 planos | OK (`A500_k224_planes3_release`) | **49,92 / 1,002** (build previo al ajuste X) | **142.102** | pendiente de gate | 40 | 0xff | 5 | canónica generada |
| B — 4 planos | OK + secuencia animada | SIN VERIFICAR (DT-045) | — | pendiente | 40 | 0xff | 5 | — |
| C — 5 planos | OK (`A500_k224_planes5_release`) | **49,92 / 1,002** | **142.102** | pendiente de gate | 40 | 0xff | 5 | canónica generada |
| D — 6 planos EHB | OK + secuencia animada | SIN VERIFICAR (DT-045) | — | pendiente | 40 | 0xff | 5 | EHB revisado a ojo: fondo y sprites correctos |

`detail = 0xff0528`: 40 placements (`0x28`), máxima reutilización 5 (`0x05`) y máscara de canales `0xff`.

## Medidas

- `tools/debug/measure-fps.mjs 224_sprite_reuse_carousel A500_k224_planes3_release --json` → 49,92 fps emulados / 49,88 host, 142.102 ciclos/frame, 1,00159 campos/frame, 1011 frames, `stable: true`.
- Idéntico resultado en `A500_k224_planes5_release` (49,92 fps, 142.102 ciclos): el camino estático no escala con los planos porque no reemite la lista.
- Telemetría publicada por la demo: `placements=40`, `channels=0xff`, `reuse=5`, `patched=160` words por frame (más 80 words de PT una vez por segundo).
- `HOST-391` ampliado: una escena de 8 canales × 5 franjas compone 40 placements y devuelve `channels == 0xff` y `max_reuse == 5`, que es la misma telemetría que valida la batería.

## Hallazgos de API (evidencia)

1. **Recomposición por frame**: `SpriteScene::emit` con 30 actores attached/libres midió ~670k ciclos/frame (≈5,0 campos) y ~580k de ellos dentro de la composición. El camino estático parcheado baja a 142.102 ciclos (1 campo) con más objetos visibles. La fachada necesita un camino incremental/estable; hoy no lo expone.
2. **Admisión vs capacidad por línea**: `RepresentationBudget::sprite_channels` se consume al dar de alta cada actor, pero la multiplexación vertical decide después. Una escena con más actores que canales debe dimensionar la admisión al número de actores o degrada antes de tiempo. Documentado en `SPRITE_MATERIALIZATION_CONTRACT.md` §4.
3. **Telemetría ausente**: no había forma de verificar "8 canales, 5 reusos" sin inspeccionar el reparto. Añadidos `channels` y `max_reuse` a `SpriteComposeResult` y cubiertos por HOST-391.
4. **Optimizaciones aplicadas**: máscara de rangos en `SpriteAllocator` (evita tres barridos vacíos por frame), counting-sort estable por línea con scratch del llamador, y caché de cocinado de pares *attached* por actor (reusa DATA mientras no cambie la fuente/altura).

## Visión

`src/VISION.md` (prompt canónico, 5 frames de `A500_k224_planes3_release`). Respuesta: fondo de rejilla azul/verde estable, sprites geométricos multicolor repartidos, contornos limpios, sin halos, sin restos del objeto anterior y sin rectángulo opaco tapando el fondo. El modelo describe apariciones/desapariciones entre frames, coherentes con la rotación de arte cada segundo.

## Artefacto del arnés (DT-045)

Con la build actual, `measure-fps` da **49,5–50,3 fps en las cuatro primeras sub-ventanas** y después todas las ventanas leen 0 frames (`detail=0xff0528`, READY). La misma build medida con `run-demo.sh --sequence-frames 12 --sequence-interval-ms 1500` produce **12 frames distintos a lo largo de 18 s**, así que la demo sigue animando: el parón es de la herramienta con la sesión GDB adjunta, no de la demo. `216_attached_actors` mide estable con el mismo arnés, lo que descarta un fallo general del harness. Registrado como DT-045.

## Sin verificar

- Re-medida de fps del build actual (X a todo el ancho) y de los perfiles B y D.
- Gate de flicker (`temporal-detect` + `flicker-check`) sobre la secuencia actual.
- Contraste dirigido C contra `INTENT.md` (presencia por banda y por lane) en las cuatro profundidades.
- Estabilidad del swap de COP1LC bajo carga sostenida (>10 s) y con las cuatro profundidades (DT-045).
