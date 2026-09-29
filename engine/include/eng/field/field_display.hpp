#pragma once

/// \file field_display.hpp
/// **Cabecera de composición de un campo**: los registros de geometría/modo/paleta que definen un
/// tramo de pantalla, sin punteros de plano. Es la **fuente única** del header: la construyen por
/// igual el driver de scroll (`XlimitedDisplayComposer`) y la composición por bandas
/// (`scene::RasterLayout`), y se emite con `emit_field_display_header`.
///
/// No escribe punteros (`BPLxPT`): esos los aporta `emit_view_pointers` (field) o
/// `emit_band_pointers` (scene), que sí dependen del scroll. Así la geometría es de la composición
/// y el driver solo cambia lo que se mueve.

#include <eng/core/types/domains.hpp>
#include <eng/core/types/types.hpp>
#include <eng/graphics/copper/copper.hpp>

namespace eng::field {

/// Valores de la cabecera de un campo (todos explícitos: quien compone los conoce).
struct FieldHeaderConfig {
	eng::u16 dmacon = 0;
	eng::u16 bplcon0 = 0;
	eng::u16 bplcon1 = 0;
	eng::u16 bplcon2 = 0;
	bool set_bplcon4 = false; ///< emitir `BPLCON4` (p. ej. salir de EHB); si no, se omite
	eng::u16 bplcon4 = 0;
	eng::u16 bpl1mod = 0;
	eng::u16 bpl2mod = 0;
	eng::u16 diwstrt = 0;
	eng::u16 diwstop = 0;
	eng::u16 ddfstrt = 0;
	eng::u16 ddfstop = 0;
	eng::PaletteWords palette {}; ///< paleta a emitir desde `COLOR00`
};

/// Emite la cabecera de un campo en orden canónico: `DMACON` → `BPLCON0` → `BPLCON1` → `BPLCON2` →
/// [`BPLCON4`] → `BPLxMOD` → `DIW` → `DDF` → paleta. Los registros que no son geometría
/// (`BPLCON4`) van DESPUÉS de `BPLCON2` y antes de los módulos, que es el orden probado del
/// compositor (intercalarlos de otra forma pierde el último plano en conmutaciones, MI09).
template <class Sched>
inline void emit_field_display_header(Sched& sched, const FieldHeaderConfig& h) {
	sched.move(::eng::copper::Register::DMACON, h.dmacon);
	sched.move(::eng::copper::Register::BPLCON0, h.bplcon0);
	sched.move(::eng::copper::Register::BPLCON1, h.bplcon1);
	sched.move(::eng::copper::Register::BPLCON2, h.bplcon2);
	if (h.set_bplcon4) {
		sched.move(::eng::copper::Register::BPLCON4, h.bplcon4);
	}
	sched.move(::eng::copper::Register::BPL1MOD, h.bpl1mod);
	sched.move(::eng::copper::Register::BPL2MOD, h.bpl2mod);
	sched.move(::eng::copper::Register::DIWSTRT, h.diwstrt);
	sched.move(::eng::copper::Register::DIWSTOP, h.diwstop);
	sched.move(::eng::copper::Register::DDFSTRT, h.ddfstrt);
	sched.move(::eng::copper::Register::DDFSTOP, h.ddfstop);
	for (eng::u8 i = 0u; i < h.palette.size(); ++i) {
		sched.move(::eng::copper::color_register(i), h.palette[i]);
	}
}

} // namespace eng::field
