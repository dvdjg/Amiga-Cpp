# Auditoria inicial de tests del engine (2026-04-06)

Actualización de cierre del lote 1:

- `F01_font_digits`, `H01_custom_peek_poke` e `I01_input_edges` ya existen, compilan y quedan validados en vivo por ADF con `assert_failures=0`.
- La auditoría se conserva como foto del punto de partida, pero estos tres huecos ya no cuentan como pendientes operativos.

Auditoria operativa del estado real de cobertura del engine frente a:

- `doc/engine-test-battery-matrix.md`
- `doc/engine-unified-test-roadmap.md`
- `doc/engine-subsystems.md`
- `doc/amiga-implementation-roadmap.md`
- `tests/amiga-battery/`

Objetivo: separar lo que ya tiene cobertura usable, lo que existe pero sigue desalineado en docs, y lo que todavia no tiene un caso funcional del engine.

## 1. Resumen ejecutivo

El proyecto ya tiene una base fuerte de cobertura funcional en video y blitter, y una primera semilla real de micro-test del engine con `EU01`.

El principal bloqueo actual no es "falta total de tests", sino una mezcla de tres problemas:

1. **Cobertura desbalanceada**:
   video/blitter/sprites estan mucho mas avanzados que input, joy, DOS, audio, tilemap, custom, fixmath, rand y diagnostico.
2. **Deriva documental**:
   la matriz, el roadmap unificado y el roadmap de implementacion no siempre reflejan el mismo estado real.
3. **Promocion incompleta a tests engine-centric**:
   varios casos existen y pasan visualmente, pero no todos estan presentados como pruebas del engine con tier, contrato y trazas unificadas.

## 2. Cobertura actual por subsistema

### 2.1 Cobertura fuerte o razonablemente avanzada

| Subsistema | Estado real observado | Referencias |
|-----------|------------------------|-------------|
| Display base / bitplanes | Fuerte | `T00`, `T01`, `T02`, `T06`, `T07` |
| Blitter | Fuerte | `B01` a `B06` |
| Copper basico | Aceptable | `C01`, `V01`, `V02`, `V03` |
| Sprites engine | Aceptable | `S01`, `S04`, `S05`, y ademas existe `S02` |
| Clock | Primera base real de micro-test | `EU01` |

Observacion:

- `T07` ya usa `engine_view_*`.
- `EU01` ya usa `engine_clock_*` y `engine_font_draw_char_1bpl`.
- `S01`, `S04`, `S05` cubren partes importantes de `engine_sprite_*`.

### 2.2 Cobertura parcial o indirecta

| Subsistema | Situacion |
|-----------|-----------|
| `engine_font_*` | `EU01` + `F01` ya cubren dibujo mínimo 1bpl; queda ampliar variantes si hiciera falta |
| `engine_view_*` | Hay cobertura parcial en `T07`, pero no existe aun un caso engine-centric tipo `V04` |
| `engine_custom_*` | `H01` ya cubre escritura visual de paleta y lectura de registros custom legibles; sigue faltando expandir cobertura si se añaden wrappers nuevos |
| `engine_dos_*` | Existe implementación `dos_io.c` y hay casos DOS de infraestructura, pero no un test engine funcional claro `D01` |
| `ENGINE_MEM_TRACE` / `ENGINE_DIAG` / `engine_trace_*` | Hay implementación, pero solo cobertura de integración implícita; no hay casos `EU03` / `EU04` |

### 2.3 Cobertura ausente o muy débil

| Subsistema | Hueco |
|-----------|-------|
| `engine_joy_*` | No hay `J01` |
| `engine_input_edges_*` | `I01` ya cierra la base de ratón programático; queda teclado/joy si se expone automatización estable |
| `engine_tilemap_*` | No hay `T03` o `EI04` real |
| `engine_rand_*` | No hay `EU02` |
| `engine_fixmath_*` | No hay micro-test dedicado `M01` / equivalente `EU` |
| Audio `engine_audio_*` | No hay `A01` ni `A03` del engine cerrados |
| `engine_copper_double_*` | No hay `EF07` / `C02` |

## 3. Desalineaciones detectadas en documentación

### 3.1 `S02_attached_sprite_pairs` existe pero sigue fuera de la matriz y del estado oficial

El arbol ya contiene:

- `tests/amiga-battery/S02_attached_sprite_pairs/`
- `case.json`
- `src/main.c`
- `docs/technique.md`
- evidencia abundante en `evidence/`

Sin embargo:

- `doc/engine-test-battery-matrix.md` no lo menciona.
- `doc/amiga-implementation-roadmap.md` sigue reflejando `S02` como `PENDIENTE`.

Esto indica deriva documental real.

### 3.2 `EU01` existe, pero su estado documental sigue ambiguo

El roadmap unificado lo presenta como primer micro-test U y documenta pasada viva con `stage_id=0xE005`.
Sin embargo, la matriz sigue tratandolo como parcial y no existe aun una politica clara de cierre para convertirlo en referencia de plantilla.

### 3.3 Los tiers `U / F / T / I` están definidos pero no están normalizados en los casos

`doc/engine-unified-test-roadmap.md` pide que los nuevos casos declaren tier en el `README`, pero eso no aparece de forma sistematica en los `README.md` actuales.

Consecuencia:

- cuesta distinguir rapido entre micro-test del engine, test funcional de subsistema y tecnica compuesta.

## 4. Riesgos actuales para el sistema de tests del engine

1. **Prioridades falsas por documentación atrasada**  
   La IA puede elegir mal el siguiente trabajo si ve `S02` como pendiente cuando ya existe.

2. **Cobertura engañosa de APIs**  
   Hay APIs implementadas en `engine/src/` con cero prueba dedicada visible, aunque el repositorio parezca maduro por la cantidad de casos gráficos.

3. **Promoción incompleta al engine**  
   Algunos casos prueban técnicas reales, pero no todos prueban claramente la API reusable del engine como objetivo principal.

4. **Infra de evidencia más madura que algunos contratos de cierre**  
   El pipeline de evidencia existe, pero sigue faltando homogeneidad en criterios de cierre por subsistema.

## 5. Primer lote recomendado

Se recomienda no abrir muchos frentes. El primer lote debe cerrar huecos de alta rentabilidad y bajo acoplamiento.

### Lote 1 propuesto

| ID propuesto | Motivo | Complejidad esperada |
|-------------|--------|----------------------|
| `I01` input edges | API ya implementada, falta caso directo y aporta valor transversal | Baja |
| `J01` joystick | API pequeña y autocontenida; cubre un hueco total del engine | Baja |
| `H01` custom peek/poke | Caso muy pequeño y útil para validar wrappers `engine_custom_*` | Baja |
| `F01` fuente 8x8 | Ya hay uso en `EU01`; separar un caso propio consolidará un subsistema reusable | Baja |

### Por qué no empezar por audio o tilemap

- **Audio** tiene mayor fricción de validación porque exige huella visual y suele mezclar timing.
- **Tilemap** puede abrir demasiado diseño si no se fija primero una representación visual mínima.
- **Copper doble buffer** tiene más superficie temporal y más riesgo de entrar en debugging de sincronía demasiado pronto.

## 6. Orden sugerido tras el lote 1

1. `I01` - `engine_input_edges_*`
2. `J01` - `engine_joy_*`
3. `H01` - `engine_custom_*`
4. `F01` - `engine_font_*`
5. `D01` - `engine_dos_file_slurp`
6. `EU02` - `engine_rand_*`
7. `M01` o equivalente `EU` - `engine_fixmath_*`
8. `V04` - `engine_view_*` dedicado
9. `T03` / `EI04` - `engine_tilemap_*`
10. `EF07` / `C02` - `engine_copper_double_*`
11. `A01` - `engine_audio_*`

## 7. Acciones documentales recomendadas antes del siguiente lote

1. Actualizar `doc/engine-test-battery-matrix.md` para reflejar:
   - `S02` como caso existente
   - columna `Tier`
   - separación entre cobertura fuerte, parcial y plan
2. Actualizar `doc/amiga-implementation-roadmap.md` para alinear:
   - estado real de `S02`
   - estado real de `EU01`
3. Definir un criterio simple para declarar un caso engine-centric:
   - usa API `engine_*`
   - tiene `runtime-state` y `evidence-log`
   - deja huella visual
   - tiene tier explícito

## 8. Conclusión

La base del engine ya no está en fase cero: hay cobertura fuerte en vídeo y una infraestructura de evidencia seria.

El siguiente salto de calidad no es "hacer muchos tests nuevos", sino:

1. corregir la deriva entre docs y realidad;
2. cubrir los huecos pequeños pero importantes del engine reutilizable;
3. normalizar tiers y criterios de cierre;
4. dejar audio, tilemap y cobre doble para un segundo escalón.
