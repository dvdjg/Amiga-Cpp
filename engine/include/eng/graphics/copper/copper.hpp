#pragma once

/// \file copper.hpp
/// Constructor didactico de copperlists OCS, con **modelo tipado del hardware**.
///
/// El Copper ejecuta una lista de instrucciones en Chip RAM. Cada instruccion ocupa
/// dos words de 16 bits:
///
/// - `MOVE`: word0 = offset de registro custom, word1 = valor.
/// - `WAIT`: word0 = posicion vertical/horizontal, word1 = mascara/comparacion.
/// - `SKIP`: igual que WAIT pero con el bit 0 de la **segunda** word a 1.
/// - fin de lista: `0xffff, 0xfffe`.
///
/// El modelo tipado impide representar estados incorrectos (ver
/// `docs/engine/architecture/COHERENCIA_DE_SETUP.md`):
///
/// - `MoveSlot`/`WaitSlot` distinguen **que palabra se parchea**: parchear el dato de un
///   MOVE con el indice de un WAIT (descuadre clasico de copperlist) **no compila**.
/// - `RegisterOffset` obliga a que un destino custom salga de un constructor del dominio
///   (`Register`, `color_register`, `bitmap_pointer_*`, `sprite_*_register`): inventar un
///   offset exige escribir `RegisterOffset{...}` de forma explicita y greppable.
/// - `copper_register_access()` clasifica un offset segun el mapa real del chipset
///   (fuente: WinUAE-DBG `identify.cpp` `custd` y `custom.cpp:7316-7613`); los validadores
///   de `eng::setup` lo usan para rechazar MOVEs a registros inexistentes o de solo lectura.
/// - `ColumnPatch` describe un relleno con paso (blitter o CPU) sobre la lista y su `stride`
///   se valida contra las instrucciones emitidas.
///
/// Esta clase no intenta ser todavia un scheduler completo. Es el primer ladrillo:
/// una forma segura de construir listas pequenas sin que cada demo escriba words
/// magicas a mano. Mas adelante el `CopperScheduler` compondra contribuciones de
/// drivers, efectos UAF y patches runtime.
///
/// ```text
///   instrucción de Copper en Chip RAM (2 words big-endian, 4 bytes)
///   ┌───────────────┬───────────────┐   MOVE: word0 = offset de registro custom, word1 = valor
///   │     word 0    │     word 1    │   WAIT: word0 = posición v/h,           word1 = máscara/comparación
///   └───────────────┴───────────────┘   FIN : 0xffff, 0xfffe (termina la lista)
/// ```
///
/// Codificacion del comparador (AHRM 3.ª, cap. 2, Table 2-2 "Copper Instruction Summary",
/// lineas 1043-1071 del `.cat.md`) contrastada con WinUAE-DBG (`custom.cpp:9225-9262`):
/// el bit 0 de la primera word distingue MOVE (0) de WAIT/SKIP (1); el bit 0 de la segunda
/// distingue WAIT (0) de SKIP (1); `VM` = bits 15-8 (con el bit 15 = BFD, "blitter finished
/// disable"), `HM` = bits 7-1, y el bit 7 del comparador V no se puede enmascarar
/// (`custom.cpp:4427`, `:9255`).

#include <eng/core/types/domains.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/hw/info.hpp>
#include <eng/memory/arena.hpp>

namespace eng::copper {

/// Offsets de registros custom soportados por el vocabulario tipado.
///
/// Los valores son offsets desde la base custom `$dff000` y coinciden con el mapa del
/// chipset (AHRM 3.ª Appendix B; WinUAE-DBG `identify.cpp` tabla `custd`). El nombre
/// `BPLCON3` es el del AHRM para `$106`; `BPLCON4` es el registro **AGA** `$10c`
/// (identify.cpp:268/271) — confundirlos cablea el registro equivocado.
enum class Register : u16 {
	COPCON = 0x02e,
	COP1LCH = 0x080,
	COP1LCL = 0x082,
	COP2LCH = 0x084,
	COP2LCL = 0x086,
	COPJMP1 = 0x088,
	COPJMP2 = 0x08a,
	DIWSTRT = 0x08e,
	DIWSTOP = 0x090,
	DDFSTRT = 0x092,
	DDFSTOP = 0x094,
	DMACON = 0x096,
	BPL1PTH = 0x0e0,
	BPL1PTL = 0x0e2,
	BPL2PTH = 0x0e4,
	BPL2PTL = 0x0e6,
	BPL3PTH = 0x0e8,
	BPL3PTL = 0x0ea,
	BPL4PTH = 0x0ec,
	BPL4PTL = 0x0ee,
	BPL5PTH = 0x0f0,
	BPL5PTL = 0x0f2,
	BPL6PTH = 0x0f4,
	BPL6PTL = 0x0f6,
	BPLCON0 = 0x100,
	BPLCON1 = 0x102,
	BPLCON2 = 0x104,
	BPLCON3 = 0x106, ///< ECS/AGA: enhanced features (ENBPLCN3 bit 0, KILLEHB bit 9; AHRM C)
	BPLCON4 = 0x10c, ///< AGA: mascaras de bitplanes/sprites
	BPL1MOD = 0x108,
	BPL2MOD = 0x10a,
	BLTCON0 = 0x040,
	BLTCON1 = 0x042,
	BLTAFWM = 0x044,
	BLTALWM = 0x046,
	BLTCPTH = 0x048,
	BLTCPTL = 0x04a,
	BLTBPTH = 0x04c,
	BLTBPTL = 0x04e,
	BLTAPTH = 0x050,
	BLTAPTL = 0x052,
	BLTDPTH = 0x054,
	BLTDPTL = 0x056,
	BLTSIZE = 0x058,
	BLTCMOD = 0x060,
	BLTBMOD = 0x062,
	BLTAMOD = 0x064,
	BLTDMOD = 0x066,
	BLTCDAT = 0x070,
	BLTBDAT = 0x072,
	BLTADAT = 0x074,
	COLOR00 = 0x180,
	COLOR08 = 0x190,
	COLOR14 = 0x19c,
	COLOR15 = 0x19e,
};

/// Flags basicos de DMACON.
///
/// DMACON usa el bit 15 como selector set/clear: si esta a 1, los bits indicados
/// se activan; si esta a 0, se limpian. Aqui solo exponemos lo que necesita la demo
/// inicial del Copper.
enum DmaControl : u16 {
	DmaSetClear = 0x8000,
	DmaBlitterPriority = 0x0400, // BLTPRI/BLITHOG: el Blitter no cede slots a la CPU
	DmaMaster = 0x0200,
	DmaBitplane = 0x0100,
	DmaCopper = 0x0080,
	DmaBlitter = 0x0040,
	DmaSprite = 0x0020,   // SPREN: habilita el DMA de todos los sprites
	DmaDisk = 0x0010,
};

/// **Destino de un MOVE ya validado por el dominio**: offset custom par (`0x000`-`0x1fe`).
///
/// Se construye con los constructores del dominio (`Register`, `color_register`,
/// `bitplane_pointer_high_register`, `sprite_pos_register`…) en vez de inventar numeros:
/// un `RegisterOffset{0x997u}` explicito y citado sigue siendo posible (escape para
/// registros fuera del vocabulario), pero queda greppable y el validador de setup
/// (`eng::setup::check_copper_list`) lo rechaza si no existe en el chipset.
struct RegisterOffset {
	u16 offset = 0; ///< offset desde `$dff000` (par, rango 0x000-0x1fe)

	constexpr operator u16() const { return offset; }
};

/// Devuelve el offset de un registro COLORxx.
/// \param index  índice de color (0..31).
/// \return offset del registro `COLORxx` (base `COLOR00` + `index*2`).
constexpr RegisterOffset color_register(u8 index) {
	return RegisterOffset {static_cast<u16>(static_cast<u16>(Register::COLOR00) + static_cast<u16>(index & 31u) * 2u)};
}

/// Devuelve el offset del word alto del puntero de un bitplane.
///
/// Los punteros BPLxPT son pares de registros `PTH/PTL`. El Copper solo puede
/// escribir words, asi que cargar un puntero requiere dos MOVEs. `plane` usa base
/// cero para encajar con arrays C++: 0 = BPL1, 5 = BPL6.
/// \param plane  índice de plano base-cero (0 = BPL1).
/// \return offset del registro `BPLxPTH`.
constexpr RegisterOffset bitplane_pointer_high_register(u8 plane) {
	return RegisterOffset {static_cast<u16>(static_cast<u16>(Register::BPL1PTH) + static_cast<u16>(plane & 7u) * 4u)};
}

/// Devuelve el offset del word bajo del puntero de un bitplane.
/// \param plane  índice de plano base-cero (0 = BPL1).
/// \return offset del registro `BPLxPTL`.
constexpr RegisterOffset bitplane_pointer_low_register(u8 plane) {
	return RegisterOffset {static_cast<u16>(static_cast<u16>(Register::BPL1PTL) + static_cast<u16>(plane & 7u) * 4u)};
}

/// Offset del puntero de DATA (word alto) del canal de sprite `ch` (0 = SPR0).
/// Offsets: `SPRxPTH = 0x120 + ch*4`, `SPRxPTL = 0x122 + ch*4` (AHRM, "Sprite DMA channels").
constexpr RegisterOffset sprite_pointer_high_register(u8 ch) {
	return RegisterOffset {static_cast<u16>(0x120u + static_cast<u16>(ch & 7u) * 4u)};
}

/// Offset del puntero de DATA (word bajo) del canal de sprite `ch` (0 = SPR0).
constexpr RegisterOffset sprite_pointer_low_register(u8 ch) {
	return RegisterOffset {static_cast<u16>(0x122u + static_cast<u16>(ch & 7u) * 4u)};
}

/// Offset de `SPRxPOS` del canal `ch` (stride 8: `SPR0POS=0x140`, `SPR1POS=0x148`…).
constexpr RegisterOffset sprite_pos_register(u8 ch) {
	return RegisterOffset {static_cast<u16>(0x140u + static_cast<u16>(ch & 7u) * 8u)};
}

/// Offset de `SPRxCTL` del canal `ch` (stride 8: `SPR0CTL=0x142`, `SPR1CTL=0x14a`…).
constexpr RegisterOffset sprite_ctl_register(u8 ch) {
	return RegisterOffset {static_cast<u16>(0x142u + static_cast<u16>(ch & 7u) * 8u)};
}

/// Offset de `SPRxDATA` del canal `ch` (stride 8: `SPR0DATA=0x144`…).
constexpr RegisterOffset sprite_data_register(u8 ch) {
	return RegisterOffset {static_cast<u16>(0x144u + static_cast<u16>(ch & 7u) * 8u)};
}

/// Offset de `SPRxDATB` del canal `ch` (stride 8: `SPR0DATB=0x146`…).
constexpr RegisterOffset sprite_datb_register(u8 ch) {
	return RegisterOffset {static_cast<u16>(0x146u + static_cast<u16>(ch & 7u) * 8u)};
}

// =====================================================================================
// Mapa de acceso de los registros custom para escrituras del Copper
// =====================================================================================

/// Clase de acceso de un offset custom para una **escritura**.
///
/// Fuente: WinUAE-DBG `identify.cpp` (tabla `custd`, lineas 135-395: `CD_WO` = write-only,
/// `CD_ECS_AGNUS`/`CD_ECS_DENISE`/`CD_AGA`, flags 0 = registro de lectura) contrastada con
/// los `case` de `custom_wput_agnus` (`custom.cpp:7316-7613`) y con su `default`
/// ("writing to read-only register causes read access", `custom.cpp:7603-7611`).
enum class RegisterAccess : u8 {
	Writable, ///< existe en OCS y la escritura tiene efecto
	EcsOnly,  ///< existe solo con Agnus/Denise ECS (A500+ en adelante)
	AgaOnly,  ///< existe solo con AGA (A1200)
	ReadOnly, ///< registro de lectura: la escritura se ignora (causa un acceso de lectura)
	Reserved, ///< hueco del mapa / sin funcion
};

/// Clasifica un offset custom (0x000-0x1fe) para escritura. Un offset impar se normaliza
/// igual que el hardware (`& 0x1fe`; el bus conecta `A9-A1`).
/// \param offset  offset desde `$dff000`.
/// \return clase de acceso del registro en ese offset.
[[nodiscard]] constexpr RegisterAccess copper_register_access(u16 offset) noexcept {
	const u16 o = static_cast<u16>(offset & 0x1feu);
	switch (o) {
		// Registros de lectura (lado Agnus/Denise de lectura). Escribir en ellos no hace nada.
		case 0x000: // BLTDDAT (lectura anticipada del Blitter)
		case 0x002: // DMACONR
		case 0x004: // VPOSR
		case 0x006: // VHPOSR
		case 0x008: // DSKDATR
		case 0x00a: // JOY0DAT
		case 0x00c: // JOY1DAT
		case 0x010: // ADKCONR
		case 0x012: // POT0DAT
		case 0x014: // POT1DAT
		case 0x016: // POTGOR
		case 0x018: // SERDATR
		case 0x01a: // DSKBYTR
		case 0x01c: // INTENAR
		case 0x01e: // INTREQR
		case 0x07c: // LISAID (ECS: revision de Denise)
		case 0x08c: // COPINS (lectura de la instruccion del Copper)
			return RegisterAccess::ReadOnly;
		// Bloque Agnus bajo (disco, beam, serial, potenciómetros, reloj de joystick).
		case 0x00e: // CLXDAT (escribir lo limpia)
		case 0x020: case 0x022: case 0x024: case 0x026: case 0x028:
		case 0x02a: case 0x02c: case 0x02e:
		case 0x030: case 0x032: case 0x034: case 0x036:
		case 0x038: case 0x03a: case 0x03c: case 0x03e: // strobes A1000
		// Blitter (registros < $40: requieren CDANG en COPCON; el takeover del engine lo
		// activa, `amiga.cpp` `takeover_display` + `winuae/copper.md`).
		case 0x040: case 0x042: case 0x044: case 0x046: case 0x048: case 0x04a:
		case 0x04c: case 0x04e: case 0x050: case 0x052: case 0x054: case 0x056:
		case 0x058: case 0x05a: case 0x05c: case 0x05e:
		case 0x060: case 0x062: case 0x064: case 0x066:
		case 0x070: case 0x072: case 0x074:
		case 0x07e: // DSKSYNC
		// Copper y display.
		case 0x080: case 0x082: case 0x084: case 0x086: case 0x088: case 0x08a:
		case 0x08e: case 0x090: case 0x092: case 0x094: case 0x096:
		case 0x09a: case 0x09c: case 0x09e:
		// Audio (4 canales).
		case 0x0a0: case 0x0a2: case 0x0a4: case 0x0a6: case 0x0a8: case 0x0aa:
		case 0x0b0: case 0x0b2: case 0x0b4: case 0x0b6: case 0x0b8: case 0x0ba:
		case 0x0c0: case 0x0c2: case 0x0c4: case 0x0c6: case 0x0c8: case 0x0ca:
		case 0x0d0: case 0x0d2: case 0x0d4: case 0x0d6: case 0x0d8: case 0x0da:
		// Punteros de bitplane 1-6 y control.
		case 0x0e0: case 0x0e2: case 0x0e4: case 0x0e6: case 0x0e8: case 0x0ea:
		case 0x0ec: case 0x0ee: case 0x0f0: case 0x0f2: case 0x0f4: case 0x0f6:
		case 0x100: case 0x102: case 0x104:
		case 0x108: case 0x10a:
		case 0x110: case 0x112: case 0x114: case 0x116: case 0x118: case 0x11a:
		// Sprites: punteros, POS/CTL y DATA/DATB de los 8 canales.
		case 0x120: case 0x122: case 0x124: case 0x126: case 0x128: case 0x12a:
		case 0x12c: case 0x12e: case 0x130: case 0x132: case 0x134: case 0x136:
		case 0x138: case 0x13a: case 0x13c: case 0x13e:
		case 0x140: case 0x142: case 0x144: case 0x146: case 0x148: case 0x14a:
		case 0x14c: case 0x14e: case 0x150: case 0x152: case 0x154: case 0x156:
		case 0x158: case 0x15a: case 0x15c: case 0x15e: case 0x160: case 0x162:
		case 0x164: case 0x166: case 0x168: case 0x16a: case 0x16c: case 0x16e:
		case 0x170: case 0x172: case 0x174: case 0x176: case 0x178: case 0x17a:
		case 0x17c: case 0x17e:
		case 0x1fe: // NULL (escritura de cierre reconocida por el chipset)
			return RegisterAccess::Writable;
		case 0x106: // BPLCON3 (ECS Denise)
		case 0x098: // CLXCON (ECS)
		case 0x1c0: case 0x1c2: case 0x1c4: case 0x1c6:
		case 0x1c8: case 0x1ca: case 0x1cc: case 0x1ce:
		case 0x1d0: case 0x1d2: case 0x1d4: case 0x1d6: case 0x1d8:
		case 0x1dc: case 0x1de: case 0x1e0: case 0x1e2: case 0x1e4:
		case 0x1e6: case 0x1e8: case 0x1ea: case 0x1ec: case 0x1ee:
			return RegisterAccess::EcsOnly;
		case 0x1da: // HHPOSR (lectura ECS)
			return RegisterAccess::ReadOnly;
		case 0x0f8: case 0x0fa: case 0x0fc: case 0x0fe: // BPL7-8PT
		case 0x10c: // BPLCON4
		case 0x10e: // CLXCON2
		case 0x11c: case 0x11e: // BPL7-8DAT
		case 0x1fc: // FMODE
			return RegisterAccess::AgaOnly;
		default:
			// COLOR00-31 y el resto de registros no listados con funcion: huecos del mapa.
			break;
	}
	if (o >= 0x180u && o <= 0x1beu) {
		return RegisterAccess::Writable; // COLOR00..COLOR31
	}
	return RegisterAccess::Reserved;
}

/// `true` si un MOVE del Copper puede escribir en `offset` con el chipset dado.
[[nodiscard]] constexpr bool copper_register_writable(u16 offset, eng::hw::Chipset chipset) noexcept {
	const RegisterAccess a = copper_register_access(offset);
	switch (a) {
		case RegisterAccess::Writable:
			return true;
		case RegisterAccess::EcsOnly:
			return chipset == eng::hw::Chipset::ECS || chipset == eng::hw::Chipset::AGA;
		case RegisterAccess::AgaOnly:
			return chipset == eng::hw::Chipset::AGA;
		case RegisterAccess::ReadOnly:
		case RegisterAccess::Reserved:
			return false;
	}
	return false;
}

/// Modo estricto de Copper (`-DENG_COPPER_STRICT=1` en el build, p. ej. vía `build.args`):
/// `Plan::end_frame()` deja de publicar una lista cuyo informe tenga desborde de Timeline
/// visible o intenciones sin manejar; el fallo llega por el camino de error ya existente
/// (`end_frame` devuelve `false`) en vez de verse como un defecto sutil en pantalla.
#if defined(ENG_COPPER_STRICT)
inline constexpr bool strict_mode = true;
#else
inline constexpr bool strict_mode = false;
#endif

// =====================================================================================
// Slots tipados: la direccion de una instruccion emitida
// =====================================================================================

/// **Referencia a un MOVE emitido** (indice de su palabra de OPCODE dentro de la lista).
///
/// Es el unico tipo aceptado por los parches de dato (`patch_data`): pasar el indice de un
/// WAIT/SKIP (otro `u16`) no compila; convertirlo a `MoveSlot` exige escribirlo de forma
/// explicita. `operator u16` existe solo para leer el indice (comparaciones, spans), nunca
/// para construir uno.
struct MoveSlot {
	u16 index = 0xffffu; ///< word0 del MOVE; el dato va en `index + 1`

	/// `true` si el slot referencia una instruccion (no es el sentinela `0xffff`).
	[[nodiscard]] constexpr bool valid() const { return index != 0xffffu; }
	/// Indice de la palabra de OPCODE (registro destino) dentro de la lista.
	[[nodiscard]] constexpr u16 opcode_word() const { return index; }
	/// Indice de la palabra de DATO dentro de la lista (la que se parchea por frame).
	[[nodiscard]] constexpr u16 data_word() const { return static_cast<u16>(index + 1u); }
	/// Siguiente instruccion emitida inmediatamente despues (2 words): es el invariante que
	/// usan los pares PTH/PTL emitidos con dos `move_at` consecutivos.
	[[nodiscard]] constexpr MoveSlot next_instruction() const {
		return MoveSlot {static_cast<u16>(index + 2u)};
	}
	constexpr operator u16() const { return index; }
};

/// **Referencia a un WAIT/SKIP emitido** (indice de su palabra de OPCODE). Solo lo aceptan
/// los parches de espera (`patch_wait`); mezclarlo con un `MoveSlot` no compila.
struct WaitSlot {
	u16 index = 0xffffu; ///< word0 del WAIT/SKIP; la mascara va en `index + 1`

	/// `true` si el slot referencia una instruccion (no es el sentinela `0xffff`).
	[[nodiscard]] constexpr bool valid() const { return index != 0xffffu; }
	/// Indice de la palabra de OPCODE (posicion v/h) dentro de la lista.
	[[nodiscard]] constexpr u16 opcode_word() const { return index; }
	/// Indice de la palabra de MASCARA dentro de la lista.
	[[nodiscard]] constexpr u16 mask_word() const { return static_cast<u16>(index + 1u); }
	constexpr operator u16() const { return index; }
};

/// **Relleno con paso (stride) sobre instrucciones MOVE de la lista**: es el contrato del
/// blitter de columnas (`blitter_fill_words_strided`) y del copper-chunky (`p[0]=c; p+=2`).
///
/// El `stride` se valida contra las instrucciones realmente emitidas
/// (`eng::setup::check_copper_column`): escribir sobre `WAIT`s "por un modulo mal puesto"
/// deja de ser un descuadre silencioso. `stride_words = 2` es el patron de MOVEs
/// consecutivos (`[reg, dato, reg, dato, …]`), el unico que salta de dato en dato.
struct ColumnPatch {
	MoveSlot first {};       ///< primer MOVE cuyo word de DATO se rellena
	u16 stride_words = 2u;   ///< palabras entre datos consecutivos (par, >= 2)
	u16 rows = 0u;           ///< numero de datos (filas) a escribir

	/// `true` si la forma es representable: primer MOVE valido, paso par >= 2 y alguna fila.
	[[nodiscard]] constexpr bool valid() const {
		return first.valid() && (stride_words >= 2u) && ((stride_words & 1u) == 0u) && rows != 0u;
	}
	/// Indice del word de DATO de la fila `r` (`r < rows`).
	[[nodiscard]] constexpr u16 data_word(u16 r) const {
		return static_cast<u16>(first.data_word() + r * stride_words);
	}
};

/// Construye un `ColumnPatch` con el paso por defecto de MOVEs consecutivos (2 words).
/// \param first  primer MOVE del grupo (emitido con `move_at`).
/// \param rows   numero de filas.
/// \return el descriptor (`valid()` es false si la forma no es representable).
[[nodiscard]] constexpr ColumnPatch column_patch(MoveSlot first, u16 rows) {
	return ColumnPatch {first, 2u, rows};
}

// =====================================================================================
// Helpers libres para parchear un bloque ya emitido (activo/inactivo)
// =====================================================================================

/// Escribe el word de DATO del MOVE `slot` en `words` (bloque activo/inactivo del doble
/// buffer). **Sin comprobacion de rango**: el slot es un invariante de la emision (salio de
/// `move_at` sobre la misma lista); el chequeo global es `eng::setup::check_copper_list`.
inline void patch_data(eng::Span<u16> words, MoveSlot slot, u16 value) {
	words[slot.data_word()] = value;
}

/// Escribe el word de REGISTRO (destino) del MOVE `slot` en `words`.
inline void patch_move_reg(eng::Span<u16> words, MoveSlot slot, RegisterOffset reg) {
	words[slot.opcode_word()] = reg.offset;
}

/// Escribe un puntero de 32 bits repartido en el par de MOVEs `slot` (alto) y
/// `slot.next_instruction()` (bajo) en `words`.
inline void patch_move32(eng::Span<u16> words, MoveSlot slot, eng::u32 address) {
	words[slot.data_word()] = static_cast<u16>(address & 0xffffu);
	words[slot.next_instruction().data_word()] = static_cast<u16>(address >> 16u);
}

/// Escribe la **linea** (word0) del WAIT `slot` en `words`. Solo posiciones 0..255: por
/// encima la lista necesita el par de overflow (`wait_line_pal`) y hay que reemitirla.
inline void patch_wait(eng::Span<u16> words, WaitSlot slot, u16 vpos, u8 hpos = 1u) {
	if (vpos <= 255u) {
		words[slot.opcode_word()] =
			static_cast<u16>((vpos << 8u) | (static_cast<u16>(hpos) & 0xfeu) | 1u);
	}
}

/// Codifica la primera word de un WAIT sencillo.
///
/// Para las demos tempranas usamos `h = 1`, igual que los ejemplos clasicos:
/// `0x4001` espera aproximadamente a la linea `$40`.
/// \param vpos  línea vertical (byte bajo del comparador de WAIT).
/// \param hpos  posición horizontal (def. 1).
/// \return la primera word del WAIT (`(vpos<<8) | (hpos & 0xfe) | 1`).
constexpr u16 wait_word(u8 vpos, u8 hpos = 1) {
	return static_cast<u16>((static_cast<u16>(vpos) << 8) | (hpos & 0xfe) | 1u);
}

/// Builder lineal para copperlists.
///
/// No reserva memoria: escribe sobre un `MemoryBlock` que debe vivir en Chip RAM.
/// Esto mantiene visible la regla mas importante del Copper: Agnus solo puede leer
/// la lista desde Chip RAM.
///
/// Los emisores devuelven **slots tipados** (`MoveSlot`/`WaitSlot`) y los parches solo
/// aceptan el tipo correspondiente: el descuadre "parchear el opcode en vez del dato"
/// (o al reves) es un error de compilacion, no un efecto raro en pantalla.
class ListBuilder {
public:
	constexpr ListBuilder() = default;

	explicit ListBuilder(MemoryBlock block)
		: m_words(static_cast<u16*>(block.data)),
		  m_capacity_words(block.size / sizeof(u16)),
		  m_ok(block.valid() && block.kind == MemoryKind::Chip) {}

	/// Construye desde una reserva tipada de copperlist (`Block<CopperTag>`): el
	/// dominio ya viene etiquetado y solo se valida que viva en Chip RAM. **No** toma
	/// propiedad del bloque: copia la vista (el `Block` es movible, no copiable).
	explicit ListBuilder(const eng::Block<eng::CopperTag>& block)
		: m_words(reinterpret_cast<u16*>(block.view.data())),
		  m_capacity_words(static_cast<u16>(block.view.size() / sizeof(u16))),
		  m_ok(block.valid() && block.kind == MemoryKind::Chip) {}

	explicit ListBuilder(const eng::Block<eng::CopperTag, eng::MemoryKind::Chip>& block)
		: m_words(reinterpret_cast<u16*>(block.view.data())),
		  m_capacity_words(static_cast<u16>(block.view.size() / sizeof(u16))),
		  m_ok(block.valid()) {}

	/// Escribe un MOVE Copper a un registro del enum (destino validado por construccion).
	void move(Register reg, u16 value) {
		move(RegisterOffset {static_cast<u16>(reg)}, value);
	}

	/// Escribe un MOVE Copper con un destino del **dominio** (`RegisterOffset`): el offset
	/// sale de `Register`, `color_register`, `bitplane_pointer_*` o `sprite_*_register`.
	/// Un offset inventado exige `RegisterOffset{...}` explicito (greppable y validable).
	/// Camino caliente (emisión por línea): `always_inline` para que el estado del builder
	/// viva en registro, no en memoria.
	__attribute__((always_inline)) inline void move(RegisterOffset offset, u16 value) {
		write_pair(offset.offset, value);
	}

	/// Igual que `move` pero escribe el par (registro+dato) en **una sola** operación de
	/// 32 bits. En chip RAM (copperlists de cientos de palabras por frame) la contienda por
	/// el bus hace que 1 store de 32 bits cueste bastante menos que 2 de 16. `m_used_words`
	/// es siempre par, así que el destino está alineado a 4 bytes.
	__attribute__((always_inline)) inline void move32(RegisterOffset offset, u16 value) {
		const u16 used = m_used_words;
		if (m_capacity_words < 2u || used > m_capacity_words - 2u) {
			m_ok = false;
			m_overflow_sent = true;
			return;
		}
		*reinterpret_cast<u32*>(m_words + used) =
			(static_cast<u32>(offset.offset) << 16) | static_cast<u32>(value);
		m_used_words = static_cast<u16>(used + 2u);
	}

	/// Escribe los dos MOVEs necesarios para cargar un puntero de bitplane.
	///
	/// Esta funcion no valida que la direccion apunte a Chip RAM: esa garantia debe
	/// venir de la arena usada por el driver. Aqui solo codificamos el formato que
	/// espera Agnus en BPLxPTH/BPLxPTL.
	void move_bitplane_pointer(u8 plane, eng::Address<eng::MemoryKind::Chip> address) {
		const uintptr raw = address.value;
		move(bitplane_pointer_high_register(plane), static_cast<u16>(raw >> 16));
		move(bitplane_pointer_low_register(plane), static_cast<u16>(raw & 0xffffu));
	}

	/// Espera a una linea de raster con mascara estandar (solo V). Camino caliente.
	///
	/// Usa la mascara `0xff00`: compara los 8 bits verticales e ignora la posicion
	/// horizontal. Es la espera mas comun (cambios al principio de linea).
	__attribute__((always_inline)) inline WaitSlot wait_line(u8 vpos) {
		const u16 index = m_used_words;
		write_pair(wait_word(vpos), 0xff00);
		return WaitSlot {index};
	}

	/// Espera a una linea PAL completa (0..311) manejando el overflow del contador V.
	///
	/// Port de `CopWaitSafe` de demoscene-repo (libgfx, `include/copper.h`). El
	/// contador vertical del Copper (VPOS) tiene 9 bits pero el campo VP del WAIT solo
	/// 8: al esperar una linea >= 256 el byte bajo "wraps". La tecnica clasica es
	/// insertar ANTES el par `0xffdf/0xfffe` (esperar al final del campo V=255 justo
	/// antes de que VPOS haga wrap), y luego el WAIT morir con el byte bajo de `vpos`.
	/// El par de overflow solo se emite la primera vez por lista (`m_overflow_sent`).
	/// Limitacion documentada: el comparador del emulador tampoco compara V8
	/// (`custom.cpp:4427`), asi que la precision por encima de la 255 no esta garantizada
	/// (ver `OLEADA1_LIBGFX_INVENTARIO.md` §CopWaitSafe).
	WaitSlot wait_line_pal(u16 vpos) {
		if (vpos <= 255u) {
			return wait_line(static_cast<u8>(vpos & 0xffu));
		}
		if (!m_overflow_sent) {
			m_overflow_sent = true;
			// Espera al final de la linea 255: fuerza el wrap del bit 8 de VPOS.
			write_pair(0xffdf, 0xfffe);
		}
		const u16 index = m_used_words;
		write_pair(wait_word(static_cast<u8>(vpos & 0xffu)), 0xfffe);
		return WaitSlot {index};
	}

	/// Espera a una posicion concreta de la linea (V y H).
	///
	/// Usa la mascara `0xfffe`: compara tambien los bits horizontales, lo que permite
	/// "copper bars" que cambian un registro a mitad de scanline. `hpos` debe ser par
	/// (los ejemplos clasicos usan 1 con la mascara estandar; para H real se codifica
	/// el valor par que compara).
	WaitSlot wait_position(u8 vpos, u8 hpos) {
		const u16 index = m_used_words;
		write_pair(wait_word(vpos, static_cast<u8>(hpos & 0xfe)), 0xfffe);
		return WaitSlot {index};
	}

	/// Espera a una POSICION (V y H) de una linea PAL completa (0..311), manejando el
	/// overflow del contador V como `wait_line_pal` pero conservando la comparacion
	/// horizontal. Es el port exacto de `CopWaitSafe` con `X(...)`: se usa para los
	/// cambios de paleta por scanline de `bobs3d`, que esperan al final de la linea
	/// anterior (`X(288)`) y al principio de la actual (`X(0)`). `hpos` va en el mismo
	/// formato que `wait_position` (color-clock, se enmascara a par).
	WaitSlot wait_position_pal(u16 vpos, u8 hpos) {
		if (vpos <= 255u) {
			return wait_position(static_cast<u8>(vpos & 0xffu), hpos);
		}
		if (!m_overflow_sent) {
			m_overflow_sent = true;
			write_pair(0xffdf, 0xfffe);
		}
		const u16 index = m_used_words;
		write_pair(wait_word(static_cast<u8>(vpos & 0xffu), static_cast<u8>(hpos & 0xfe)), 0xfffe);
		return WaitSlot {index};
	}

	/// Finaliza la lista. El Copper se detiene en este par especial.
	void end() {
		write_pair(0xffff, 0xfffe);
		m_overflow_sent = false;
	}

	// --- Extensiones para efectos tipo "copper chunky" (p. ej. plasma) ----------

	/// Emite un MOVE y devuelve el slot de su instruccion (word0), para poder parchear
	/// luego su word de valor con `patch_data` (equivale a guardar el `CopInsT*` del
	/// original y hacer `CopSetColor` por frame).
	MoveSlot move_at(RegisterOffset offset, u16 value) {
		const u16 index = m_used_words;
		write_pair(offset.offset, value);
		return MoveSlot {index};
	}
	MoveSlot move_at(Register reg, u16 value) { return move_at(RegisterOffset {static_cast<u16>(reg)}, value); }

	/// Parchea el word de valor de un MOVE emitido antes (slot devuelto por `move_at`).
	__attribute__((always_inline)) inline void patch_data(MoveSlot slot, u16 value) {
		if (m_ok && static_cast<u32>(slot.data_word()) < m_used_words) {
			m_words[slot.data_word()] = value;
		}
	}

	/// Parchea la **linea** (word0) de un WAIT ya emitido (slot devuelto por `wait_raw`,
	/// `wait_line`, `skip`...). Deja la mascara (word1). `vpos` debe caer en 0..255: por
	/// encima, la lista necesita el par de overflow (`wait_line_pal`) y hay que reemitirla;
	/// en ese caso el builder queda marcado como no valido (`ok() == false`) en vez de
	/// escribir un byte bajo que la espera no cumpliria.
	__attribute__((always_inline)) inline void patch_wait(WaitSlot slot, u16 vpos, u8 hpos = 1) {
		if (m_ok && static_cast<u32>(slot.opcode_word()) < m_used_words) {
			if (vpos > 255u) {
				m_ok = false; // la estructura no tiene overflow: el parche seria incorrecto
				return;
			}
			m_words[slot.opcode_word()] = wait_word(static_cast<u8>(vpos), hpos);
		}
	}

	/// Parchea el **registro** (word0) de un MOVE ya emitido: permite que un slot cambie
	/// de destino (p. ej. escribir COLOR01..06 o COLOR09..13 segun la fase del efecto).
	__attribute__((always_inline)) inline void patch_move_reg(MoveSlot slot, RegisterOffset reg) {
		if (m_ok && static_cast<u32>(slot.data_word()) < m_used_words) {
			m_words[slot.opcode_word()] = reg.offset;
		}
	}

	/// SKIP: WAIT con mascara `0xffff` (bit 0 = 1 => salta la instruccion siguiente si el
	/// beam ya paso por (vpos, hpos)). Codificacion de `CopSkip` de libgfx (verbatim):
	/// la segunda word con el bit 0 a 1 distingue SKIP de WAIT (`custom.cpp:9249`).
	/// `hpos` en unidades de color-clock (se divide por 2 como en el original).
	WaitSlot skip(u16 vpos, u16 hpos) {
		const u16 index = m_used_words;
		write_pair(wait_word(static_cast<u8>(vpos & 0xffu), static_cast<u8>((hpos >> 1) | 1u)), 0xffff);
		return WaitSlot {index};
	}

	/// WAIT crudo (equivale a `CopInsWait`): `word0 = (vp&0xff)<<8 | ((hp>>1)|1)`,
	/// `word1 = mask`. `hp` en color-clock. Devuelve el slot.
	WaitSlot wait_raw(u16 vp, u16 hp, u16 mask) {
		const u16 index = m_used_words;
		write_pair(static_cast<u16>(((vp & 0xffu) << 8) | (((hp >> 1) | 1u) & 0xffu)), mask);
		return WaitSlot {index};
	}

	/// WAIT con mascaras de V/H (equivale a `CopWaitMask`): `word0 = (vp&0xff)<<8 |
	/// ((hp>>1)|1)`, `word1 = ((0x80|vpmask)<<8) | ((hpmask>>1)&0xfe)`.
	///
	/// `vpmask`/`hpmask` se recortan a sus campos reales (AHRM Table 2-2): `VE` = bits 14-8
	/// (el bit 15 es BFD y el bit 7 de V no se enmascara, `custom.cpp:4427`) y `HE` = bits 7-1.
	WaitSlot wait_masked(u16 vp, u16 hp, u16 vpmask, u16 hpmask) {
		const u16 index = m_used_words;
		write_pair(static_cast<u16>(((vp & 0xffu) << 8) | (((hp >> 1) | 1u) & 0xffu)),
			   static_cast<u16>((((0x80u | (vpmask & 0x7fu)) & 0xffu) << 8) | ((hpmask >> 1) & 0xfeu)));
		return WaitSlot {index};
	}

	/// MOVE de 32 bits (puntero) con el **orden del original** (libgfx `CopMove32`):
	/// primero `reg+2` (word bajo) y luego `reg` (word alto). Devuelve el slot del primer
	/// MOVE del par.
	MoveSlot move32(Register reg, eng::Address<eng::MemoryKind::Chip> address) {
		const uintptr raw = address.value;
		const u16 index = m_used_words;
		write_pair(static_cast<u16>(static_cast<u16>(reg) + 2u), static_cast<u16>(raw & 0xffffu));
		write_pair(static_cast<u16>(reg), static_cast<u16>(raw >> 16));
		return MoveSlot {index};
	}

	/// Parchea un MOVE32 emitido con `move32` (slot del primer MOVE del par) con otra
	/// direccion (equivale a `CopInsSet32` del original; p. ej. apuntar `COP2LC` al label
	/// de una fila). Escribe **low primero** en `slot` y **high** en `slot+2`, el mismo
	/// reparto que `move32`.
	void patch_move32(MoveSlot slot, eng::Address<eng::MemoryKind::Chip> address) {
		const uintptr raw = address.value;
		patch_data(slot, static_cast<u16>(raw & 0xffffu));
		if (m_ok && static_cast<u32>(slot.next_instruction().data_word()) < m_used_words) {
			m_words[slot.next_instruction().data_word()] = static_cast<u16>(raw >> 16);
		}
	}

	/// Word de la direccion de una instruccion (para calcular labels de copper).
	constexpr eng::Address<eng::MemoryKind::Chip> instruction_address(MoveSlot slot) const {
		return eng::Address<eng::MemoryKind::Chip>::from_storage(m_words) + slot.index * 2u;
	}
	/// Igual que la anterior para un WAIT/SKIP (destino de `COP2LC` en bucles de lista).
	constexpr eng::Address<eng::MemoryKind::Chip> instruction_address(WaitSlot slot) const {
		return eng::Address<eng::MemoryKind::Chip>::from_storage(m_words) + slot.index * 2u;
	}

	constexpr bool ok() const {
		return m_ok;
	}

	constexpr u16* data() const {
		return m_words;
	}

	/// Vista de las palabras emitidas (Chip RAM), sin banco: solo para computos internos
	/// (p. ej. unir sub-listas); para DMA usar `chip_view()`.
	[[nodiscard]] constexpr eng::Span<u16> span() const {
		return eng::Span<u16> {m_words, m_used_words};
	}

	/// **Vista tipada (Chip)** de la lista emitida hasta `words_used()`: es la forma que
	/// exigen `takeover_display`/`install_copper_list` (la procedencia DMA viaja en el tipo).
	[[nodiscard]] constexpr eng::ChipView<eng::CopperTag> chip_view() const {
		return eng::ChipView<eng::CopperTag> {
			eng::Address<eng::MemoryKind::Chip>::from_storage(
				reinterpret_cast<const eng::u8*>(m_words)),
			static_cast<eng::u32>(m_used_words) * 2u};
	}

	constexpr u16 words_used() const {
		return m_used_words;
	}

	constexpr u32 bytes_used() const {
		return static_cast<u32>(m_used_words) * sizeof(u16);
	}

private:
	/// Escribe un par WAIT/MOVE (`a`,`b`) y avanza el cursor. **Camino caliente**: la
	/// emisión de una copperlist por línea lo llama miles de veces por frame, así que
	/// `always_inline` es deliberado. Sin él, a `-O1` gcc recargaba `m_ok`,
	/// `m_used_words` y `m_capacity_words` desde memoria en cada palabra y recomputaba
	/// el puntero base (medido: el patrón dominante en `build_frame` de la 086).
	///
	/// El chequeo de capacidad se hace una vez por par (no por palabra) y contra un
	/// límite con margen de 1 palabra: si quedara justo una palabra libre, un par la
	/// desbordaría a medias, así que se exige `+2 <= capacity`; el camino rápido evita
	/// tocar memoria salvo las dos escrituras.
	__attribute__((always_inline)) inline void write_pair(u16 a, u16 b) {
		const u16 used = m_used_words;
		if (m_capacity_words < 2u || used > m_capacity_words - 2u) {
			m_ok = false;
			m_overflow_sent = true;
			return;
		}
		u16* const words = m_words;
		words[used] = a;
		words[used + 1u] = b;
		m_used_words = static_cast<u16>(used + 2u);
	}

	u16* m_words = nullptr; ///< buffer de la copperlist (Chip RAM), base de escritura
	u16 m_capacity_words = 0; ///< capacidad del buffer en palabras
	u16 m_used_words = 0; ///< palabras escritas hasta ahora
	bool m_ok = false; ///< no se ha desbordado la capacidad
	bool m_overflow_sent = false; ///< ya se marcó un desborde (evita repetir el aviso)
};

} // namespace eng::copper
