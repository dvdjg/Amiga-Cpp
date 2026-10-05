# Referencia — carga asíncrona

Cargar de disco **sin bloquear el frame**. La E/S se lanza y el backend la resuelve de forma **diferida**: el bucle llama `os::file_pump()` (o cede) y postea `FileDone`/`FileError`; por frame se drena el puerto y se completa la operación.

## `AsyncRead` — `res/async_load.hpp` (R6.7)

`AsyncRead` (`async_load.hpp:35`) lee un fichero de forma asíncrona: `begin(path, dst)` (`:47`) abre y lanza la lectura de hasta `dst.size()` bytes (deferida en Amiga), `state()` da `AsyncReadState` (`:32`: `Idle`/`Pending`/`Done`/`Failed`), y `on_done(m)` completa con el `FileDone` de la cola.

```cpp
eng::res::AsyncRead load;
load.begin("data/code/answer.engz", eng::Span<eng::u8> {buf, sizeof(buf)});
// por frame: os::file_pump(); while (os::system_port().pop(m)) load.on_done(m);
if (load.done()) { /* buf listo */ }
```

El `cookie` de la petición es `IoUser{'L', id}` (`kTag = 'L'` loader, `:39`), distinto del `'A'` de la caché de assets: el `on_done` solo acepta el suyo. La identidad con **generación** (`RequestTable`, `os/request.hpp`) la aporta el consumidor multiplexado. Aplicación típica: una **transición de zona** que prefetchea el código siguiente sin parar el juego.

## `AsyncOverlay` — `res/async_overlay.hpp`

`AsyncOverlay` (`async_overlay.hpp:31`) encadena, **sin bloquear el frame**, la lectura del contenedor (`AsyncRead`), su **decodificación** (`decode_engz`) y la **carga del HUNK** por segmento (`DynLoader`, R6.3): cierra los `⏳` de R6.4/R6.6. `begin(mem, path, container, scratch)` (`:47`) lanza la E/S (el `MemoryManager` entra por `Ref`, no propietario); por frame, `os::file_pump()` + drenar el puerto llamando `on_done(m)` avanza la cadena. `ready()`/`handle()`/`unload()` consultan y cierran.

```cpp
eng::res::AsyncOverlay overlay {dyn, app.memory_manager()};
overlay.begin("data/code/zone1.engz", {comp, sizeof comp}, {image, sizeof image});
// por frame: os::file_pump(); ... overlay.on_done(m);
if (overlay.ready()) { auto fn = (Fn) dyn.symbol(overlay.handle(), "answer"); fn(); overlay.unload(); }
```

`DynLoader` y `MemoryManager` entran por **referencia** (no son observadores `Ref`: la vida del overlay es la de quien lo declara, típicamente el juego/una zona). El estado es `State` (`:33`): `Idle`/`Reading`/`Ready`/`Failed`.

Volver al [índice de `res/`](README.md) · [Referencia](../README.md) · [manual](../../README.md).
