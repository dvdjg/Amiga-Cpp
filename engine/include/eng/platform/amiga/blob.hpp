#pragma once

/// \file blob.hpp
/// **Lote de BOBs OR intercalados de coste cero**: programacion del Blitter `inline`
/// (sin `jsr` por objeto) para que el calculo por objeto del llamador y la programacion
/// del blit queden en el **mismo bucle** — la estructura exacta de `DrawObject` de
/// `demoscene-repo-orig/effects/bobs3d/bobs3d.c`.
///
/// Fija las constantes del lote UNA vez (`begin`) y por objeto solo escribe
/// `BLTCON0` (minterm `A_OR_B` + ASH), `BLTAPT`, `BLT(B/D)PT` y `BLTSIZE`. `.asm` medido
/// en la demo 117: la version no-inline gastaba un `jsr` + 4 pushes por BOB (~600
/// ciclos/objeto).
///
/// Es una cabecera de **plataforma** (igual que `object3d.hpp`) y expone registros custom
/// a proposito: es la frontera unsafe del backend. El puntero se obtiene con
/// `AmigaBackend::custom_registers()`.
///
/// Uso:
///   eng::amiga::OrBlobBatch batch;
///   batch.begin(backend.custom_registers(), words, height, amod, dmod);
///   for (cada objeto) batch.one(src, dst, shift);
///   batch.end();

#include <eng/core/types/types.hpp>

namespace eng::amiga {

/// Programa un lote de BOBs OR intercalados con el cursor del Blitter en la instancia
/// (sin estado global). Todos los metodos son `always_inline`.
class OrBlobBatch {
public:
	using Reg = volatile eng::u16;

	/// Fija las constantes del lote. `custom` = base de registros $dff000.
	__attribute__((always_inline)) inline void begin(Reg* custom, eng::u16 words,
							 eng::u16 height, eng::s16 amod,
							 eng::s16 dmod) {
		c = custom;
		// DMACON: SET de MASTER + BLITTER. No toca BLTPRI (lo fija el copper), que
		// hace que el Blitter no ceda slots a la CPU (mismo efecto que el original).
		c[kDmacon] = static_cast<eng::u16>(0x8000u | 0x0200u | 0x0040u);
		wait();
		con0 = static_cast<eng::u16>(kUseA | kUseB | kUseD | kMintermAOrB);
		size = static_cast<eng::u16>((static_cast<eng::u16>(height) << 6u) | words);
		c[kBltcon1] = 0;
		c[kBltafwm] = 0xffff;
		c[kBltalwm] = 0xffff;
		c[kBltamod] = static_cast<eng::u16>(amod);
		c[kBltbmod] = static_cast<eng::u16>(dmod);
		c[kBltdmod] = static_cast<eng::u16>(dmod);
	}

	/// Lanza UN objeto (espera al anterior antes de reprogramar los punteros).
	__attribute__((always_inline)) inline void one(const void* source, void* dest,
						       eng::u8 shift) {
		wait();
		const eng::u16 s = static_cast<eng::u16>(static_cast<eng::u16>(shift & 0x0fu) << 12u);
		c[kBltcon0] = static_cast<eng::u16>(s | con0);
		// En Amiga `uintptr` es 32 bits (el `static_cast` es exacto); en host 64 bits se
		// estrecha a la direccion de 32 bits (los punteros del test caben).
		*reinterpret_cast<volatile eng::u32*>(&c[kBltapt]) =
			static_cast<eng::u32>(reinterpret_cast<eng::uintptr>(source));
		*reinterpret_cast<volatile eng::u32*>(&c[kBltbpt]) =
			static_cast<eng::u32>(reinterpret_cast<eng::uintptr>(dest));
		*reinterpret_cast<volatile eng::u32*>(&c[kBltdpt]) =
			static_cast<eng::u32>(reinterpret_cast<eng::uintptr>(dest));
		c[kBltsize] = size;
	}

	/// Espera al ultimo objeto del lote.
	__attribute__((always_inline)) inline bool end() {
		wait();
		return true;
	}

private:
	/// BBUSY (DMACONR bit 14): `btst` sobre la palabra completa.
	__attribute__((always_inline)) inline void wait() const {
		while ((c[kDmaconr] & 0x4000u) != 0u) {
		}
	}

	static constexpr eng::u16 kDmaconr = 0x002u / 2u;
	static constexpr eng::u16 kDmacon = 0x096u / 2u;
	static constexpr eng::u16 kBltcon0 = 0x040u / 2u;
	static constexpr eng::u16 kBltcon1 = 0x042u / 2u;
	static constexpr eng::u16 kBltafwm = 0x044u / 2u;
	static constexpr eng::u16 kBltalwm = 0x046u / 2u;
	static constexpr eng::u16 kBltbpt = 0x04cu / 2u;
	static constexpr eng::u16 kBltapt = 0x050u / 2u;
	static constexpr eng::u16 kBltdpt = 0x054u / 2u;
	static constexpr eng::u16 kBltsize = 0x058u / 2u;
	static constexpr eng::u16 kBltbmod = 0x062u / 2u;
	static constexpr eng::u16 kBltamod = 0x064u / 2u;
	static constexpr eng::u16 kBltdmod = 0x066u / 2u;
	static constexpr eng::u16 kUseA = 0x0800u;
	static constexpr eng::u16 kUseB = 0x0400u;
	static constexpr eng::u16 kUseD = 0x0100u;
	static constexpr eng::u16 kMintermAOrB = 0x00fcu;

	Reg* c = nullptr;
	eng::u16 con0 = 0;
	eng::u16 size = 0;
};

} // namespace eng::amiga
