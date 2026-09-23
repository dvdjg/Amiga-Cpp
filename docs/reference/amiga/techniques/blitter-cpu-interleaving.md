# Blitter-CPU interleaving en el A500

Referencia de la técnica demoscene de **solapar** el trabajo del Blitter con el de la CPU.
Sintetizada de `amiga-bootcamp/17_demoscene/timing_optimization.md` (§Technique 2) y
contrastada con el estado real del seam de Blitter del engine. El presupuesto y la
corrección de unidades están en
[copper-timing-and-budget.md](copper-timing-and-budget.md); este documento cubre solo el
solape.

## 1. Principio

Blitter y CPU **comparten el bus**. Mientras el Blitter trabaja, la CPU que hace accesos a
memoria se queda esperando ranuras; la CPU que trabaja **solo con registros** no necesita
el bus y avanza. Por eso el patrón es: **arrancar el blit → hacer cómputo sin memoria →
esperar → siguiente blit**.

```
   CPU    │■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■│
          │  arranca   │  cómputo solo-registros  │ espera │ …
   Blitter│            │■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■│
          │            │  relleno del polígono     │
          └────────────┴───────────────────────────┴─────────────► tiempo
```

En un A500 el Blitter y la CPU se reparten el bus; esperar sin hacer nada desperdicia la
mitad o más de las ranuras útiles. En un 68000 el cómputo solo-registros es real: las
instrucciones con ambos operandos en registro (`muls.w`, `add.l`, `swap`…) no tocan el bus.

## 2. Estado en el engine

El seam de Blitter (`MinimalBackend`, `engine/src/platform/amiga/`) ofrece **tres**
piezas relacionadas con el solape:

| Pieza | Qué hace | Dónde |
|---|---|---|
| `wait_blitter()` | espera a que baje `BBUSY` (`DMACONR`) | `amiga_minimal_internal.hpp` |
| **Servicio de fondo** | durante la espera, drena tareas de fondo registradas | `set_blitter_service`, `g_blitter_service` |
| **Espera diferida** (`wait = false`) | lanza la operación sin esperar; el llamador espera después | variantes `*_wait` de las ops |

- **Servicio de fondo**: `wait_blitter()` ejecuta `g_blitter_service(user, vpos)` en cada
  vuelta del bucle mientras `BBUSY` siga activo. `engine.hpp` lo conecta a la cola de
  tareas de fondo (`BackgroundBlitterService`), así que **el CPU ya hace trabajo útil
  mientras el Blitter corre** (demo `081_background_tasks`). Ese trabajo es de fondo
  (audio/cola), **no** transformación de geometría.
- **Espera diferida**: encadenar operaciones con `wait = false` y esperar una sola vez al
  final (patrón de la pipeline de 3 buffers; `flat_shade_xor(..., area_fill_wait = false)`).
- **Por defecto**: cada operación usa `wait = true`, es decir, **espera síncrona**. No hay
  solape automático de cómputo de geometría con el Blitter.

**Conclusión**: el engine tiene el *mecanismo* (servicio + espera diferida), pero el camino
por defecto es secuencial; el solape de CPU "solo-registros" con la geometría no está
automatizado.

## 3. Cómo aplicarlo

1. **Encadenar blits** con `wait = false` y colocar entre ellos el cómputo que no toque los
   destinos en curso.
2. **Registrar el servicio de fondo** para trabajo independiente del frame (audio, colas).
3. **Esperar antes de usar el resultado**: leer o reescribir el destino de un blit en curso
   desde la CPU lo corrompe. El solape es con trabajo que **no accede a ese destino**.
4. **Precargar en registros** lo que se vaya a necesitar (matriz, contadores) antes de
   arrancar el blit, para que el tramo de solape no tenga que leer memoria.

## 4. Antipatrón: el bucle de espera del Blitter

Sin servicio de fondo, `wait_blitter()` es un bucle apretado sobre `BBUSY` que no hace nada
más. Es correcto (y barato) si no hay trabajo pendiente, pero es un desperdicio si el CPU
podría adelantar cómputo.

```
   MAL:   arrancar blit → while (BBUSY) {}            → arrancar siguiente
   BIEN:  arrancar blit → cómputo solo-registros → while (BBUSY) {} → siguiente
```

## 5. Cuándo NO usarlo

- Si **no hay trabajo solo-registros** que hacer: el bucle de espera vacío es entonces lo
  más barato.
- Si el siguiente trabajo **lee o escribe el destino** del blit en curso: hay que esperar
  primero.
- Si el cuello es de **bus** (muchos accesos a Chip RAM) y no de espera del Blitter: el
  solape no ayuda; ver [copper-timing-and-budget.md](copper-timing-and-budget.md) §5.

## 6. Referencias

- `amiga-bootcamp/17_demoscene/timing_optimization.md` (§Technique 2, §Antipatterns)
- [copper-timing-and-budget.md](copper-timing-and-budget.md) — presupuesto, contención y lo medido
- `engine/src/platform/amiga/amiga_minimal_internal.hpp` (`wait_blitter`, servicio)
- `demos/amiga/081_background_tasks/` — servicio de fondo durante la espera
- AHRM 3.ª, capítulo del Blitter (`BLTPRI`, `DMACONR`)
