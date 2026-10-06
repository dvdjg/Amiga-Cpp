#pragma once

/// \file sprite_manager.hpp
/// Gestión de sprites hardware del Amiga OCS a nivel de escena.
///
/// El chipset dispone de 8 sprites DMA. Cada sprite:
///   - es un bitmap de 1-2 words de ancho (16/32 px) y hasta 128 líneas;
///   - su DATA vive en Chip RAM (el DMA lee SPRxDATA/SPRxDATB desde SPRxPT);
///   - se posiciona con SPRxPOS (vstart/hstart) y SPRxCTL (vstop, doble ancho);
///   - usa los colores COLOR16-31 (compartidos entre sprites y, en DPF, con PF2);
///   - su prioridad frente a los playfields la fija `BPLCON2` (`PF1P`/`PF2P`): un sprite
///     puede quedar delante o detrás de cada playfield; la prioridad ENTRE sprites la
///     decide el orden/prioridad de los canales.
///
/// El `SpriteManager` es un componente de la escena: guarda la configuración de
/// los 8 sprites, reserva su Chip RAM y emite los MOVEs de Copper
/// (SPRxPT/SPRxPOS/SPRxCTL) dentro de la copperlist que construye el compositor.
///
/// Reglas del engine: sin heap, sin RTTI, gnu++23. La DATA del sprite la aporta
/// la aplicación (bloque Chip); el manager solo la referencia y la posición.
///
/// ```text
///   app (bitmaps)                     SpriteManager (componente de escena)        chipset
///   ─────────────                     ──────────────────────────────────         ───────
///   SpriteConfig[i] (data,w,h,pos) ─► 8 canales: config + Chip DATA ──► MOVEs de Copper
///   sprite_data() escribe Chip RAM        │                            SPRxPT / SPRxPOS / SPRxCTL
///                                         └─ COLOR16-31 (compartidos con PF2 en DPF)
///   Prioridad: BPLCON2 (PF1P/PF2P) fija sprite vs playfield; entre sprites, por canal.
/// ```

#include <eng/core/types/domains.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/types/typed.hpp>
#include <eng/core/util/noncopyable.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/sprite.hpp>
#include <eng/memory/arena.hpp>
#include <eng/memory/memory_manager.hpp>

namespace eng::graphics {

/// Configuración de un sprite hardware.
struct SpriteConfig {
    bool enabled = false;
    /// DATA en **Chip RAM** con el banco en el tipo (`ChipView<SpriteTag>`): la lee el DMA
    /// de sprites (`SPRxPT`), así que una vista Fast/Slow o sin banco **no compila**
    /// (AGENTS §1.10). Tamaño en **bytes** de `height*2*width_words` words (DAT/DATB).
    ChipView<SpriteTag> data {};
    u8 width_words = 1;          // 1 = 16 px, 2 = 32 px (SPRxCTL bit doble ancho)
    u8 height = 0;               // líneas (1..128)
    u16 hpos = 0;                // posición horizontal (px)
    u16 vstart = 0;              // línea vertical de inicio
    u16 vstop = 0;               // línea SIGUIENTE a la última visible (exclusiva; AHRM cap. 4)
    u8 palette_base = 16;        // COLOR16 + palette_base*4 (defecto 16: COLOR16-19)
    /// **Attached** al sprite anterior del par (bit 0 de `SPRxCTL`): 4 bits/píxel sobre
    /// `COLOR16-31` (15 colores). Los pares válidos son 0+1, 2+3, 4+5, 6+7; reduce los
    /// canales útiles de 8 a 4. Se puede conmutar por zona con el rearmado (el CTL es
    /// reescribible por línea). Ver `docs/reference/amiga/techniques/sprite-layer.md` §4.
    bool attach = false;
};

/// Gestor de hasta 8 sprites hardware (componente de la escena).
class SpriteManager : public eng::util::Noncopyable {
public:
    SpriteManager() = default;

    /// Reserva el bloque de DATA de sprites en Chip RAM (la app escribe los
    /// bitmaps con `sprite_data()`). No reserva copper (lo hace el compositor).
    /// \param memory      gestor de memoria (bloque en Chip).
    /// \param data_bytes  tamaño del bloque de DATA de sprites.
    /// \return `true` si la reserva cupo.
    bool init(MemoryManager& memory, u32 data_bytes) {
	m_data = eng::Block<eng::SpriteTag> {memory.chip().reserve<eng::SpriteTag>(data_bytes, 16)};
	return m_data.valid();
    }

    /// Acceso acotado al bloque de DATA (el tamaño viaja con el puntero).
	Span<u8> sprite_data() { return { m_data.view.data(), m_data.view.size() }; }
	Span<const u8> sprite_data() const { return { m_data.view.as_const().data(), m_data.view.size() }; }

    /// Configura el canal `index` (0..7). Silenciosamente ignorado si `index >= 8` o si la
    /// DATA del `cfg` no cubre `height*width_words*2` words (se deshabilita ese canal).
    /// \param index  canal de sprite (0..7).
    /// \param cfg    configuración del canal (posición/DATA/tamaño/paleta).
    void set(u8 index, const SpriteConfig& cfg) {
        if (index >= 8) return;
        m_spr[index] = cfg;
        // Contrato de tamaño: un sprite habilitado necesita
        // `height*width_words*2` words (DAT+DATB por línea). Si la DATA del span
        // no lo cubre, se descarta (no se emite) en lugar de leer fuera de rango.
        if (cfg.enabled && !cfg.data.empty()) {
            // `height*width_words*2` words = ×2 bytes (la vista es de bytes).
            const usize need = static_cast<usize>(cfg.height) * cfg.width_words * 2u * 2u;
            if (cfg.data.size() < need) m_spr[index].enabled = false;
        }
    }
    void disable(u8 index) { if (index < 8) m_spr[index] = SpriteConfig{}; }
    void disable_all() { for (auto& s : m_spr) s = SpriteConfig{}; }

    /// ¿Algún sprite habilitado?
    bool any_enabled() const {
        for (const auto& s : m_spr) if (s.enabled) return true;
        return false;
    }

    /// Bits de DMACON que activan el DMA de sprites. En el chipset es un **único bit**
    /// (`SPREN`, bit 5) que habilita los 8 canales: no existen bits por sprite. Un canal
    /// que no debe verse se desactiva con su `SPRxPT` a 0 (o con `VSTOP <= VSTART`).
    /// Referencia: AHRM 3.ª (`MOVE.W #$83A0,DMACON` = `DMAEN|BPLEN|COPEN|SPREN`).
    u16 dma_bits() const {
        for (const auto& s : m_spr) {
            if (s.enabled && !s.data.empty()) return 0x0020u; // SPREN
        }
        return 0u;
    }

    /// Emite SPRxPT / SPRxPOS / SPRxCTL de los sprites habilitados en el Copper.
    /// Llámala desde el compositor antes de `end()` (paleta y sprites al final).
    ///
    /// Agrupa por `vstart`: emite **un** `WAIT` por línea distinta (en orden ascendente) y
    /// luego una **ráfaga** con los sprites de esa línea. Así dos sprites en la misma
    /// `vstart` no provocan un segundo `WAIT` (que encontraría el haz pasado y esperaría al
    /// frame siguiente) y el orden no depende del índice de canal.
    template <class Sched>
    void emit_into(Sched& sched) const {
        bool done[8] = {};
        for (u8 pass = 0; pass < 8; ++pass) {
            u8 pick = 0xff;
            for (u8 i = 0; i < 8; ++i) {
                const SpriteConfig& s = m_spr[i];
                if (done[i] || !s.enabled || s.data.empty()) continue;
                if (pick == 0xff || s.vstart < m_spr[pick].vstart) pick = i;
            }
            if (pick == 0xff) break;
            const u16 line = m_spr[pick].vstart;
            sched.wait_line_safe(line);
            for (u8 i = 0; i < 8; ++i) {
                const SpriteConfig& s = m_spr[i];
                if (done[i] || !s.enabled || s.data.empty() || s.vstart != line) continue;
                emit_config(sched, i, s, s.data);
                done[i] = true;
            }
        }
    }

    /// Emite un `HwSpriteTemplate` con multiplexado vertical y cambios de paleta.
    ///
    /// Toma UNA imagen fuente troceada en segmentos (`HwSpriteTemplate::segments`) y la
    /// sirve por el MISMO canal hardware: cada segmento se rearma en su linea (técnica
    /// "chasing the raster" / multiplexado vertical). Además aplica los cambios de
    /// paleta por franja (`switches`) escribiendo COLORxx entre medias.
    ///
    /// `channel` es el canal 0..7; `base_y` la primera linea del primer segmento.
    /// Requiere que la `HwSpriteTemplate` describa segmentos con `data_offset` creciente
    /// (words desde el inicio de `bitmap`); cada linea de sprite ocupa
    /// `width_words*2` words (DAT y DATB intercalados).
    template <u8 MS, u8 MP, class Sched>
    void emit_template_into(
        Sched& sched,
        const HwSpriteTemplate<MS, MP>& tpl,
        u8 channel,
        u16 base_y,
        u16 hpos = 0
    ) const {
        if (channel >= 8) return;
        u16 line = base_y;
        for (u8 i = 0; i < tpl.segment_count; ++i) {
            const HwSpriteSegment& seg = tpl.segments[i];
            const Span<const u16> data_words =
                tpl.bitmap.subspan(seg.data_offset, seg.height * (tpl.width_words * 2u));
            // Puente documentado: `HwSpriteTemplate::bitmap` es una imagen **cocinada en Chip
            // RAM** (contrato de `sprite.hpp`); aquí se certifica el banco al pasar al emisor
            // DMA. Si la procedencia no fuera Chip, el bug es del productor del template.
            const ChipView<SpriteTag> data {
                Address<MemoryKind::Chip>::from_storage(data_words.data()),
                data_words.size() * 2u};
            SpriteConfig cfg {
                true, data, tpl.width_words, static_cast<u8>(seg.height & 0xffu),
                hpos, line, static_cast<u16>(line + seg.height), // VSTOP exclusivo (AHRM)
                0, // palette_base: los sprites usan COLOR16+; para multiplexar por par
                   // hay que respetar que el switch cambia el COLORxx del par (ver abajo)
            };
            // `attach` (15 colores) de la plantilla: el canal impar une su par y aporta
            // los bits 2-3 del índice (AHRM cap. 4, "Attached Sprites"). Sin esto, la
            // plantilla declararía el par pero la emisión no lo activaría.
            cfg.attach = tpl.attach;
            // WAIT en la línea VSTART del segmento (mismo patrón que el bootcamp:
            // WAIT + MOVE SPRxPOS/CTL). El primer segmento también espera; el gap
            // de 1 línea (`line += height + 1`) garantiza que el anterior terminó.
            sched.wait_line(static_cast<u8>(line & 0xffu));
            emit_config(sched, channel, cfg, data);

            // Cambios de paleta asociados a este segmento (color multiplexing).
            // Un switch con `line` dentro de [line, line+height) pertenece al segmento
            // que estamos servir: lo aplicamos aqui (el Copper ya espero a esa linea).
            const u16 seg_end = static_cast<u16>(line + seg.height);
            for (u8 s = 0; s < tpl.switch_count; ++s) {
                const HwSpritePaletteSwitch& sw = tpl.switches[s];
                if (sw.line < line || sw.line >= seg_end) continue;
                for (u8 c = 0; c < sw.count; ++c) {
                    sched.move(
                        static_cast<copper::Register>(0x180 + (sw.first + c) * 2u),
                        sw.colors[c]
                    );
                }
            }
            line = static_cast<u16>(line + seg.height + 1u); // +1 gap requerido por el DMA
        }
    }

    constexpr u16 copper_words() const {
        u16 w = 0;
        // Por sprite: 1 WAIT (2 words) + POS + CTL + PTH + PTL (4 MOVEs = 8 words).
        for (const auto& s : m_spr) if (s.enabled && !s.data.empty()) w += 10;
        return w;
    }

    /// **Armado de objetos (patrón validado)**: un solo `WAIT` en `arm_line` y, por canal
    /// habilitado, un `arm_object` (PT a la DATA, POS/CTL con `VSTOP` exclusivo y `ATTACH`).
    /// `arm_line` debe caer tras el VBlank y **antes del primer `VSTART`** (p. ej. 32). Es el
    /// camino recomendado para objetos; `emit_into` (un `WAIT` por `vstart`) queda para
    /// segmentos rearmados por línea ("chasing the raster", demo 053).
    template <class Sched>
    void emit_armed_into(Sched& sched, u16 arm_line) const {
        sched.wait_line_safe(arm_line);
        for (u8 i = 0u; i < 8u; ++i) {
            const SpriteConfig& s = m_spr[i];
            if (!s.enabled || s.data.empty()) continue;
            arm_object(sched, i, s.data, s.hpos, s.vstart, s.height, s.attach);
        }
    }

    /// **Arma un objeto de sprite** en la posición actual del Copper (patrón validado por la
    /// demo 214): escribe `SPRxPTH/L` a `data`, `SPRxPOS` y `SPRxCTL`. Llamar **tras**
    /// `wait_line_safe(arm_line)`, con una línea temprana (antes del primer `VSTART`) y **una
    /// vez por frame** para todos los canales (un solo `WAIT` compartido). La DATA apunta a la
    /// imagen (deja fuera la cabecera de la estructura si la hay); `VSTOP` es la línea
    /// **siguiente** a la última visible (AHRM cap. 4, *«the line after the last displayed
    /// row»*). `attach` (bit 7) solo es válido en el canal **impar** de un par (0+1, 2+3, …).
    /// No toca el gestor ni hardware hasta que el Copper ejecute la lista.
    template <class Sched>
    static void arm_object(Sched& sched, u8 channel, ChipView<SpriteTag> data, u16 x, u16 y,
                           u16 height, bool attach = false) {
        if (channel >= 8u || data.empty() || height == 0u) return;
        SpriteConfig cfg {};
        cfg.enabled = true;
        cfg.data = data;
        cfg.width_words = 1u;
        cfg.height = static_cast<u8>(height > 128u ? 128u : height);
        cfg.hpos = x;
        cfg.vstart = y;
        cfg.vstop = y + cfg.height; // exclusivo (AHRM cap. 4); u16 + u8 -> u16
        cfg.attach = attach;
        emit_config(sched, channel, cfg, data);
    }

    /// **Arma una lista de `HwSpritePlacement`** (salida de `compose_sprites`) soportando el
    /// **multiplexado vertical** del `SpriteAllocator`: la **primera** config de cada canal se
    /// arma en `arm_line` (línea temprana compartida, patrón de objetos HOST-428) y cada
    /// **rearma posterior** se emite en el `vstart` de su placement (patrón de segmentos de
    /// `emit_template_into`). Así un mismo canal puede servir objetos en franjas disjuntas del
    /// frame, que es lo que el allocator reparte (p. ej. ch0 arriba para un par *attached* y
    /// abajo para otro objeto).
    ///
    /// `placements` debe venir en orden **no decreciente de `vstart`** (lo garantiza
    /// `compose_sprites`: los intents se ordenan por `top`). Los placements con `vstart` por
    /// encima de `arm_line` no se rearman (no son visibles y un `WAIT` hacia atrás envolvería
    /// al frame siguiente). No usa el estado del gestor: es `arm_object` puro sobre la lista.
    template <class Sched>
    static void emit_placements_into(Sched& sched, eng::Span<const HwSpritePlacement> placements,
                                     u16 arm_line = 32u) {
        // 1) Primera config de cada canal: en la línea de armado temprana (un solo WAIT).
        bool seen[8] {};
        sched.wait_line_safe(arm_line);
        for (eng::u16 i = 0; i < placements.size(); ++i) {
            const HwSpritePlacement& p = placements[i];
            if (p.channel >= 8u || p.data.empty() || p.height == 0u || p.width_words == 0u) {
                continue;
            }
            if (seen[p.channel]) continue;
            seen[p.channel] = true;
            arm_object(sched, p.channel, p.data, p.hpos, p.vstart, p.height, p.attach);
        }
        // 2) Rearmes del multiplexado vertical: cada config posterior, en su `vstart`.
        for (eng::u16 k = 0; k < 8u; ++k) {
            seen[k] = false;
        }
        for (eng::u16 i = 0; i < placements.size(); ++i) {
            const HwSpritePlacement& p = placements[i];
            if (p.channel >= 8u || p.data.empty() || p.height == 0u || p.width_words == 0u) {
                continue;
            }
            if (!seen[p.channel]) { // primera del canal: ya armada en (1)
                seen[p.channel] = true;
                continue;
            }
            if (p.vstart <= arm_line) continue; // por encima del armado: no visible
            sched.wait_line_safe(p.vstart);
            arm_object(sched, p.channel, p.data, p.hpos, p.vstart, p.height, p.attach);
        }
    }

    /// Vuelca una lista de `HwSpritePlacement` (salida del compositor) a los 8 canales,
    /// dejando el gestor listo para `emit_into`. No toca hardware.
    /// \param placements  colocaciones (salida del compositor de sprites).
    /// \param count       nº de colocaciones.
    /// \return nº de canales aplicados (≤ 8).
    u8 apply(const HwSpritePlacement* placements, u8 count) {
        if (placements == nullptr) return 0;
        u8 applied = 0;
        for (u8 i = 0; i < count; ++i) {
            const HwSpritePlacement& p = placements[i];
            if (p.channel >= 8u || p.data.empty() || p.height == 0u || p.width_words == 0u) {
                continue;
            }
            SpriteConfig cfg {};
            cfg.enabled = true;
            // El placement ya trae la DATA **Chip** tipada (`ChipView<SpriteTag>`): no hay
            // puente aquí; el banco lo certificó el productor (compositor de sprites).
            cfg.data = p.data;
            cfg.width_words = p.width_words;
            cfg.height = static_cast<u8>(p.height > 128u ? 128u : p.height);
            cfg.hpos = p.hpos;
            cfg.vstart = p.vstart;
            cfg.vstop = static_cast<u16>(p.vstart + cfg.height); // exclusivo (AHRM cap. 4)
            // `attach` (15 colores): sin esta copia, el par del compositor se emitiría
            // como dos sprites independientes y el color 4 bits se perdería. El CTL del
            // canal impar lleva el bit 7 (`emit_config`). Ver `sprite-layer.md` §4.
            cfg.attach = p.attach;
            set(p.channel, cfg);
            ++applied;
        }
        return applied;
    }

private:
    /// Codifica y emite un sprite (POS/CTL/PT) en el canal dado.
    ///
    /// Offsets de registros custom (base $dff000): SPRxPOS = 0x140 + x*8,
    /// SPRxCTL = 0x142 + x*8, SPRxPTH = 0x120 + x*4, SPRxPTL = 0x122 + x*4.
    ///
    /// Layout de bits (AHRM cap. 5, verificado contra amiga-bootcamp
    /// `08_graphics/sprites.md`):
    ///   SPRxPOS: bits 15-8 = VSTART[7:0], bits 7-0 = HSTART[8:1]
    ///            (la posición horizontal va en low-res pixels ÷ 2: los sprites
    ///            solo se colocan en posiciones pares).
    ///   SPRxCTL: bits 15-8 = VSTOP[7:0], bit 3 = VSTART[8], bit 2 = VSTOP[8],
    ///            bit 1 = HSTART[0], bit 0 = ATTACH.
    template <class Sched>
    static void emit_config(Sched& sched, u8 channel, const SpriteConfig& s,
                            ChipView<SpriteTag> data) {
        const u16 pos = static_cast<u16>(((s.vstart & 0xff) << 8) | ((s.hpos >> 1) & 0xff));
        const u16 ctl = static_cast<u16>(
            ((s.vstop & 0xff) << 8) |
            (s.attach ? 0x0080u : 0x0000u) |     // bit 7: ATTACH (AHRM 4: "bit 7")
            (((s.vstart >> 8) & 0x1u) << 2) |     // bit 2: VSTART[8]
            (((s.vstop >> 8) & 0x1u) << 1) |      // bit 1: VSTOP[8]
            (s.hpos & 0x1u)                       // bit 0: HSTART[0]
        );
        sched.move(static_cast<copper::Register>(0x140 + channel * 8), pos);     // SPRxPOS
        sched.move(static_cast<copper::Register>(0x142 + channel * 8), ctl);     // SPRxCTL
        const uintptr addr = data.address(0).value;
        sched.move(static_cast<copper::Register>(0x120 + channel * 4), static_cast<u16>(addr >> 16));   // SPRxPTH
        sched.move(static_cast<copper::Register>(0x122 + channel * 4), static_cast<u16>(addr & 0xffff)); // SPRxPTL
    }

    SpriteConfig m_spr[8] {};
	eng::Block<eng::SpriteTag> m_data {};
};

} // namespace eng::graphics
