#pragma once

/// \file polygon_fill_service.hpp
/// Puente engine -> backend para rellenar polígonos por hardware (Blitter).
///
/// **NO VERIFICADA** (regla de verificación por demo de `AGENTS.md`): ninguna demo
/// exitosa la ejercita; el único consumidor era un intento de demo descartado por
/// rotura del relleno (costuras entre caras y rayas en caras de canto). Puede
/// cambiar o eliminarse sin aviso. Para rellenar mallas de caras que deben encajar,
/// revisar antes si la primitiva correcta es el contorno XOR + un solo area fill
/// (ruta fiel de la demo 116), en vez del relleno por cara.
///
/// `Playfield` vive en el núcleo del engine y es agnóstico de la plataforma: el
/// relleno por defecto es el scanline CPU. El Blitter de Amiga vive en el
/// backend. Para que la API de alto nivel (`Surface::fill_polygon`) use hardware
/// sin que el llamador cambie, el backend **instala** este callback en su init
/// (`MinimalBackend::configure_memory`) y el playfield lo consulta; si no hay
/// callback (host, o backend sin Blitter), el playfield cae a su relleno CPU.
/// Es el mismo patrón de singleton de proceso que el resto del engine.
///
/// ASCII del flujo:
///
///   app: surf.fill_polygon(xs, ys, n, color)
///          -> Surface (recorta al clip)
///          -> CanvasPlayfield::fill_polygon
///               -> polygon_fill_service().fill( ... )   [backend Amiga]
///               -> si null/false: Playfield::fill_polygon (scanline CPU)
///
/// Diseño de la firma (obligatorio por `INTERNAL_TYPE_SYSTEM.md` §9): los
/// **buffers** van como vistas de dominio (`PlaneBytes`, `MaskBuffer`) con su
/// tamaño, los **escalares de geometría** a secas (`u8`/`u16`/`u32`, §3.3) y los
/// vértices como `Span` (sin el patrón "puntero + count"). El crudo (`data()`)
/// solo aparece dentro del backend.

#include <eng/core/domains.hpp>
#include <eng/core/span.hpp>

namespace eng::field {

/// Callback de relleno de polígono instalado por el backend.
///
/// Contrato del callback (todo en píxeles/bytes del LIENZO; el polígono ya viene
/// recortado al clip del llamador):
///   - `dst`               buffer de los planos del lienzo (INTERLEAVED, Chip RAM).
///   - `planes`            nº de bitplanes.
///   - `row_bytes`         bytes por scanline de un plano (`width/8`, múltiplo de 2).
///   - `plane_base_stride` bytes de la base del plano `p` a la del plano `p+1`
///                         (interleaved: `row_bytes`).
///   - `plane_row_stride`  bytes de una fila a la siguiente DENTRO de un plano
///                         (interleaved: `row_bytes*planes`).
///   - `width`/`height`    límites del lienzo; el callback recorta el bbox a ellos.
///   - `xs`/`ys`           vértices en píxeles del lienzo (mismo tamaño, > 2).
///   - `mask`              scratch de 1 bit del LLAMADOR en Chip RAM, de al menos
///                         `row_bytes*height`; su contenido no se conserva.
/// Devuelve `false` si no puede rellenar (p. ej. scratch insuficiente); entonces
/// el playfield delega en el relleno CPU. Un polígono totalmente fuera del lienzo
/// es un éxito sin píxeles (devuelve `true`).
struct PolygonFillService {
    using FillFn = bool (*)(eng::PlaneBytes dst, eng::u8 planes, eng::u16 row_bytes,
                            eng::u32 plane_base_stride, eng::u32 plane_row_stride,
                            eng::u16 width, eng::u16 height,
                            eng::Span<const eng::s16> xs, eng::Span<const eng::s16> ys,
                            eng::u8 color, eng::MaskBuffer mask);
    FillFn fill = nullptr;
};

/// Instancia única de proceso: la fija el backend, la lee el engine. La
/// inicialización es constante (sin guardia de runtime).
inline PolygonFillService& polygon_fill_service() {
    static PolygonFillService s {};
    return s;
}

} // namespace eng::field
