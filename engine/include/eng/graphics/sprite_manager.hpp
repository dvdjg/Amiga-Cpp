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
/// **Rutas de emisión.** El camino canónico de juego es `emit_placements_into`
/// (placements del compositor de actores, con rearme vertical del canal reutilizado y
/// paleta por franja). `emit_armed_into`/`arm_object` y `emit_template_into` son drivers
/// de bajo nivel para demos de la técnica; `emit_into` (un `WAIT` por `VSTART`) es legado
/// de demos antiguas y no debe usarse en código nuevo. La lógica de juego no llama al
/// `SpriteManager` directamente: compone con `SpriteScene` y aplica los placements.
/// Ver `docs/guides/roadmap/ROADMAP_JUEGO_SPRITES_BOBS.md` §4.1.
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
#include <eng/graphics/sprite_chain.hpp>
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
    u16 hpos = 0;                // X: HSTART absoluto en color-clocks (píxel = hpos − 0x81)
    u16 vstart = 0;              // línea vertical de inicio
    u16 vstop = 0;               // línea SIGUIENTE a la última visible (exclusiva; AHRM cap. 4)
    /// **Attached** al sprite anterior del par (bit 7 de `SPRxCTL`): 4 bits/píxel sobre
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

    /// Configura el canal `index` (0..7). Silenciosamente ignorado si `index >= 8`; un
    /// canal **habilitado sin DATA** (`ChipView` vacía) queda **deshabilitado**: la forma
    /// parcial «enabled sin DATA» no existe (el armado exige la DATA tipada; AGENTS §1.10).
    /// Si la DATA no cubre `height*width_words*2` words, el canal se deshabilita.
    /// \param index  canal de sprite (0..7).
    /// \param cfg    configuración del canal (posición/DATA/tamaño/paleta).
    void set(u8 index, const SpriteConfig& cfg) {
        if (index >= 8) return;
        m_spr[index] = cfg;
        // Contrato de tamaño: un sprite habilitado necesita
        // `height*width_words*2` words (DAT+DATB por línea). Si la DATA del span
        // no lo cubre, se descarta (no se emite) en lugar de leer fuera de rango.
        if (cfg.enabled) {
            if (cfg.data.empty()) {
                m_spr[index].enabled = false;
                return;
            }
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
            };
            // `attach` (15 colores) de la plantilla: el canal impar une su par y aporta
            // los bits 2-3 del índice (AHRM cap. 4, "Attached Sprites"). Sin esto, la
            // plantilla declararía el par pero la emisión no lo activaría.
            cfg.attach = tpl.attach;
            // Los sprites usan siempre COLOR16..31: un switch de paleta cambia los COLORxx
            // del PAR de canales (Color Bleed), así que dos plantillas que compartan canal
            // no pueden tener paletas distintas en líneas solapadas (ver `sprite-layer.md` §3).
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
                for (eng::usize c = 0; c < sw.colors.size(); ++c) {
                    sched.move(
                        copper::color_register(static_cast<u8>(sw.first + c)),
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

    /// **Arma la cadena DMA de un canal** (`sprite_chain.hpp`): escribe `SPRxPOS`/`SPRxCTL`
    /// de la **primera franja** (patrón validado de `arm_object`: demo 214/HOST-428) y
    /// `SPRxPTH/L` a la **DATA del primer bloque** (la cadena tras su cabecera POS/CTL).
    /// A partir de ahí el **walk** del DMA encadena solo: al consumir los datos del bloque
    /// llega a la `POS` del siguiente (`custom.cpp:12012-12023`; el DMA procesa cada
    /// cabecera con `dmastate == 0`, `custom.cpp:4078-4090`). Armar solo escribiendo el
    /// puntero **no basta**: una franja cuyo `VSTART` llega antes de que el walk consuma su
    /// cabecera desincroniza el canal (observado en la demo 219 con `VSTART` bajo el
    /// armado); por eso la primera franja va por registro y su cabecera del buffer se
    /// salta. Llamar **tras** un `wait_line_safe(arm_line)` compartido, con `arm_line`
    /// temprana (antes del primer `VSTART` del canal) y la cadena ya construida y con
    /// runway de ceros. La cadena se rearma cada frame (sin recarga el walk queda tras el
    /// terminador: antipatrón *Phantom Sprite*, `winuae/sprite-dma.md`).
    template <class Sched>
    static void arm_dma_chain(Sched& sched, u8 channel, ChipView<SpriteTag> chain, u16 x, u16 y,
                              u16 height, bool attach = false) {
        if (channel >= 8u || chain.size() < 4u || height == 0u) return;
        const u16 h = static_cast<u16>(height > kSpriteMaxHeightLines ? kSpriteMaxHeightLines
                                                                      : height);
        const SpritePosCtl e = encode_sprite_pos_ctl(x, y, h, attach);
        sched.move(copper::sprite_pos_register(channel), e.pos); // SPRxPOS
        sched.move(copper::sprite_ctl_register(channel), e.ctl); // SPRxCTL
        // PT a la DATA del primer bloque (cabecera POS/CTL de 2 words por delante).
        const uintptr addr = (chain.address(0) + 4).value;
        sched.move(copper::sprite_pointer_high_register(channel),
                   static_cast<u16>(addr >> 16)); // SPRxPTH
        sched.move(copper::sprite_pointer_low_register(channel),
                   static_cast<u16>(addr & 0xffff)); // SPRxPTL
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
    ///
    /// `extra` intercala **cambios de paleta por franja** (`SpritePaletteEvent`, 0-based:
    /// `colors[k]` → `COLOR[first+k]`, p. ej. los switches de una plantilla de actor) en su
    /// orden de línea con los rearmes. Sus líneas deben ser `> arm_line`.
    ///
    /// `chains` (F3, `sprite_chain.hpp`) activa la **emisión DMA encadenada** por canal: si
    /// `chains[c]` está construida, es del canal `c` y cubre **todos** los placements válidos
    /// de `c` (`block_count` coincide), el canal se arma con `arm_dma_chain` (2 MOVEs, sin
    /// `POS`/`CTL`: los pone cada bloque de la cadena) y **se saltan** sus rearmes por
    /// franja. El resto de canales sigue el camino actual. Una cadena que no cubra el canal
    /// (p. ej. bloque nuevo sin cadena) **degrada a rearme** sola — nunca se mezcla a medias.
    template <class Sched>
    static void emit_placements_into(Sched& sched, eng::Span<const HwSpritePlacement> placements,
                                     u16 arm_line = 32u,
                                     eng::Span<const SpritePaletteEvent> extra = {},
                                     eng::Span<const SpriteDmaChain> chains = {},
                                     bool clear_unused = false) {
        // 0) Cobertura de cadena por canal: `chains[c]` lista si cubre todos los placements
        //    válidos del canal `c` (mismo criterio de validez que el resto de la función).
        bool use_chain[8] {};
        if (!chains.empty()) {
            u8 valid_count[8] {};
            for (eng::u16 i = 0; i < placements.size(); ++i) {
                const HwSpritePlacement& p = placements[i];
                if (p.channel >= 8u || p.data.empty() || p.height == 0u ||
                    p.width_words == 0u) {
                    continue;
                }
                ++valid_count[p.channel];
            }
            for (u8 c = 0u; c < 8u; ++c) {
                if (chains.size() <= c) break;
                const SpriteDmaChain& ch = chains.data()[c];
                use_chain[c] = ch.used != 0u && ch.channel == c &&
                               ch.block_count == valid_count[c] && valid_count[c] != 0u;
            }
        }
        // 1) Primera config de cada canal: en la línea de armado temprana (un solo WAIT).
        //    Canal con cadena: `SPRxPT` a la cadena; el resto: `arm_object` (POS/CTL/PT).
        //    Con `clear_unused`, los canales sin placement de este frame se desactivan en
        //    la misma línea temprana (si no, conservarían el sprite del frame anterior).
        bool has[8] {};
        for (eng::u16 i = 0; i < placements.size(); ++i) {
            const HwSpritePlacement& p = placements[i];
            if (p.channel < 8u && !p.data.empty() && p.height != 0u && p.width_words != 0u) {
                has[p.channel] = true;
            }
        }
        bool seen[8] {};
        sched.wait_line(static_cast<u8>(arm_line & 0xffu)); // arm_line es temprana (<256)
        if (clear_unused) {
            for (u8 c = 0u; c < 8u; ++c) {
                if (has[c]) continue;
                sched.move(copper::sprite_ctl_register(c), 0u);
                sched.move(copper::sprite_pointer_high_register(c), 0u);
                sched.move(copper::sprite_pointer_low_register(c), 0u);
            }
        }
        for (eng::u16 i = 0; i < placements.size(); ++i) {
            const HwSpritePlacement& p = placements[i];
            if (p.channel >= 8u || p.data.empty() || p.height == 0u || p.width_words == 0u) {
                continue;
            }
            if (seen[p.channel]) continue;
            seen[p.channel] = true;
            if (use_chain[p.channel]) {
                arm_dma_chain(sched, p.channel, chains.data()[p.channel].storage, p.hpos,
                              p.vstart, p.height, p.attach);
            } else {
                arm_object(sched, p.channel, p.data, p.hpos, p.vstart, p.height, p.attach);
            }
        }
        // 2) Rearmes del multiplexado vertical y eventos de paleta, fusionados por línea
        //    (el Copper no puede esperar hacia atrás). Un canal servido por cadena no
        //    rearma: sus franjas ya van cocinadas en los bloques.
        bool marked[8] {};
        u16 ri = 0u;
        u16 r_idx = 0u;
        bool r_have = false;
        auto pull_rearm = [&]() {
            r_have = false;
            while (ri < placements.size()) {
                const HwSpritePlacement& p = placements[ri++];
                if (p.channel >= 8u || p.data.empty() || p.height == 0u ||
                    p.width_words == 0u) {
                    continue;
                }
                if (!marked[p.channel]) { // primera del canal: ya armada en (1)
                    marked[p.channel] = true;
                    continue;
                }
                if (use_chain[p.channel]) continue; // franja servida por la cadena
                if (p.vstart <= arm_line) continue; // por encima del armado: no visible
                r_idx = static_cast<u16>(ri - 1u);
                r_have = true;
                return;
            }
        };
        u16 ei = 0u;
        pull_rearm();
        while (r_have || ei < extra.size()) {
            const u16 rline = r_have ? placements[r_idx].vstart : 0xffffu;
            const u16 eline = (ei < extra.size()) ? extra.data()[ei].line : 0xffffu;
            if (rline <= eline) {
                const HwSpritePlacement& p = placements[r_idx];
                if (rline <= 255u) sched.wait_line(static_cast<u8>(rline));
                else sched.wait_line_safe(rline);
                arm_object(sched, p.channel, p.data, p.hpos, p.vstart, p.height, p.attach);
                pull_rearm();
            } else {
                const SpritePaletteEvent& ev = extra.data()[ei];
                if (ev.line > arm_line && !ev.colors.empty()) {
                    sched.wait_line_safe(ev.line);
                    // Switch 0-based: `colors[k]` -> COLOR[first+k] (como
                    // `SpriteManager::emit_template_into`).
                    for (eng::usize k = 0; k < ev.colors.size(); ++k) {
                        sched.move(copper::color_register(static_cast<u8>(ev.first + k)),
                                   ev.colors[k]);
                    }
                }
                ++ei;
            }
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
    /// La pareja `POS`/`CTL` la codifica `encode_sprite_pos_ctl` (`sprite_chain.hpp`), la
    /// **misma función** que usa la cadena DMA: una sola fuente del formato (AGENTS §1.7;
    /// layout AHRM cap. 5 verificado contra amiga-bootcamp `08_graphics/sprites.md` y
    /// `../WinUAE-DBG/custom.cpp:4044-4058`). `height` es el tramo visible (`VSTOP`
    /// exclusivo); un `vstop <= vstart` codifica `VSTOP = VSTART` (sprite sin filas).
    template <class Sched>
    static void emit_config(Sched& sched, u8 channel, const SpriteConfig& s,
                            ChipView<SpriteTag> data) {
        const u16 height = (s.vstop > s.vstart)
                               ? static_cast<u16>(s.vstop - s.vstart)
                               : 0u;
        const SpritePosCtl e = encode_sprite_pos_ctl(s.hpos, s.vstart, height, s.attach);
        sched.move(copper::sprite_pos_register(channel), e.pos); // SPRxPOS
        sched.move(copper::sprite_ctl_register(channel), e.ctl); // SPRxCTL
        const uintptr addr = data.address(0).value;
        sched.move(copper::sprite_pointer_high_register(channel), static_cast<u16>(addr >> 16));   // SPRxPTH
        sched.move(copper::sprite_pointer_low_register(channel), static_cast<u16>(addr & 0xffff)); // SPRxPTL
    }

    SpriteConfig m_spr[8] {};
	eng::Block<eng::SpriteTag> m_data {};
};

} // namespace eng::graphics
