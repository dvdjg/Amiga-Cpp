# APIs parametricas, capa retained y evaluacion C vs C++

Documento de apoyo para el diseno del engine cuando una misma capacidad puede requerir implementaciones diferentes segun el contexto real de la maquina.

## Idea central

En Amiga no conviene modelar muchas capacidades del engine como una sola funcion "universal".

La implementacion correcta puede cambiar de forma material segun:

- numero de bitplanes
- formato del sprite o bitmap fuente
- si hay mascara o no
- stride o modulo del destino
- clipping
- ownership del buffer destino
- politica de persistencia o restauracion
- si el elemento vive en bitplanes, sprites hardware, blitter BOBs o una capa debug

Por eso, el engine deberia tender a dos niveles complementarios:

1. primitivas low-level, explicitas y parametricas;
2. capa retained o scene-level que simplifica la gestion habitual.

## Regla de arquitectura

### 1. Capa low-level

Debe exponer los parametros que realmente cambian el algoritmo o el coste.

Ejemplos:

- `engine_cpu_blit_1bpl_*`
- `engine_cpu_blit_2bpl_*`
- `engine_cpu_blit_4bpl_*`
- `engine_blit_bob_masked_*`
- `engine_blit_bob_unmasked_*`
- primitivas con clipping explicito
- primitivas con restore-under explicito

Esta capa no debe fingir que todos los casos cuestan lo mismo ni que todos los destinos se comportan igual.

### 2. Capa retained

Debe construir sobre las primitivas low-level para simplificar el uso normal:

- scene objects
- orden de dibujo
- dirty rects
- save-under / restore-under
- HUD y overlays
- politica por frame
- adaptacion a scroll o viewport

La capa retained decide cuando llamar a las primitivas, pero no debe borrar la distincion entre contextos con costes distintos.

## Ejemplo: sprites CPU

Una API demasiado plana seria:

```c
void engine_draw_cpu_sprite(...);
```

El problema es que no expresa:

- cuantos bitplanes toca
- si el sprite tiene mascara
- si el fondo debe restaurarse
- si se dibuja en world space o screen space
- si el destino es compartido con scroll

Una familia mas sana podria ser:

- primitivas especializadas:
  - `engine_cpu_sprite_draw_1bpl`
  - `engine_cpu_sprite_draw_2bpl`
  - `engine_cpu_sprite_draw_4bpl`
  - variantes masked/unmasked
- wrappers retained:
  - `engine_scene_sprite_submit`
  - `engine_scene_overlay_submit`
  - `engine_scene_present`

## Politica recomendada para nuevas APIs

Antes de fijar una API, responder:

1. Que parametros cambian de verdad la implementacion?
2. Cuales son solo datos y cuales son politicas?
3. Conviene una ruta generica y varias especializadas?
4. La llamada pertenece al nivel low-level o al retained?
5. Se necesita una ABI C simple o una especializacion estatica?

## C++ como herramienta de especializacion

### Cuanto sentido tiene?

Tiene sentido si se usa como herramienta de:

- especializacion estatica
- inlining agresivo
- organizacion de familias de variantes
- separacion entre primitivas y wrappers

No tanto si se usa para introducir:

- jerarquias pesadas
- excepciones
- RTTI
- STL grande en rutas criticas

### Donde ayudaria de verdad

Ejemplo conceptual:

```cpp
template<int Bitplanes, bool Masked>
void cpu_blit_sprite(...);
```

Si `Bitplanes` y `Masked` son constantes, el compilador puede:

- podar ramas
- simplificar bucles
- especializar accesos
- dejar una version muy cercana a codigo escrito a mano

Esto encaja bien con:

- CPU blits por numero de bitplanes
- variantes masked/unmasked
- hot paths clipped/unclipped
- helpers de scene que llaman a primitivas especializadas

## Alternativas viables en C con GCC

No hace falta migrar todo a C++ para obtener parte de este beneficio.

En C con GCC se puede acercar bastante usando:

- `static inline`
- variantes especializadas explicitas
- macros generadoras
- include parametrizado
- `__builtin_constant_p(...)`
- LTO

Ejemplo de enfoque:

```c
void engine_cpu_blit_generic(..., int bitplanes);
void engine_cpu_blit_1bpl(...);
void engine_cpu_blit_2bpl(...);
void engine_cpu_blit_4bpl(...);
```

y una macro o wrapper que despache a la version especializada cuando el parametro sea constante.

## Recomendacion practica para este repo

### Opcion A: seguir en C y especializar mejor

Recomendable si ahora mismo se prioriza:

- toolchain conservador
- ABI simple
- migracion de bajo riesgo

Pasos:

1. definir familias low-level especializadas para casos criticos;
2. mantener fallback generico;
3. construir encima una capa retained ligera;
4. usar tests de bateria para medir si la especializacion compensa.

### Opcion B: adoptar C++ de forma controlada

Recomendable si se quiere:

- expresar mejor familias de variantes
- usar `templates`
- organizar wrappers retained sobre primitivas mas especializadas

Pero con disciplina:

- mantener ABI C donde convenga
- evitar excepciones y RTTI
- no meter STL pesada en rutas criticas
- introducirlo primero en modulos concretos

## Estrategia sugerida

La estrategia mas prudente es esta:

1. mantener el runtime y la ABI general muy cerca de C;
2. definir una politica oficial de "low-level parametrico + retained";
3. hacer un experimento pequeno en un modulo critico:
   - por ejemplo CPU blit o sprite CPU por bitplanes;
4. comparar:
   - claridad del codigo
   - tamano de binario
   - coste de mantenimiento
   - assembly generado
5. decidir despues si merece expandir C++.

## Relacion con la madurez de APIs

Esta politica no implica que toda API deba nacer ya perfectamente abstraida.

En este repo la regla correcta es:

- primero promover una capacidad funcional y verificada;
- despues refactorizarla si aparecen mas consumidores o patrones mejores.

La politica de madurez y refactor queda documentada en:

- [engine-api-maturity-and-refactor-policy.md](engine-api-maturity-and-refactor-policy.md)

## Conclusiones operativas

- El engine debe diferenciar mejor entre primitivas low-level y capa retained.
- Muchas funciones deben ser parametricas o especializadas segun contexto real.
- C++ puede ayudar mucho para especializacion estatica, pero no es la unica via.
- Antes de migrar el proyecto entero, conviene hacer una prueba pequena y medible.

## Referencias relacionadas

- [engine-architecture.md](engine-architecture.md)
- [engine-roadmap.md](engine-roadmap.md)
- [engine-subsystems.md](engine-subsystems.md)
- [engine-test-battery-matrix.md](engine-test-battery-matrix.md)
- [amiga-lowlevel-agent-prompt.md](amiga-lowlevel-agent-prompt.md)
- [amiga-lowlevel-technique-contract-template.md](amiga-lowlevel-technique-contract-template.md)
