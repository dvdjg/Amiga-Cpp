# Política de uso de Fast RAM

Fast RAM es memoria accesible por la CPU que evita competir con Agnus por el bus Chip. Cuando el
hardware la detecta, el engine debe facilitar que el trabajo de CPU se coloque allí por defecto,
reservando Chip RAM para recursos que leen el Copper, Blitter, bitplane DMA, Paula u otro DMA del
chipset. La detección habilita una política; no mueve por sí misma memoria ya cargada.

## Regla de asignación

```text
                          Fast disponible
                     ┌──────────┴──────────┐
                     sí                   no
                     │                    │
trabajo solo CPU  → Fast               Slow/Any según backend
trabajo de DMA    → Chip               Chip
scratch CPU       → Fast scratch       Slow scratch / política del backend
```

La API de dominio debe expresar si el recurso necesita DMA y qué alternativas permite. El allocator
resuelve `FastPreferred` en setup y deja constancia del banco efectivo. No se debe usar Slow como
sinónimo de Fast: Slow sigue compartiendo contención del bus o puede no estar disponible.

Si se detecta Fast RAM, las nuevas reservas de trabajo exclusivamente CPU deben preferir Fast
automáticamente mediante `MemoryManager`/`ResourceStore` (`FastPreferred`); el consumidor no debería
repetir la detección ni escoger `fast()` manualmente en cada subsistema. El handle debe registrar el
banco efectivo. `FastRequired` falla si el banco no existe o no cabe; `FastPreferred` puede caer a
Slow solo si su política lo permite. Chip/DMA nunca cae a Fast o Slow. En el engine esto es
`eng::MemoryPolicy` (`ChipRequired`/`FastRequired`/`FastPreferred`/`AnyBank`) con
`eng::reserve<Tag>(mm, policy, bytes, align)`, que resuelve el banco reutilizando
`fast_or_slow`/`any_bank` y deja el **banco efectivo** en `Block::kind`.

El arranque debe reservar Fast primero para el stack configurado y después calcular una cuota para
los pools persistentes y scratch CPU. `HwInfo::fast_ram_bytes` es capacidad detectada, no capacidad
libre para el juego: hay que dejar margen para Exec, otros procesos, startup y servicios residentes.

## Cuatro mecanismos distintos

### Pila principal

El soporte actual ofrece `FAST_STACK=1`: `_start` reserva una pila con `AllocMem(MEMF_FAST)` y
cambia `SP` antes de entrar en los constructores y `main()`. Si la reserva falla, conserva la pila
original como fallback. Es una selección de build por aplicación, con tamaño fijo actual, no una
selección runtime integrada en `App`. El stub conserva el `SP` anterior, pero no conserva base y
tamaño del bloque Fast para ejecutar `FreeMem` al retornar; hay que resolver esa vida útil antes de
tratarlo como mecanismo general para aplicaciones de larga vida.

El cambio afecta al modo de CPU:

- En takeover/supervisor, `SP` y `SSP` son el mismo stack activo; las interrupciones usarán Fast.
- En un proceso AmigaDOS en modo usuario, cambiar `SP` solo cambia la pila de usuario; el SSP sigue
  siendo propiedad de Exec.
- Un juego que convive con Exec no debe cambiar el SSP ni asumir ownership del stack de supervisor.
- Para que la reserva no se fugue en un entorno de vida larga, el startup debe conservar base y
  tamaño, restaurar el stack anterior y liberar el bloque Fast al salir cuando el contrato de salida
  lo permita.

Una mejora de API consiste en configurar tamaño y preferencia de pila en el perfil de arranque y
hacer que el startup reciba una política generada por el build o por un launcher. La detección ocurre
antes de `main()` mediante el resultado de `AllocMem(MEMF_FAST)`; el `MemoryManager` normal de C++ aún
no está construido en ese punto.

La política objetivo es `StackPolicy {bank = FastPreferred, bytes = ...}`. Debe devolver el banco
efectivo/fallback y conservar base+tamaño para liberar la reserva al salir. No debe intentar cambiar
el SSP de un proceso en modo usuario. En el engine existe ya `eng::StackPolicy` +
`eng::stack_from(mm, policy)` (`memory/stack.hpp`), con el banco efectivo en `Stack::block.kind`;
falta conectar `FAST_STACK=1` del build a esa política.

### Datos globales/estáticos

Los globals `.data`/`.bss` los ubica el cargador del ejecutable según los segmentos HUNK. Detectar
Fast desde C++ no los relocaliza. Copiar un objeto global ya construido a Fast no es correcto en
general: punteros internos, tablas de inicialización, referencias de otros globals y registros de
relocación conservarían la dirección original.

Opciones válidas:

- **Colocación por segmento al enlazar/cargar**: separar segmentos HUNK de datos con una política
  Fast y hacer que el cargador los reserve allí antes de aplicar relocaciones. Debe verificarse que
  `elf2hunk` conserva los flags y que el camino de ejecución usado por el juego los respeta.
- **Datos de trabajo dinámicos**: reservarlos durante setup con una política `FastPreferred` y pasar
  vistas a los subsistemas. La reserva explícita con `MemoryManager::fast()` existe hoy; la selección
  automática por dominio aún debe integrarse en la fachada de recursos.
- **Globales en secciones custom**: solo si el linker y el cargador documentan cómo traducen la
  sección ELF a un HUNK con flags de memoria. No debe introducirse `ENG_FAST_RAM` suponiendo que una
  sección arbitraria terminará en Fast.

Los globals que contienen recursos DMA deben continuar en Chip o ser reemplazados por bloques Chip
tipados. Nunca debe aplicarse una política global que traslade indiscriminadamente todos los
estáticos a Fast.

### Código principal

El código del ejecutable principal ya fue colocado por el loader antes de que el runtime pueda
sondear memoria. Ejecutarlo desde Fast requiere que el HUNK de código solicite Fast y que el cargador
de ejecutables lo respete, o que un loader propio relocalice el programa completo antes de iniciar
C/C++. No basta copiar `.text`: las relocaciones, tablas de init/fini, vectores de excepción,
referencias entre segmentos y entry point deben apuntar al nuevo layout.

El código en Fast no está visible para DMA. Toda tabla constante referenciada por Copper/Blitter
debe residir además en Chip, aunque el código que la produce se ejecute desde Fast.

### Librerías dinámicas

La carga de código modular es el lugar más natural para seleccionar banco por segmento. El loader
debe recibir una política y aplicarla antes de copiar y relocalizar:

```cpp
struct ModuleMemoryPolicy {
    MemoryPreference code = MemoryPreference::FastPreferred;
    MemoryPreference data = MemoryPreference::FastPreferred;
    MemoryPreference bss = MemoryPreference::FastPreferred;
    bool allow_fallback = true;
};
```

Reglas:

- Respetar `HUNKF_CHIP` como requisito DMA; no degradarlo a Fast.
- Tratar `HUNKF_FAST` como preferencia o requisito según la especificación confirmada del formato y
  la política declarada; si no se puede cumplir, devolver un error explícito cuando sea obligatorio.
- Para segmentos sin flag, usar `ModuleMemoryPolicy`; Fast es preferida para `.text`, `.data` y BSS
  solo si el módulo no declara dependencias Chip.
- Reservar un bloque por segmento y guardar el owner tipado, banco efectivo, tamaño y alineación.
- Aplicar relocaciones después de todas las reservas, porque se necesitan las bases finales de todos
  los hunks.
- Liberar cada segmento al descargar el módulo, tras retirar callbacks/tareas y asegurar que no se
  ejecuta código ni existe DMA pendiente.
- En máquinas sin Fast, aplicar fallback explícito a Slow/Chip según el segmento; no asumir que
  `MEMF_ANY` produce Fast.

El `DynLoader` actual recibe una `LinearArena` única para HUNK y hace `.englib` in situ; no cumple
todavía esta política. `HunkSegment::mem` registra un tipo de memoria, pero `HunkImage::load()` no
selecciona un banco por segmento: aunque el HUNK indique Chip/Fast, la reserva sigue viniendo de la
arena única. La carga propietaria debe reservar cada segmento por separado, aplicar fallback
declarado y registrar el banco efectivo antes de relocalizar.

## Preparación del `MemoryManager`

`HwInfo::fast_ram_bytes` describe memoria detectada en Exec; `MemoryConfig::fast_bytes` decide cuánto
intenta reservar el backend. El presupuesto reservado debe ser menor o igual a la disponibilidad,
con margen para Exec, stack, memoria de otras tasks y librerías. El orden recomendado de arranque es:

1. El startup de plataforma conserva el stack del sistema y solicita la pila Fast opcional.
2. El backend sondea las regiones de RAM y publica `HwInfo`.
3. El plan de memoria calcula cuánto Fast reservar para pools persistentes y scratch CPU, descontando
   las reservas realizadas durante el bootstrap.
4. `MemoryManager` configura los bancos persistentes y scratch.
5. La aplicación crea tareas y reserva sus stacks antes de activar sus context switches.
6. La carga de módulos recibe su `ModuleMemoryPolicy` y registra el banco realmente usado.

No hay que reservar de nuevo con `AllocMem` desde cada subsistema una vez configurado el manager.
Las reservas directas del startup y de stubs del sistema son fronteras de bootstrap; sus bloques
deben transferirse al gestor o liberarse explícitamente.

## Verificación necesaria

- Ejecutar la misma demo con cero Fast y con Fast y comprobar el fallback.
- Verificar el rango de dirección de cada bloque mediante `hw::classify_region`, no solo el flag
  solicitado.
- Medir un kernel CPU-bound en Chip, Slow y Fast; separar el efecto de CPU del tráfico DMA.
- Confirmar `SP` y modo supervisor en startup; probar interrupciones solo en takeover y confirmar
  que un proceso DOS no modifica SSP.
- Inspeccionar ELF/HUNK/mapa para confirmar flags y ubicación de secciones estáticas; comprobar que
  globals con punteros siguen siendo válidos.
- Cargar HUNK multi-segmento con code/data/BSS en bancos distintos y verificar relocaciones y
  exports byte/dirección a byte.
- Descargar el módulo, comprobar que todos los segmentos vuelven a sus pools y que no quedan
  tareas, callbacks, IRQ ni DMA apuntando al módulo.
- Auditar el binario para asegurar CPU 68000 y ABI correcto (`tools/analyze/asm-audit.mjs`).

## Referencias

- [`MEMORY_OWNERSHIP.md`](MEMORY_OWNERSHIP.md)
- [`INTERNAL_TYPE_SYSTEM.md`](INTERNAL_TYPE_SYSTEM.md) §3.8
- [`HARDWARE_INVENTORY.md`](HARDWARE_INVENTORY.md)
- [`RESOURCE_SYSTEM.md`](RESOURCE_SYSTEM.md) §2
- [`FILE_SYSTEM_AND_DYNAMIC_LIBRARIES.md`](FILE_SYSTEM_AND_DYNAMIC_LIBRARIES.md)
- [`ROADMAP_MEMORY_OWNERSHIP.md`](../../guides/roadmap/ROADMAP_MEMORY_OWNERSHIP.md)
- [`ROADMAP_RESOURCES.md`](../../guides/roadmap/ROADMAP_RESOURCES.md)
- AHRM 3.ª edición §1: selección `MEMF_CHIP` y memoria Fast; secciones de Chip DMA.
