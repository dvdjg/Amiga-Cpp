# Sistema de archivos y librerías dinámicas

Este documento define la arquitectura necesaria para acceder al sistema de archivos de forma normalizada y para cargar, descargar y ejecutar librerías dinámicas de Amiga, incluidas imágenes comprimidas con ZX0. La API pública debe ocultar `dos.library`, `trackdisk.device`, handles de Exec, punteros de memoria y el formato físico del recurso.

## Estado actual

El engine ya tiene piezas útiles, pero no una solución completa:

- `eng::os::file_*` define abrir, cerrar, leer, escribir, crear directorios, borrar, renombrar y E/S asíncrona mediante `FileDone`/`FileError`.
- La implementación Amiga sobre `dos.library` permite rutas normales, lectura/escritura y operaciones básicas; `trackdisk` y MFM son rutas separadas.
- `AssetRuntime` y `AssetCache` cargan bytes con presupuesto y mensajes, pero su contrato todavía no es un VFS.
- `DynLoader` parsea `.englib` y HUNK, aplica relocaciones y busca símbolos, pero recibe una imagen ya cargada y no posee la memoria.
- El depacker ZX0 existe en `eng/audio/zx0.hpp` y `support/dzx0_68000.s`, pero no está integrado como etapa del loader de librerías.
- `HunkImage::load()` recibe una sola `LinearArena`; los flags Chip/Fast del HUNK se registran, pero no seleccionan bancos independientes durante la reserva.

## Capas objetivo

```text
App / Resources / DynLibHandle
          │
          ▼
ResourceManager
  - VFS normalizado
  - handles y ownership
  - estados y errores
  - presupuesto por banco
          │
          ├── FileService: dos.library / trackdisk / paquete propio
          ├── DecodeService: raw / ZX0 / otros codecs
          └── ModuleLoader: HUNK / ENGL / relocaciones / símbolos
          │
          ▼
MemoryManager
  Chip | Fast | Slow | scratch
```

La E/S entrega bytes. El decoder transforma bytes comprimidos en una imagen. El loader transforma
la imagen en segmentos ejecutables y datos relocados. El gestor de recursos conserva el ownership
de todos los bloques hasta `unload` y coordina la vida útil con E/S, tareas e instrucciones en curso.

## VFS normalizado

`file_*` no debe ser la API final de la aplicación. Hace falta una fachada `Vfs` o `FileSystem` con:

- rutas normalizadas con separadores, `.` y `..` controlados;
- dispositivos y mounts (`PROG:`, `DATA:`, `DF0:`, paquete embebido, backend host);
- resolución de rutas relativa a una raíz de recurso;
- consulta de existencia, tipo, tamaño y fecha si el backend lo permite;
- enumeración de directorios con entradas de capacidad fija o iterador de mensajes;
- apertura como handle propietario, con cierre automático o `FileHandle` movible;
- lectura/escritura por offset y operaciones asíncronas identificadas por request, no solo por archivo;
- cancelación y cierre seguro de requests en vuelo;
- errores normalizados (`NotFound`, `Permission`, `Busy`, `Io`, `InvalidPath`, `Unsupported`,
  `Cancelled`, `OutOfMemory`, `Corrupt`);
- lectura completa opcional para load screens, sin obligar a bloquear el frame principal;
- backend de paquete para concatenar muchos ficheros pequeños y reducir seeks en disquete.

El path no debe viajar como `const char*` sin ownership definido. Para cargas asíncronas, el VFS debe
copiar o internar el path hasta recibir la terminación. El request debe incluir generación para que
un `FileDone` tardío no complete una operación reutilizada.

```text
VfsPath -> FileRequest{id, generation, handle, offset, bytes, buffer, cookie}
       -> FileService
       -> FileResult{id, generation, status, bytes}
```

`IoUser {tag,id}` es suficiente para el primer prototipo, pero para librerías y cancelación hace
falta un `RequestId`/generación separado del id del asset. El mensaje no debe permitir que una
respuesta de una carga antigua escriba el slot de una carga nueva.

## Memoria solicitada por el consumidor

La API debe aceptar una política explícita, no un puntero de arena:

```cpp
enum class MemoryPreference : u8 {
    AnyCpu,
    FastPreferred,
    SlowRequired,
    ChipRequired,
    FastRequired,
};

struct LoadMemoryPolicy {
    MemoryPreference code = MemoryPreference::FastPreferred;
    MemoryPreference data = MemoryPreference::AnyCpu;
    MemoryPreference bss = MemoryPreference::AnyCpu;
    bool allow_fallback = true;
};
```

Para HUNK debe prevalecer el flag del fichero cuando sea obligatorio (`HUNKF_CHIP`); la preferencia
del consumidor puede restringir más, pero no relajar un requisito del módulo. Si se solicita Fast y
no existe, `allow_fallback` decide si se usa Slow o se devuelve `MemoryUnavailable`. La memoria Chip
debe ser obligatoria para código/datos que Paula, Copper o Blitter vayan a leer.

La reserva debe devolver un módulo propietario con segmentos individuales:

```text
DynamicModule
  ├── code: Block<CodeTag, Fast|Chip>
  ├── data: Block<DataTag, Fast|Slow|Chip>
  ├── bss:  Block<BssTag, Fast|Slow|Chip>
  ├── symbols
  ├── init/fini
  ├── refcount/pin
  └── state
```

No se debe pasar una `LinearArena` al loader como sustituto de esta política. `LinearArena` sirve
para scratch o una fase completa; las librerías descargables necesitan bloques persistentes
liberables y ownership automático.

## Pipeline de librería comprimida

```text
open(path)
  → read header / stat
  → detectar contenedor y codec
  → reservar input en CPU RAM o Chip si el backend lo exige
  → leer imagen comprimida
  → reservar output de imagen según tamaño declarado
  → depack ZX0 por CPU
  → validar magic y límites HUNK/ENGL
  → reservar segmentos finales por política de memoria
  → copiar BSS/DATA/CODE
  → aplicar relocaciones
  → resolver imports y construir exports
  → ejecutar init bajo contrato
  → publicar `LibReady`
```

La imagen comprimida es staging y puede liberarse después de relocalizar. Los segmentos finales no
pueden liberarse mientras existan referencias a símbolos, tareas que ejecuten el módulo, callbacks,
IRQ instaladas o datos que el módulo haya registrado.

### Contenedor recomendado

ZX0 no contiene por sí mismo una política de recurso ni un tamaño de salida fácilmente expresado por
la API del engine. Para librerías comprimidas conviene un contenedor pequeño, por ejemplo `.engz`,
con:

```text
magic, version, codec, flags,
compressed_size, uncompressed_size,
alignment, memory_policy, payload_crc, payload
```

El `payload` puede ser `.englib` o HUNK. El contenedor debe declarar el tamaño descomprimido antes de
reservar output, detectar truncados y comprobar CRC antes de publicar el módulo. El host debe
generarlo y el loader debe rechazar versiones o codecs desconocidos sin ejecutar datos parciales.

## DynLoader completo

El loader actual debe evolucionar desde un parser que recibe una imagen hacia un servicio que posea
el ciclo de vida:

```cpp
LibRequest request(const char* path, const LoadMemoryPolicy& policy);
LibState state(LibHandle h) const;
bool retain(LibHandle h);
bool release(LibHandle h);
void* symbol(LibHandle h, u32 hash) const;
bool unload(LibHandle h);
```

Debe añadir:

- `load_async` y `load_sync` de load screen;
- estados `Declared`, `Reading`, `Decompressing`, `Relocating`, `Initializing`, `Ready`, `Failed`,
  `Unloading`;
- mensajes `LibReady`, `LibError`, `LibUnloaded` con request/generación;
- soporte de `init`/`fini` y convención de entrada documentada;
- tabla de imports y resolución contra una ABI estable del engine;
- comprobación de versión ABI, CPU objetivo y flags de reubicación;
- límites de segmentos, relocaciones, exports y símbolos para evitar corrupción;
- refcount/pin para impedir descargas mientras hay consumidores;
- espera o rechazo de descarga si una tarea o IRQ todavía ejecuta código del módulo;
- rollback completo si falla una relocación, símbolo, CRC o inicializador;
- caché de módulos por path/hash con invalidación explícita;
- información de diagnóstico sin exponer punteros a la aplicación.

## Relocaciones y ABI

HUNK no equivale a una librería segura por sí mismo. El loader debe fijar:

- tipos de relocación soportados y su endianness;
- hunks de código, datos y BSS aceptados;
- flags Chip/Fast y fallback permitido;
- formato de símbolos exportados e imports;
- versión de ABI del engine;
- registros preservados, formato de funciones y ownership de callbacks;
- prohibición de guardar punteros a un módulo que pueda descargarse.

El código cargado vive en RAM ejecutable. En 68000 no hay NX: una política `CodeMemory` debe validar
que el banco elegido es ejecutable y que el consumidor entiende que un módulo puede modificar estado
global. Si se permite código en Chip, debe presupuestarse frente a bitplanes y DMA.

## Carga y descarga segura

```text
request
  → load refcount = 1
  → módulo Ready
  → retain por cada consumidor
  → release
  → si refcount = 0 y no está pinned:
       detener tareas/callbacks del módulo
       desinstalar IRQ/hooks
       ejecutar fini
       esperar DMA/E/S que use sus buffers
       liberar segmentos e imagen staging
       publicar LibUnloaded
```

`unload` no debe ser una simple puesta a cero de la estructura mientras el módulo pueda estar en
ejecución. El loader actual hace eso y por tanto necesita un servicio de ownership y sincronización
antes de usarse como sistema general.

## Verificación necesaria

- HOST: normalización de paths, mounts, errores, cancelación y generaciones de requests.
- HOST: ZX0 dentro del contenedor `.engz`, CRC, tamaño, truncado y payload HUNK/ENGL.
- HOST: HUNK con segmentos Chip/Fast/Slow, fallback, BSS, relocaciones y rollback.
- HOST: symbol lookup, ABI mismatch, refcount, pin y descarga rechazada durante uso.
- Hardware: carga asíncrona desde `dos.library`, descompresión en tarea de fondo, módulo ejecutado y
  descargado al cambiar de zona.
- Hardware: memoria Fast preferida para código y comportamiento explícito en A500 sin Fast.
- Auditoría 68000: depacker, relocador y stubs sin instrucciones 68020+/FPU ni libcalls prohibidas.

## Orden recomendado

1. Completar VFS host-testable y normalización de paths.
2. Introducir `RequestId`/generación, cancelación y errores normalizados.
3. Separar pools persistentes de scratch y añadir `LoadMemoryPolicy` efectiva.
4. Crear contenedor `.engz` con tamaño, codec, CRC y política.
5. Integrar ZX0 como etapa genérica de decode, no como API exclusiva de audio.
6. Migrar `HunkImage` a reservas por segmento y `DynLoader` a ownership real.
7. Añadir imports/ABI/init/fini y descarga segura.
8. Integrar E/S asíncrona, mensajes y demo de transición de zona.

## Referencias

- [`RESOURCE_SYSTEM.md`](RESOURCE_SYSTEM.md)
- [`MINI_OS_IO.md`](MINI_OS_IO.md)
- [`MEMORY_OWNERSHIP.md`](MEMORY_OWNERSHIP.md)
- [`INTERNAL_TYPE_SYSTEM.md`](INTERNAL_TYPE_SYSTEM.md)
- [`ROADMAP_RESOURCES.md`](../../guides/roadmap/ROADMAP_RESOURCES.md)
