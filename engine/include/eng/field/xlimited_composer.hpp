#pragma once

/// \file xlimited_composer.hpp
/// Compositores de display de XLimited/corkscrew: `RasterColorZone`,
/// `XlimitedDisplayComposer` (single) y `XlimitedDualComposer` (DPF). Definido aparte de
/// `xlimited_base.hpp`; `xlimited.hpp` es la cabecera de familia.

#include <eng/field/xlimited_playfield.hpp>

namespace eng::field {

/// Compositor mínimo para XLimited/corkscrew (single playfield interleaved).
///
/// Es la única capa que toca BPLCON0/BPLCON1/BPLxPT/BPLMOD/DIW/DDF. A
/// diferencia de `DpfDisplayComposer` no gestiona dos playfields; sí emite el
/// **split vertical** del corkscrew (segundo juego de BPLxPT a la fila 0 del
/// bucle de display cuando `view.split_active`). Paleta al inicio del frame y
/// doble buffer de copperlist. Úsalo así:
///
///   XlimitedDisplayComposer comp;
///   comp.init(memory, {palette, 1536, planes});
///   comp.compose(field.hardware_view());
///   comp.install(backend);
///
/// El compositor emite la lista completa en **dos bloques** (`copper::DoubleBuffer`)
/// y alterna el activo: `compose()` vuelve a emitir la lista entera en el bloque
/// inactivo (DMACON/BPLCON/DIW/DDF/paleta/punteros) y `install()` publica
/// `COP1LC`. NO parchea words sueltos: el parcheo de 13 words existe solo en
/// `TileScrollScene::patch_copper` (`drivers/tile_scroll.hpp`). Re-emitir cuesta mas
/// que parchear, pero deja la lista final auditable; unificarlo es F1 de
/// `docs/guides/roadmap/NORMALIZACION_REPO.md`.
/// Zona de color por raster (raster colors): en la línea `line` el registro `reg`
/// pasa a `color`. Las zonas deben venir en orden ASCENDENTE de línea. Requieren
/// un display SIN split de Copper (`linear_display`) para no desordenar el raster.
/// Compartida por los compositores single y dual.
struct RasterColorZone {
    u16 line = 0;
    copper::Register reg = copper::Register::COLOR00;
    u16 color = 0;
};

/// Construye una zona de color por raster para el registro COLOR`index`.
constexpr RasterColorZone raster_color(u16 line, u8 color_index, u16 color) {
    return { line, static_cast<copper::Register>(copper::color_register(color_index)), color };
}

class XlimitedDisplayComposer {
public:
    using ColorZone = RasterColorZone;

    struct Config {
        eng::PaletteWords palette {}; // 2^planes colores (8/16/32/64 segun planes 3..6)
        u32 copper_bytes = 1536;
        u8 planes = 4; // rango 3..6 (3=8c, 4=16c, 5=32c, 6=EHB/DPF 3+3)
        u16 diwstrt = xlimited_detail::kDiwStrt;
        u16 diwstop = xlimited_detail::kDiwStop;
        u16 ddfstrt = xlimited_detail::kDdfStrt;
        u16 ddfstop = xlimited_detail::kDdfStop;
        const graphics::SpriteManager* sprites = nullptr; // opcional
        // Raster colors opcionales (gradiente del color del patrón de fondo).
        const ColorZone* color_zones = nullptr;
        u8 color_zone_count = 0;
    };

    bool init(MemorySystem& memory, const Config& cfg) {
        m_cfg = cfg;
        if (!m_copper.begin(memory, cfg.copper_bytes) || cfg.palette.empty()) return false;
        m_initialized = true;
        return true;
    }

    bool compose(const PlayfieldHardwareView& view) {
        return compose(view, nullptr);
    }

    /// Zona OVERLAY genérica: un playfield SEPARADO mostrado en la franja inferior
    /// de la ventana (raster `DIWSTRT_y + view.viewport_h` en adelante). Es un
    /// MECANISMO del display (cambiar BPLCON1/BPL1/2MOD/BPLxPT + paleta en un
    /// raster fijo); no es específico de HUD. El HUD como patrón (texto/marcas)
    /// se compone a NIVEL DE ESCENA como un `CanvasPlayfield` + `Surface`, y se
    /// pasa aquí como la zona overlay. La escena mantiene la ventana DIW abierta
    /// a su tamaño total y la zona solo programa la franja en el raster de corte.
    struct OverlayZone {
        PlayfieldHardwareView view;    // playfield del overlay (canvas)
        eng::PaletteWords palette {};  // paleta del overlay (0..2^planes-1), opcional
        u8 palette_colors = 0;         // nº de colores a emitir (0 = ninguno)
        // Si el overlay NO comparte geometría con el campo (distinto nº de planos),
        // esta zona conmuta BPLCON0/BPLCON4/BPLMOD + punteros en un WAIT (MI09). Si
        // comparte geometría se usa el split de punteros conservador (BitplaneSplit),
        // que no reprograma BPLCON0/DDF/BPLMOD a mitad de frame.
        bool use_mode_switch = false;
        graphics::ModeSwitchZone mode_switch {};
    };

    bool compose(const PlayfieldHardwareView& view, const OverlayZone* hud) {
        if (!m_initialized || !view.bitplanes) return false;
        if (!valid_view(view)) return false;
        if (!m_copper_initialized) {
            // Primer frame: emitir la lista completa en AMBOS bloques y dejar activo
            // el ultimo (patron de `copper::DoubleBuffer`).
            if (!emit_full(view, hud)) return false;
            m_copper.flip();
            if (!emit_full(view, hud)) return false;
            m_copper.flip();
            m_copper_initialized = true;
            return true;
        }
        if (!emit_full(view, hud)) return false;
        m_copper.flip();
        return true;
    }

    /// Toma el control del display e instala la primera copperlist (una vez).
    template <typename Backend>
    void takeover(Backend& backend) const {
        if (m_initialized && m_copper_initialized) {
            m_copper.takeover(backend);
        }
    }

    template <typename Backend>
    void install(Backend& backend) const {
        if (m_initialized && m_copper_initialized) {
            m_copper.install(backend);
        }
    }

    constexpr bool ok() const { return m_ok; }
    constexpr u16 copper_words() const { return m_copper_words; }
    /// Depuración: puntero al bloque de copper activo.
    const u16* debug_active_copper() const {
        return m_copper_initialized ? m_copper.active_words() : nullptr;
    }

private:
    static constexpr u16 pointer_high_word(u8 plane) {
        return static_cast<u16>(21u + plane * 4u);
    }
    static constexpr u16 pointer_low_word(u8 plane) {
        return static_cast<u16>(23u + plane * 4u);
    }

    bool valid_view(const PlayfieldHardwareView& v) const {
        if (!v.bitplanes || v.planes == 0 || v.planes > 6) return false;
        if (v.bitmap_bytes_per_row == 0) return false;
        if (v.bitmap_height == 0) return false;
        if (v.display_height == 0 || v.display_offset >= v.display_height) return false;
        if (v.viewport_w == 0 || v.viewport_h == 0) return false;
        // Validación genérica BPLMOD: debe ser bitmap_bytes_per_row*planes - viewport_w/8 - modulo_offset
        // No se valida con literal 320; se comprueba que bpl1mod no excede el total.
        if (v.plane_bytes < static_cast<u32>(v.bitmap_bytes_per_row * v.bitmap_height * v.planes)) return false;
        // Split: la fila del wrap cae dentro de la ventana y no sobrepasa el buffer.
        if (v.split_active && v.split_line >= v.viewport_h) return false;
        // LÍMITE OCS (fallo RÁPIDO): el WAIT del Copper compara solo 8 bits de línea
        // (0..255). Con split MÓVIL, la línea de corte máxima es
        // DIWSTRT_y + viewport_h - 1 = viewport_h + 40; debe quedar <= 255, luego el
        // campo visible debe ser <= 215. NO usar 256 (produce el wrap adelantado).
        // Canónico: 208 (13 filas de tile) + HUD si se quieren ocupar 256.
        if (v.split_active && static_cast<u16>(v.viewport_h + 40u) > 255u) return false;
        return true;
    }

    bool emit_full(const PlayfieldHardwareView& view, const OverlayZone* hud = nullptr) {
        copper::SchedulerT<false> sched { m_copper.inactive_block() };
        const u16 bplcon0 = static_cast<u16>(
            0x0200u | (static_cast<u16>(view.planes) << 12u));
        sched.move(copper::Register::DMACON,
            static_cast<u16>(copper::DmaSetClear | copper::DmaMaster |
                             copper::DmaCopper | copper::DmaBitplane |
                             (m_cfg.sprites ? m_cfg.sprites->dma_bits() : 0)));
        sched.move(copper::Register::BPLCON0, bplcon0);
        sched.move(copper::Register::BPLCON1, view.bplcon1);
        sched.move(copper::Register::BPLCON2, 0x0000);
        // EHB (HalfBrite): con 6 planos en modo SINGLE, el bit 0 de BPLCON4 activa
        // el modo EHB — el plano 6 deja de ser un bit de color y actúa como
        // selector "half": color = base/2. Es la semántica del tilebank BASES-
        // PRIMERO de la 201 (índices 0..31 base, 32..63 = half automático).
        if (view.planes == 6u) sched.move(copper::Register::BPLCON4, 0x0001u);
        sched.move(copper::Register::BPL1MOD, view.bpl1mod);
        sched.move(copper::Register::BPL2MOD, view.bpl2mod);
        sched.move(copper::Register::DIWSTRT, m_cfg.diwstrt);
        sched.move(copper::Register::DIWSTOP, m_cfg.diwstop);
        sched.move(copper::Register::DDFSTRT, m_cfg.ddfstrt);
        sched.move(copper::Register::DDFSTOP, m_cfg.ddfstop);
        // Paleta primero: si el split está en una línea alta, los MOVEs de color
        // deben aplicar al inicio del frame y no tras el WAIT del split.
        sched.emit_palette(m_cfg.palette);
        for (u8 p = 0; p < view.planes; ++p) {
            // soft DPF: el plano de fondo se lee de su propio buffer (doble buffer).
            const u8* base = (view.bg_plane_base != nullptr && p == view.parallax_plane)
                             ? view.bg_plane_base : view.real_base;
            const u32 addr = static_cast<u32>(reinterpret_cast<uintptr>(base)) +
                             view.planeaddx + view.planeaddy +
                             static_cast<u32>(p) * view.bitmap_bytes_per_row;
            // En interleaved, Planes[p] = base + p*BITMAPBYTESPERROW + Y*planes*bytes.
            // planeaddy aporta el offset vertical (display_offset) y planeaddx el horizontal.
            sched.move_bitplane_pointer(p, eng::ChipAddress { addr });
        }
        // Raster colors: WAIT en cada línea + MOVE del color (orden ASCENDENTE).
        // Requieren un display lineal (sin split de Copper) para no desordenar el raster.
        for (u8 z = 0; z < m_cfg.color_zone_count; ++z) {
            const ColorZone& zone = m_cfg.color_zones[z];
            sched.wait_line(zone.line);
            sched.move(zone.reg, zone.color);
        }
        // Split vertical del corkscrew: al llegar a `split_line` filas dentro de
        // la ventana, los punteros vuelven a la fila 0 del bucle de display.
        // Limitación OCS: el comparador de WAIT del Copper usa 8 bits con
        // semántica ">=" (vpos&0xFF >= vcmp, SIN bit V8; verificado en
        // WinUAE-DBG/custom.cpp coppercomp), por lo que una línea raster>255 no se
        // puede esperar con precisión: el WAIT dispara en la primera coincidencia
        // del byte bajo (línea raster-256) y el original (XYLimited) también
        // degrada a 255. Se recorta a 255: la banda de 1..41 filas al pie muestra
        // el wrap adelantado (inherente al chipset; ver AMIGA_8WAY_SCROLLING.md §12).
        // NOTA: NO usar wait_line_safe aquí (port de CopWaitSafe, par 0xffdf/0xfffe):
        // ese doble-WAIT tampoco compara V8 en este emulador y produce recortes
        // incorrectos. La limitación es del comparador, no del overflow.
        u16 raster = 0;
        if (view.split_active) {
            raster = static_cast<u16>((m_cfg.diwstrt >> 8u) + view.split_line);
            const u8 wait = raster > 0xffu ? 0xffu : static_cast<u8>(raster);
            sched.wait_line(wait);
            for (u8 p = 0; p < view.planes; ++p) {
                const u8* base = (view.bg_plane_base != nullptr && p == view.parallax_plane)
                                 ? view.bg_plane_base : view.real_base;
                const u32 addr = static_cast<u32>(reinterpret_cast<uintptr>(base)) +
                                 view.planeaddx + view.split_planeaddy +
                                 static_cast<u32>(p) * view.bitmap_bytes_per_row;
                sched.move_bitplane_pointer(p, eng::ChipAddress { addr });
            }
        }
        // El blanking de abajo solo si no estorba con un split en línea alta.
        if (hud != nullptr) {
            if (hud->use_mode_switch) {
                // Geometría DISTINTA (p. ej. HUD con menos planos que el campo):
                // `ModeSwitchZone` reprograma BPLCON0/BPLCON4/BPLCON1/DDF/BPLMOD y
                // los punteros en un WAIT, en el orden canónico (MI09).
                sched.emit_mode_switch_zone(hud->mode_switch);
            } else {
            // Zona HUD como SPLIT DE PUNTEROS con geometría IDÉNTICA al campo
            // corkscrew: en el raster `DIWSTRT_y + view.viewport_h` solo se
            // cambian BPLxPT (+ BPLCON1=0 para anular el fine scroll y, si hay
            // paleta propia, sus colores). El lienzo del HUD COMPARTE el layout
            // del campo (planos, filas de viewport_w + guarda, DDFSTRT y BPLMOD
            // del campo), por lo que NO se reprograman BPLCON0/DDF/BPLMOD a mitad
            // de frame. El split vertical del corkscrew ya conmuta punteros a mitad
            // de frame con la MISMA geometría y funciona; esta zona replica ese
            // patrón conmutando al lienzo del HUD.
            const u16 hud_raster = static_cast<u16>((m_cfg.diwstrt >> 8u) + view.viewport_h);
            sched.wait_line(hud_raster > 0xffu ? 0xffu : static_cast<u8>(hud_raster));
            sched.move(copper::Register::BPLCON1, 0x0000);
            for (u8 p = 0; p < hud->view.planes; ++p) {
                const u32 addr = static_cast<u32>(reinterpret_cast<uintptr>(hud->view.real_base)) +
                                 static_cast<u32>(p) * hud->view.bitmap_bytes_per_row;
                sched.move_bitplane_pointer(p, eng::ChipAddress { addr });
            }
            if (!hud->palette.empty()) {
                sched.emit_palette(hud->palette, 0, hud->palette_colors);
            }
            }
        } else if (!view.split_active || raster < 0xf8u) {
            sched.wait_line(0xf8);
            sched.move(copper::Register::COLOR00, 0x0000);
        }
        // Sprites hardware (delante de los playfields): SPRxCTL/POS/PT + paleta 16-31.
        if (m_cfg.sprites != nullptr && m_cfg.sprites->any_enabled()) {
            sched.emit_palette(m_cfg.palette, 16, 16); // paleta de sprites (COLOR16-31)
            m_cfg.sprites->emit_into(sched);
        }
        sched.end();
        m_copper_words = sched.words_used();
        m_ok = sched.ok();
        return m_ok;
    }

    Config m_cfg {};
    copper::DoubleBuffer m_copper {};
    u16 m_copper_words = 0;
    bool m_initialized = false;
    bool m_copper_initialized = false;
    bool m_ok = false;
};

/// Compositor dual playfield (DPF 3+3) para dos `XLimitedPlayfield` (corkscrew).
///
/// PF1 usa los planos de hardware 1,3,5 y PF2 los 2,4,6 (cada playfield es un
/// bitmap interleaved independiente de profundidad `planes_per_field`). Ambos
/// comparten `BPLCON1` (dos nibbles de fine scroll), `BPLCON2` (prioridad) y el
/// split vertical (misma `display_height` y, si scrollean juntos en Y, misma
/// `split_line`). Cada playfield conserva su `planeaddx` (parallax en X posible).
class XlimitedDualComposer {
public:
    using ColorZone = RasterColorZone;

    struct Config {
        eng::PaletteWords palette {};      // 16 colores: PF1 0..7, PF2 8..15
        u32 copper_bytes = 1536;
        u8 planes_per_field = 3;           // 3+3 → 6 planos de hardware
        bool foreground_is_pf2 = false;    // BPLCON2 PF2PRI
        u16 diwstrt = xlimited_detail::kDiwStrt;
        u16 diwstop = xlimited_detail::kDiwStop;
        u16 ddfstrt = xlimited_detail::kDdfStrt;
        u16 ddfstop = xlimited_detail::kDdfStop;
        // Raster colors opcionales (gradiente del color del patron de fondo).
        const ColorZone* color_zones = nullptr;
        u8 color_zone_count = 0;
    };

    bool init(MemorySystem& memory, const Config& cfg) {
        m_cfg = cfg;
        if (!m_copper.begin(memory, cfg.copper_bytes) || cfg.palette.empty()) return false;
        m_initialized = true;
        return true;
    }

    bool compose(const PlayfieldHardwareView& pf1, const PlayfieldHardwareView& pf2) {
        if (!m_initialized) return false;
        if (!valid(pf1, pf2)) return false;
        if (!m_copper_initialized) {
            // Primer frame: emitir en AMBOS bloques (patron de `copper::DoubleBuffer`).
            if (!emit_full(pf1, pf2)) return false;
            m_copper.flip();
            if (!emit_full(pf1, pf2)) return false;
            m_copper.flip();
            m_copper_initialized = true;
            return true;
        }
        if (!emit_full(pf1, pf2)) return false;
        m_copper.flip();
        return true;
    }

    /// Toma el control del display e instala la primera copperlist (una vez).
    template <typename Backend>
    void takeover(Backend& backend) const {
        if (m_initialized && m_copper_initialized) {
            m_copper.takeover(backend);
        }
    }

    template <typename Backend>
    void install(Backend& backend) const {
        if (m_initialized && m_copper_initialized) {
            m_copper.install(backend);
        }
    }

    constexpr bool ok() const { return m_ok; }
    constexpr u16 copper_words() const { return m_copper_words; }

private:
    static constexpr u8 hardware_plane(u8 pf1_plane, bool is_pf1) {
        // PF1 → planos 1,3,5 (índices 0,2,4); PF2 → 2,4,6 (índices 1,3,5).
        return static_cast<u8>(pf1_plane * 2u + (is_pf1 ? 0u : 1u));
    }

    static u32 field_plane_address(const PlayfieldHardwareView& v, u8 plane, u32 y_offset) {
        return static_cast<u32>(reinterpret_cast<uintptr>(v.real_base)) + v.planeaddx + y_offset +
               static_cast<u32>(plane) * v.bitmap_bytes_per_row;
    }

    bool valid(const PlayfieldHardwareView& a, const PlayfieldHardwareView& b) const {
        g_dbg_dual_valid = 0;
        if (!a.bitplanes || !b.bitplanes) { return false; }
        if (a.planes != m_cfg.planes_per_field || b.planes != m_cfg.planes_per_field) { return false; }
        if (a.planes + b.planes > 6) { return false; }
        // Un campo puede ser ESTÁTICO (CanvasPlayfield, display_height == su
        // viewport) o LINEAL (mirror, sin split). Un campo ESTÁTICO es compatible
        // con cualquier otro (no comparte bucle vertical). Solo cuando AMBOS
        // envuelven hay que exigir un `display_height` compatible.
        const bool a_static = (a.display_height == a.viewport_h);
        const bool b_static = (b.display_height == b.viewport_h);
        if (!a_static && !b_static) {
            if (b.display_height != a.display_height && b.display_height != a.viewport_h) { return false; }
            if (a.display_height != b.display_height && a.display_height != b.viewport_h) { return false; }
        }
        // DPF MIXTO: cada campo puede llevar split O no, de forma INDEPENDIENTE
        // (el campo lineal/mirror no envuelve y su Y es libre). Solo si AMBOS
        // tienen split activo deben compartir la misma línea (mismo Y).
        if (a.split_active && b.split_active && a.split_line != b.split_line) { return false; }
        // LÍMITE OCS (fallo RÁPIDO): split móvil con WAIT de 8 bits (ver composer single).
        if (a.split_active || b.split_active) {
            const u16 vh = a.split_active ? a.viewport_h : b.viewport_h;
            if (static_cast<u16>(vh + 40u) > 255u) return false;
        }
        return true;
    }

    bool emit_full(const PlayfieldHardwareView& pf1, const PlayfieldHardwareView& pf2) {
        copper::SchedulerT<false> sched { m_copper.inactive_block() };
        const u8 total = static_cast<u8>(m_cfg.planes_per_field * 2u);
        const u16 bplcon0 = static_cast<u16>(0x0200u | (static_cast<u16>(total) << 12u) | 0x0400u);
        // BPLCON1: nibble bajo = fine de PF1, alto = fine de PF2.
        const u16 bplcon1 = static_cast<u16>(
            ((pf2.bplcon1 & 0x0f) << 4) | (pf1.bplcon1 & 0x0f));
        sched.move(copper::Register::DMACON,
            static_cast<u16>(copper::DmaSetClear | copper::DmaMaster |
                             copper::DmaCopper | copper::DmaBitplane));
        sched.move(copper::Register::BPLCON0, bplcon0);
        sched.move(copper::Register::BPLCON1, bplcon1);
        sched.move(copper::Register::BPLCON2, m_cfg.foreground_is_pf2 ? 0x0040u : 0x0000u);
        // Módulos: en DPF AMBOS playfields se muestran con el MISMO DDF/fetch, así
        // que el módulo de cada uno debe usar el fetch real de PF1 (no el suyo
        // propio, que puede diferir: un CanvasPlayfield asume fetch estándar -40
        // mientras el corkscrew usa -42). `fetch = row_bytes*planes - bpl1mod`.
        const u16 fetch1 = static_cast<u16>(
            static_cast<u32>(pf1.bitmap_bytes_per_row) * m_cfg.planes_per_field - pf1.bpl1mod);
        sched.move(copper::Register::BPL1MOD, pf1.bpl1mod); // planos 1,3,5
        sched.move(copper::Register::BPL2MOD, static_cast<u16>(
            static_cast<u32>(pf2.bitmap_bytes_per_row) * m_cfg.planes_per_field - fetch1)); // 2,4,6
        sched.move(copper::Register::DIWSTRT, m_cfg.diwstrt);
        sched.move(copper::Register::DIWSTOP, m_cfg.diwstop);
        sched.move(copper::Register::DDFSTRT, m_cfg.ddfstrt);
        sched.move(copper::Register::DDFSTOP, m_cfg.ddfstop);
        sched.emit_palette(m_cfg.palette, 0, 16); // DPF: 16 colores (PF1 0..7, PF2 8..15)
        for (u8 i = 0; i < m_cfg.planes_per_field; ++i) {
            const u8 hw1 = hardware_plane(i, true);
            const u8 hw2 = hardware_plane(i, false);
            sched.move_bitplane_pointer(hw1,
                eng::ChipAddress { field_plane_address(pf1, i, pf1.planeaddy) });
            sched.move_bitplane_pointer(hw2,
                eng::ChipAddress { field_plane_address(pf2, i, pf2.planeaddy) });
        }
        u16 raster = 0;
        // Raster colors: WAIT en cada linea + MOVE del color (orden ascendente).
        // Se emiten tras los punteros; requieren que el campo NO use split de
        // Copper (linear_display) para no desordenar el raster.
        for (u8 z = 0; z < m_cfg.color_zone_count; ++z) {
            const ColorZone& zone = m_cfg.color_zones[z];
            sched.wait_line(zone.line);
            sched.move(zone.reg, zone.color);
        }
        // Split vertical POR CAMPO: re-apunta al inicio del bucle SOLO el campo
        // que envuelve (split_active). En DPF MIXTO un campo puede ser corkscrew
        // (split) y el otro lineal/mirror (sin split, Y independiente): el lineal
        // nunca se re-apunta, su display lee contiguo su mirror.
        const bool aS = pf1.split_active, bS = pf2.split_active;
        if (aS || bS) {
            const u16 split_line = aS ? pf1.split_line : pf2.split_line;
            raster = static_cast<u16>((m_cfg.diwstrt >> 8u) + split_line);
            const u8 wait = raster > 0xffu ? 0xffu : static_cast<u8>(raster);
            sched.wait_line(wait);
            for (u8 i = 0; i < m_cfg.planes_per_field; ++i) {
                if (aS) {
                    sched.move_bitplane_pointer(hardware_plane(i, true),
                        eng::ChipAddress { field_plane_address(pf1, i, pf1.split_planeaddy) });
                }
                if (bS) {
                    sched.move_bitplane_pointer(hardware_plane(i, false),
                        eng::ChipAddress { field_plane_address(pf2, i, pf2.split_planeaddy) });
                }
            }
        }
        if (!aS || raster < 0xf8u) {
            sched.wait_line(0xf8);
            sched.move(copper::Register::COLOR00, 0x0000);
        }
        sched.end();
        m_copper_words = sched.words_used();
        m_ok = sched.ok();
        return m_ok;
    }

    Config m_cfg {};
    copper::DoubleBuffer m_copper {};
    u16 m_copper_words = 0;
    bool m_initialized = false;
    bool m_copper_initialized = false;
    bool m_ok = false;
};

} // namespace eng::field

/// Depuración: resultado del `valid()` del compositor dual (0 = OK).
volatile eng::u32 eng::field::g_dbg_dual_valid = 0;

