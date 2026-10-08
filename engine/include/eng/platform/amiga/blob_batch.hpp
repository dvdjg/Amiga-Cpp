#pragma once

/// \file blob_batch.hpp
/// **Lote de Blobs/BOBs con estado fijo** (capa de plataforma, junto a `blob.hpp`):
/// programa el Blitter `inline` fijando **una vez** el estado común (`BLTCON1`, ventanas
/// `AFWM`/`ALWM`, módulos) y, por objeto, escribiendo solo `BLTCON0` (con `ASH`/`BSH`),
/// los punteros A/B/C/D y `BLTSIZE` — la estructura de `DrawObject` del `main.c` de
/// referencia (`BartmanBasic/main.c`: `WaitBlit(); bltcon0=…; bltapt=…; …; bltsize=…;`).
///
/// Generaliza el patrón de `OrBlobBatch` (`blob.hpp`) a las operaciones que comparten
/// forma de registros: **OR** (`$FC`), **cookie-cut** (`$CA`, A=máscara/B=imagen/C=D=fondo)
/// y **copia opaca** (`$F0`). El estado fijo elimina la validación+copia de `BlitJob`,
/// el recalculo de presupuesto y la re-codificación por job del camino `FramePlan`, y deja
/// **una sola espera** por objeto (la mínima que exige el hardware para reprogramar
/// punteros), igual que el original.
///
/// Es cabecera de **plataforma** (como `blob.hpp`/`object3d.hpp`) y expone registros custom
/// a propósito: es la frontera unsafe del backend. El bloque de registros se pide tipado con
/// `HwRegs::instance()` (o `AmigaBackend::hw_regs()`), y los punteros de Blitter con
/// `graphics::BlitPtr` (dirección `Address<MemoryKind::Chip>`): el compilador **no acepta**
/// un `void*`/`volatile u16*` suelto, así que un destino en Fast/Slow RAM no compila.
///
/// Uso (cookie-cut interleaved de la 213):
///   eng::amiga::BlobBatch batch;
///   batch.begin(eng::amiga::HwRegs::instance(), eng::amiga::BlobOp::CookieCut, words,
///               height, amod, bmod, cmod, dmod);
///   for (cada BOB) batch.one(mask, image, dest, shift);   // mask/image/dest: BlitPtr
///   batch.end();

#include <eng/core/types/types.hpp>
#include <eng/graphics/blit_job.hpp>
#include <eng/graphics/blitter_state.hpp>
#include <eng/platform/amiga/hw_regs.hpp>

namespace eng::amiga {

/// Operación del lote: fija el minterm y **qué canales** conecta cada `one`. El vocabulario es de
/// **dominio** (`eng::graphics::BlobOp`, en `blitter_state.hpp`) para que la fachada pueda pedir
/// una racha sin nombrar tipos del backend.
using BlobOp = eng::graphics::BlobOp;

/// Programa un lote de blobs con estado fijo. Todos los métodos son `always_inline`: el
/// cálculo por objeto del llamador y la programación del blit quedan en el **mismo bucle**,
/// sin `jsr` por objeto (medido en `OrBlobBatch`/`bobs3d`: ~600 ciclos/objeto ahorrados).
class BlobBatch {
public:
	/// **Servicio de espera** opcional que el lote drena en cada vuelta del sondeo a BBUSY, igual
	/// que `AmigaBackend::wait_blitter`. La firma es C (`fn(user, vpos)`) a propósito: el servicio
	/// puede correr en contextos ISR-sensibles y su contexto lo **posee el llamador** (el backend
	/// guarda ahí su slot de servicio de fondo; un test, lo que necesite). `vpos` = línea de raster
	/// actual (bits 8-0 de VPOSR) por si el servicio quiere repartir su trabajo por la línea.
	/// `nullptr` deja el sondeo como bucle apretado, sin drenar nada (idéntico al original).
	using WaitFn = void (*)(void*, eng::u16);

	/// Fija las constantes del lote. `regs` = bloque de registros del hardware donde se va a
	/// programar el lote (`HwRegs::instance()` en Amiga; en tests, `HwRegs::for_test`).
	/// `words`/`height` son el ancho (palabras) y el alto (filas físicas = alto lógico ×
	/// planos en interleaved) del blit. `amod`/`bmod` son los módulos de A/B (bytes);
	/// `cmod`/`dmod` los de C/D. Para cookie-cut interleaved del par `[…imagen][…máscara]`
	/// el `bmod` es el mismo que `amod` (`= words·2`). `wait_fn`/`wait_user` = servicio de
	/// espera opcional (ver `WaitFn`); el lote solo guarda el contexto, nunca lo interpreta.
	__attribute__((always_inline)) inline void begin(HwRegs regs, BlobOp op, eng::u16 words,
							 eng::u16 height, eng::s16 amod, eng::s16 bmod,
							 eng::s16 cmod, eng::s16 dmod,
							 WaitFn wait_fn = nullptr, void* wait_user = nullptr) {
		regs_ = regs;
		op_ = op;
		wait_fn_ = wait_fn;
		wait_user_ = wait_user;
		// Espera primero (por si el lote anterior dejó blits vivos) y habilita después el
		// DMA de Blitter: SET de MASTER + BLITTER, sin tocar BLTPRI (lo gestiona la propia
		// espera: nasty alrededor del sondeo).
		wait();
		regs_.word(kDmacon) = static_cast<eng::u16>(0x8000u | 0x0200u | 0x0040u);
		eng::u16 use = 0u;
		eng::u16 minterm = 0u;
		switch (op) {
			case BlobOp::Or:        use = kUseA | kUseB | kUseD; minterm = kMintermAOrB; break;
			case BlobOp::CookieCut: use = kUseA | kUseB | kUseC | kUseD; minterm = kMintermCookieCut; break;
			case BlobOp::Opaque:    use = kUseA | kUseD; minterm = kMintermCopyA; break;
			case BlobOp::Copy:      use = kUseC | kUseD; minterm = kMintermCopyC; break;
			case BlobOp::Clear:     use = kUseD; minterm = kMintermZero; break;
		}
		base_con0 = static_cast<eng::u16>(use | minterm);
		size = static_cast<eng::u16>((static_cast<eng::u16>(height) << 6u) | words);
		// Registros comunes en escrituras de 32 bits, como el original (`move.l
		// d6,bltcon0`, `move.l #$ffffffff,bltafwm`, `move.l d4,bltamod` + swap a
		// `bltcmod`, GFX/blitter.asm:95-98): cada escritura a un registro custom cuesta
		// un ciclo de bus y empaquetar pares contiguos reduce a la mitad el coste por
		// blob. En el long big-endian el primer registro del par va en los 16 bits altos.
		write_long(kBltcon0, static_cast<eng::u32>(base_con0) << 16u); // CON1 = 0
		write_long(kBltafwm, 0xffffffffu);
		write_long(kBltcmod, (static_cast<eng::u32>(static_cast<eng::u16>(cmod)) << 16u) |
					     static_cast<eng::u16>(bmod));
		write_long(kBltamod, (static_cast<eng::u32>(static_cast<eng::u16>(amod)) << 16u) |
					     static_cast<eng::u16>(dmod));
	}

	/// Lanza UN blob. `shift` = desplazamiento fino X (0..15). Según la operación:
	///   - `Or`/`Opaque`: `a` = fuente (imagen), `b`/`d` = destino.
	///   - `CookieCut`: `a` = máscara, `b` = imagen, `c`/`d` = destino (fondo).
	/// Los tres punteros son `graphics::BlitPtr` (dirección DMA en **Chip RAM**): no se acepta
	/// un `void*` suelto.
	/// Espera al blob anterior antes de reprogramar los punteros (hardware: un juego de
	/// registros); es la ÚNICA espera por objeto. La cola del Blitter es de un nivel: si
	/// se encadenan las escrituras sin esperar, la siguiente pisa a la encolada y el
	/// blit sale incompleto (probado: BOBs con anillos cortados). El original también
	/// espera por objeto (`BlitBob`, `spr_layer/Sprite_Layer/GFX/blitter.asm:94`).
	__attribute__((always_inline)) inline void one(eng::graphics::BlitPtr a, eng::graphics::BlitPtr b,
						       eng::graphics::BlitPtr d, eng::u8 shift) {
		wait();
		if (op_ == BlobOp::Clear) {
			// Solo D: sin fuente ni desplazamiento.
			write_ptr(kBltdpt, d);
			regs_.word(kBltsize) = size;
			return;
		}
		if (op_ == BlobOp::Copy) {
			// `D = C`: origen en C, destino en D, sin barrel shifter.
			write_ptr(kBltcpt, a);
			write_ptr(kBltdpt, d);
			regs_.word(kBltsize) = size;
			return;
		}
		const eng::u16 s = static_cast<eng::u16>(static_cast<eng::u16>(shift & 0x0fu) << 12u);
		// CON0/CON1 en una sola escritura de 32 bits (el original: `move.l d6,bltcon0`):
		// cookie-cut añade BSH en CON1; el resto deja CON1 = 0 (fijado en `begin`). Todo el
		// par se escribe siempre empaquetado.
		const eng::u16 con1 = (op_ == BlobOp::CookieCut) ? s : 0u;
		write_long(kBltcon0,
			   (static_cast<eng::u32>(static_cast<eng::u16>(base_con0 | s)) << 16u) | con1);
		write_ptr(kBltapt, a);
		if (op_ == BlobOp::CookieCut) {
			write_ptr(kBltbpt, b);
			write_ptr(kBltcpt, d);
		} else {
			write_ptr(kBltbpt, d);
		}
		write_ptr(kBltdpt, d);
		regs_.word(kBltsize) = size;
	}

	/// Fija las ventanas de máscara del lote (`BLTAFWM`/`BLTALWM`). Sirve para el truco de
	/// los BOBs de 3 palabras: `alwm = 0x0000` **descarta la última palabra** del blit
	/// (fuente y destino), de modo que un blit de 3 palabras con `ASH`/`BSH` lee y escribe
	/// solo 2 palabras y usa la tercera como arrastre del barrel shifter. La referencia
	/// («SPR Layer») lo fija justo antes de dibujar sus 9 BOBs.
	__attribute__((always_inline)) inline void set_window_masks(eng::u16 afwm, eng::u16 alwm) {
		write_long(kBltafwm, (static_cast<eng::u32>(afwm) << 16u) | alwm);
	}

	/// Espera al último blob del lote.
	__attribute__((always_inline)) inline bool end() {
		wait();
		return true;
	}

private:
	/// Escribe el par PTH/PTL en **una sola escritura de 32 bits** (el original usa
	/// `move.l a2,bltapt`): una transaccion de bus en vez de dos. Los registros de punteros
	/// ($048-$056) estan alineados a 4 bytes y el build usa `-fno-strict-aliasing`. El par
	/// puede escribirse con el Blitter ocupado: queda latcheado para el siguiente blit.
	__attribute__((always_inline)) inline void write_ptr(eng::u16 word_index,
							     eng::graphics::BlitPtr p) {
		write_long(word_index, static_cast<eng::u32>(p.addr.value));
	}

	/// Escribe un registro doble (par contiguo de 16 bits) como una escritura de 32 bits,
	/// big-endian: el primer registro del par va en los 16 bits altos. Los pares usados
	/// (CON0/CON1, AFWM/ALWM, CMOD/BMOD, AMOD/DMOD) estan alineados a 4 bytes.
	__attribute__((always_inline)) inline void write_long(eng::u16 word_index, eng::u32 v) {
		*reinterpret_cast<volatile eng::u32*>(&regs_.word(word_index)) = v;
	}

	/// **Espera a que el Blitter quede libre** (BBUSY de DMACONR, bit 14) en modo «blitter nasty»
	/// (BLTPRI): el CPU suelta el bus mientras sondea y no le roba ciclos al Blitter. Sin ello, un
	/// blit largo (p. ej. los BOBs de 384 palabras de la 218) tarda hasta 3x más (WinUAE
	/// ciclo-exacto: `blitter.cpp:1745-1770`; el robo del CPU al Blitter se desactiva con
	/// `DMA_BLITPRI`). Es la macro `BlitWait` del original (`spr_layer/Sprite_Layer/GFX/blitter.i:26-31`).
	/// Mientras gira el sondeo, si hay `wait_fn_` registrado se llama en **cada vuelta** con el
	/// contexto de `wait_user_` y la línea de raster actual: así el fondo (tareas del mini-SO)
	/// avanza en vez de ser tiempo muerto. Es el único uso de `wait_fn_`/`wait_user_`.
	__attribute__((always_inline)) inline void wait() const {
		regs_.word(kDmacon) = 0x8400u; // SETCLR | BLTPRI: activa nasty
		while ((regs_.word(kDmaconr) & 0x4000u) != 0u) {
			if (wait_fn_ != nullptr) {
				const eng::u32 vposr =
					*reinterpret_cast<volatile eng::u32*>(&regs_.word(kVposr));
				wait_fn_(wait_user_, static_cast<eng::u16>((vposr & 0x1ff00u) >> 8u));
			}
		}
		regs_.word(kDmacon) = 0x0400u; // SETCLR=0 | BLTPRI: desactiva nasty
	}

	// Offsets de registro (en palabras de 16 bits, `byte/2`).
	static constexpr eng::u16 kDmaconr = 0x002u / 2u;
	static constexpr eng::u16 kVposr = 0x004u / 2u;
	static constexpr eng::u16 kBltcon0 = 0x040u / 2u;
	static constexpr eng::u16 kBltcon1 = 0x042u / 2u;
	static constexpr eng::u16 kBltafwm = 0x044u / 2u;
	static constexpr eng::u16 kBlctalwm = 0x046u / 2u;
	static constexpr eng::u16 kBltcpt = 0x048u / 2u;
	static constexpr eng::u16 kBltbpt = 0x04cu / 2u;
	static constexpr eng::u16 kBltapt = 0x050u / 2u;
	static constexpr eng::u16 kBltdpt = 0x054u / 2u;
	static constexpr eng::u16 kBltsize = 0x058u / 2u;
	static constexpr eng::u16 kBltcmod = 0x060u / 2u;
	static constexpr eng::u16 kBltbmod = 0x062u / 2u;
	static constexpr eng::u16 kBltamod = 0x064u / 2u;
	static constexpr eng::u16 kBltdmod = 0x066u / 2u;
	static constexpr eng::u16 kDmacon = 0x096u / 2u;

	static constexpr eng::u16 kUseA = eng::graphics::kBlitterUseA;
	static constexpr eng::u16 kUseB = eng::graphics::kBlitterUseB;
	static constexpr eng::u16 kUseC = eng::graphics::kBlitterUseC;
	static constexpr eng::u16 kUseD = eng::graphics::kBlitterUseD;
	static constexpr eng::u16 kMintermAOrB = eng::graphics::kBlitterMintermAOrB;
	static constexpr eng::u16 kMintermCopyA = eng::graphics::kBlitterMintermCopyA;
	static constexpr eng::u16 kMintermCopyC = eng::graphics::kBlitterMintermCopyC;
	static constexpr eng::u16 kMintermCookieCut = eng::graphics::kBlitterMintermCookieCut;
	static constexpr eng::u16 kMintermZero = eng::graphics::kBlitterMintermZero;

	HwRegs regs_ {};            ///< Bloque de registros del hardware donde se programa el lote (en Amiga, `$DFF000` fijo; en tests, el mock de `HwRegs::for_test`).
	eng::u16 base_con0 = 0;     ///< BLTCON0 base del lote (canales + minterm), **sin** el desplazamiento fino `ASH` que añade cada `one`.
	eng::u16 size = 0;          ///< BLTSIZE común del lote: alto (filas físicas) en bits 15-6 y ancho (palabras) en 5-0.
	BlobOp op_ = BlobOp::Or;    ///< Operación del lote; decide qué canales conecta cada `one` y cómo se interpretan `a`/`b`/`d`.
	WaitFn wait_fn_ = nullptr;  ///< Servicio de espera opcional (ver `WaitFn`); `nullptr` = sondeo apretado sin drenar fondo.
	void* wait_user_ = nullptr; ///< Contexto **opaco** del servicio: lo posee quien registró `wait_fn_` (el backend guarda ahí su slot de servicio); el lote solo lo devuelve tal cual en cada llamada, nunca lo interpreta. No es memoria DMA ni del lote.
};

} // namespace eng::amiga
