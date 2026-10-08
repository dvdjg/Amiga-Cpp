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
/// a proposito: es la frontera unsafe del backend. El bloque de registros se pide tipado con
/// `HwRegs::instance()` (o `AmigaBackend::hw_regs()`), y los punteros de Blitter con
/// `graphics::BlitPtr` (direccion DMA en Chip RAM; un `void*` suelto no compila).
///
/// Uso:
///   eng::amiga::OrBlobBatch batch;
///   batch.begin(backend.hw_regs(), words, height, amod, dmod);
///   for (cada objeto) batch.one(src, dst, shift);   // BlitPtr
///   batch.end();

#include <eng/core/types/types.hpp>
#include <eng/graphics/blit_job.hpp>
#include <eng/graphics/blitter_state.hpp>
#include <eng/platform/amiga/hw_regs.hpp>

namespace eng::amiga {

/// Programa un lote de BOBs OR intercalados con el cursor del Blitter en la instancia
/// (sin estado global). Todos los metodos son `always_inline`.
class OrBlobBatch {
public:
	/// **Servicio de espera** opcional que el lote drena en cada vuelta del sondeo a BBUSY, igual
	/// que `AmigaBackend::wait_blitter`: firma C (`fn(user, vpos)`) a propósito (el servicio puede
	/// correr en contextos ISR-sensibles y el contexto lo posee el llamador; el backend guarda ahí
	/// su slot de servicio). `vpos` = línea de raster actual (bits 8-0 de VPOSR). `nullptr` = bucle
	/// apretado (idéntico al original). El backend lo pasa desde su servicio de espera de Blitter.
	using WaitFn = void (*)(void*, eng::u16);

	/// Fija las constantes del lote. `regs` = bloque de registros del hardware (`HwRegs::instance()`
	/// en Amiga; `HwRegs::for_test` en tests). `wait_fn`/`wait_user` = servicio de espera opcional
	/// (ver `WaitFn`); el lote solo guarda el contexto, nunca lo interpreta.
	__attribute__((always_inline)) inline void begin(HwRegs regs, eng::u16 words,
							 eng::u16 height, eng::s16 amod,
							 eng::s16 dmod, WaitFn wait_fn = nullptr,
							 void* wait_user = nullptr) {
		regs_ = regs;
		wait_fn_ = wait_fn;
		wait_user_ = wait_user;
		// Espera primero (por si el lote anterior dejó blits vivos) y habilita después el
		// DMA de Blitter: SET de MASTER + BLITTER, sin tocar BLTPRI (lo gestiona la propia
		// espera: nasty alrededor del sondeo).
		wait();
		regs_.word(kDmacon) = static_cast<eng::u16>(0x8000u | 0x0200u | 0x0040u);
		con0 = static_cast<eng::u16>(kUseA | kUseB | kUseD | kMintermAOrB);
		size = static_cast<eng::u16>((static_cast<eng::u16>(height) << 6u) | words);
		regs_.word(kBltcon1) = 0;
		regs_.word(kBltafwm) = 0xffff;
		regs_.word(kBltalwm) = 0xffff;
		regs_.word(kBltamod) = static_cast<eng::u16>(amod);
		regs_.word(kBltbmod) = static_cast<eng::u16>(dmod);
		regs_.word(kBltdmod) = static_cast<eng::u16>(dmod);
	}

	/// Lanza UN objeto (espera al anterior antes de reprogramar los punteros). `source`/`dest`
	/// son `graphics::BlitPtr` (Chip RAM).
	__attribute__((always_inline)) inline void one(eng::graphics::BlitPtr source,
						       eng::graphics::BlitPtr dest, eng::u8 shift) {
		wait();
		const eng::u16 s = static_cast<eng::u16>(static_cast<eng::u16>(shift & 0x0fu) << 12u);
		regs_.word(kBltcon0) = static_cast<eng::u16>(s | con0);
		write_ptr(kBltapt, source);
		write_ptr(kBltbpt, dest);
		write_ptr(kBltdpt, dest);
		regs_.word(kBltsize) = size;
	}

	/// Espera al ultimo objeto del lote.
	__attribute__((always_inline)) inline bool end() {
		wait();
		return true;
	}

private:
	/// Escribe el par PTH/PTL en una sola escritura de 32 bits (registros contiguos alineados
	/// a 4 bytes; el build usa `-fno-strict-aliasing`).
	__attribute__((always_inline)) inline void write_ptr(eng::u16 word_index,
							     eng::graphics::BlitPtr p) const {
		*reinterpret_cast<volatile eng::u32*>(&regs_.word(word_index)) =
			static_cast<eng::u32>(p.addr.value);
	}

	/// BBUSY (DMACONR bit 14): `btst` sobre la palabra completa. Con servicio de fondo, lo drena
	/// en cada vuelta (mismo patron que `AmigaBackend::wait_blitter`).
	__attribute__((always_inline)) inline void wait() const {
		if (wait_fn_ == nullptr) {
			while ((regs_.word(kDmaconr) & 0x4000u) != 0u) {
			}
			return;
		}
		while ((regs_.word(kDmaconr) & 0x4000u) != 0u) {
			const eng::u32 vposr = *reinterpret_cast<volatile eng::u32*>(&regs_.word(kVposr));
			wait_fn_(wait_user_, static_cast<eng::u16>((vposr & 0x1ff00u) >> 8u));
		}
	}

	static constexpr eng::u16 kDmaconr = 0x002u / 2u;
	static constexpr eng::u16 kVposr = 0x004u / 2u;
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
	static constexpr eng::u16 kUseA = eng::graphics::kBlitterUseA;
	static constexpr eng::u16 kUseB = eng::graphics::kBlitterUseB;
	static constexpr eng::u16 kUseD = eng::graphics::kBlitterUseD;
	static constexpr eng::u16 kMintermAOrB = eng::graphics::kBlitterMintermAOrB;

	HwRegs regs_ {};            ///< Bloque de registros del hardware donde se programa el lote (en Amiga, `$DFF000` fijo; en tests, el mock de `HwRegs::for_test`).
	eng::u16 con0 = 0;          ///< BLTCON0 del lote (A|B|D | minterm OR) sin el desplazamiento fino `ASH`, que añade cada `one`.
	eng::u16 size = 0;          ///< BLTSIZE común del lote: alto (filas físicas) en bits 15-6 y ancho (palabras) en 5-0.
	WaitFn wait_fn_ = nullptr;  ///< Servicio de espera opcional (ver `WaitFn`); `nullptr` = sondeo apretado sin drenar fondo.
	void* wait_user_ = nullptr; ///< Contexto **opaco** del servicio: lo posee quien registró `wait_fn_`; el lote solo lo devuelve tal cual en cada llamada, nunca lo interpreta. No es memoria DMA ni del lote.
};

} // namespace eng::amiga
