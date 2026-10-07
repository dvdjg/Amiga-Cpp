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
/// a propósito: es la frontera unsafe del backend. El puntero se obtiene con
/// `AmigaBackend::custom_registers()`.
///
/// Uso (cookie-cut interleaved de la 213):
///   eng::amiga::BlobBatch batch;
///   batch.begin(custom, eng::amiga::BlobOp::CookieCut, words, height, amod, bmod, cmod, dmod);
///   for (cada BOB) batch.one(mask, image, dest, shift);
///   batch.end();

#include <eng/core/types/types.hpp>
#include <eng/graphics/blitter_state.hpp>

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
	using Reg = volatile eng::u16;
	using WaitFn = void (*)(void*, eng::u16);

	/// Fija las constantes del lote. `custom` = base de registros `$dff000`.
	/// `words`/`height` son el ancho (palabras) y el alto (filas físicas = alto lógico ×
	/// planos en interleaved) del blit. `amod`/`bmod` son los módulos de A/B (bytes);
	/// `cmod`/`dmod` los de C/D. Para cookie-cut interleaved del par `[…imagen][…máscara]`
	/// el `bmod` es el mismo que `amod` (`= words·2`).
	__attribute__((always_inline)) inline void begin(Reg* custom, BlobOp op, eng::u16 words,
							 eng::u16 height, eng::s16 amod, eng::s16 bmod,
							 eng::s16 cmod, eng::s16 dmod,
							 WaitFn wait_fn = nullptr, void* wait_user = nullptr) {
		c = custom;
		op_ = op;
		wait_fn_ = wait_fn;
		wait_user_ = wait_user;
		// DMACON: SET de MASTER + BLITTER (no toca BLTPRI; lo fija el copper).
		c[kDmacon] = static_cast<eng::u16>(0x8000u | 0x0200u | 0x0040u);
		wait();
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
		c[kBltcon0] = base_con0; // Clear no lo reescribe en `one`; el resto lo ajusta con el shift
		c[kBltcon1] = 0;
		c[kBltafwm] = 0xffff;
		c[kBlctalwm] = 0xffff;
		c[kBltamod] = static_cast<eng::u16>(amod);
		c[kBltbmod] = static_cast<eng::u16>(bmod);
		c[kBltcmod] = static_cast<eng::u16>(cmod);
		c[kBltdmod] = static_cast<eng::u16>(dmod);
	}

	/// Lanza UN blob. `shift` = desplazamiento fino X (0..15). Según la operación:
	///   - `Or`/`Opaque`: `a` = fuente (imagen), `b`/`d` = destino.
	///   - `CookieCut`: `a` = máscara, `b` = imagen, `c`/`d` = destino (fondo).
	/// Espera al blob anterior antes de reprogramar los punteros (hardware: un juego de
	/// registros); es la ÚNICA espera por objeto. La cola del Blitter es de un nivel: si
	/// se encadenan las escrituras sin esperar, la siguiente pisa a la encolada y el
	/// blit sale incompleto (probado: BOBs con anillos cortados). El original también
	/// espera por objeto (`BlitBob`, `spr_layer/Sprite_Layer/GFX/blitter.asm:94`).
	__attribute__((always_inline)) inline void one(const void* a, const void* b, void* d,
						       eng::u8 shift) {
		wait();
		if (op_ == BlobOp::Clear) {
			// Solo D: sin fuente ni desplazamiento.
			write_ptr(kBltdpt, d);
			c[kBltsize] = size;
			return;
		}
		if (op_ == BlobOp::Copy) {
			// `D = C`: origen en C, destino en D, sin barrel shifter.
			write_ptr(kBltcpt, a);
			write_ptr(kBltdpt, d);
			c[kBltsize] = size;
			return;
		}
		const eng::u16 s = static_cast<eng::u16>(static_cast<eng::u16>(shift & 0x0fu) << 12u);
		c[kBltcon0] = static_cast<eng::u16>(base_con0 | s);
		if (op_ == BlobOp::CookieCut) {
			c[kBltcon1] = s; // BSH (el barrel shifter desplaza A y B)
		}
		write_ptr(kBltapt, a);
		if (op_ == BlobOp::CookieCut) {
			write_ptr(kBltbpt, b);
			write_ptr(kBltcpt, d);
		} else {
			write_ptr(kBltbpt, d);
		}
		write_ptr(kBltdpt, d);
		c[kBltsize] = size;
	}

	/// Fija las ventanas de máscara del lote (`BLTAFWM`/`BLTALWM`). Sirve para el truco de
	/// los BOBs de 3 palabras: `alwm = 0x0000` **descarta la última palabra** del blit
	/// (fuente y destino), de modo que un blit de 3 palabras con `ASH`/`BSH` lee y escribe
	/// solo 2 palabras y usa la tercera como arrastre del barrel shifter. La referencia
	/// («SPR Layer») lo fija justo antes de dibujar sus 9 BOBs.
	__attribute__((always_inline)) inline void set_window_masks(eng::u16 afwm, eng::u16 alwm) {
		c[kBltafwm] = afwm;
		c[kBlctalwm] = alwm;
	}

	/// Espera al último blob del lote.
	__attribute__((always_inline)) inline bool end() {
		wait();
		return true;
	}

private:
	/// Escribe el par PTH/PTL como **dos stores de 16 bits** (registro alto primero, big-endian),
	/// igual que hace el compilador al asignar un `u32` a un registro de 16 bits en el original.
	/// Se evita el `reinterpret_cast<volatile u32*>` sobre registros `volatile u16*` (aliasing que
	/// a `-O2` puede reordenarse/miscompilarse). El par puede escribirse con el Blitter ocupado:
	/// queda latcheado para el siguiente blit.
	__attribute__((always_inline)) inline void write_ptr(eng::u16 word_index, const void* p) {
		const eng::u32 v = static_cast<eng::u32>(reinterpret_cast<eng::uintptr>(p));
		c[word_index] = static_cast<eng::u16>(v >> 16u);
		c[static_cast<eng::u16>(word_index + 1u)] = static_cast<eng::u16>(v);
	}

	/// BBUSY (DMACONR bit 14). Con `wait_fn`, drena fondo en cada vuelta.
	/// Sondea en modo «blitter nasty» (BLTPRI): el CPU suelta el bus mientras espera y
	/// no le roba ciclos al Blitter. Sin ello, el Blitter de un blit largo (p. ej. los
	/// BOBs de 384 palabras de la 218) tarda hasta 3x más (WinUAE ciclo-exacto:
	/// `blitter.cpp:1745-1770`, el robo del CPU se desactiva con `DMA_BLITPRI`).
	/// Es la macro `BlitWait` del original (`spr_layer/Sprite_Layer/GFX/blitter.i:26-31`).
	__attribute__((always_inline)) inline void wait() const {
		c[kDmacon] = 0x8400u; // SETCLR | BLTPRI: activa nasty
		while ((c[kDmaconr] & 0x4000u) != 0u) {
			if (wait_fn_ != nullptr) {
				const eng::u32 vposr = *reinterpret_cast<volatile eng::u32*>(&c[kVposr]);
				wait_fn_(wait_user_, static_cast<eng::u16>((vposr & 0x1ff00u) >> 8u));
			}
		}
		c[kDmacon] = 0x0400u; // SETCLR=0 | BLTPRI: desactiva nasty
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

	Reg* c = nullptr;
	eng::u16 base_con0 = 0;
	eng::u16 size = 0;
	BlobOp op_ = BlobOp::Or;
	WaitFn wait_fn_ = nullptr;
	void* wait_user_ = nullptr;
};

} // namespace eng::amiga
