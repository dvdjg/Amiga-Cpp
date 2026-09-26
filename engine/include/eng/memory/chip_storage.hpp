#pragma once

/// \file chip_storage.hpp
/// **Búfer estático certificado en Chip RAM** y su dirección tipada.
///
/// En Amiga, un array global puede acabar en `.data`/`.bss` (que el cargador coloca donde
/// pueda) y **no** ser alcanzable por DMA. Para que Agnus lo vea (bitplanes, copper, sprites,
/// audio, blitter) la tabla debe vivir en Chip RAM: se declara con `ENG_CHIP_RAM`, que la pone
/// en la sección `.MEMF_CHIP` (el linker la asigna a Chip; ver `vscode-amiga-debug/template/main.c`).
///
/// `ChipStorage<Tag, Bytes>` une las dos cosas: **coloca** el búfer y **certifica** su
/// procedencia, entregando una `Address<MemoryKind::Chip>` (no hay que inventar un `cast`).
/// Para datos que vienen de un fichero, `INCBIN_CHIP` (support/gcc8_c_support.h) incrusta el
/// binario en `.INCBIN.MEMF_CHIP` con la misma garantía.
///
/// ```cpp
/// ENG_CHIP_RAM eng::ChipStorage<eng::CopperTag, 4096> g_copper;
/// eng::Scheduler sched { g_copper.view() };
/// eng::Address<eng::MemoryKind::Chip> dma = g_copper.address();   // solo esto es DMA
/// ```
///
/// En host u otros targets (sin Chip RAM) `ENG_CHIP_RAM` no tiene efecto: no hay mapa de memoria
/// Amiga; el tipo sigue siendo útil para comprobar la API, pero la garantía física es del target.
/// `ENG_CHIP_RAM` se activa con el macro de proyecto `ENG_AMIGA` (lo define el build de Amiga):
/// **no** basta `__m68k__`, porque Mega Drive y Atari ST también son m68k y no tienen Chip RAM.

#include <eng/core/types/memory_kind.hpp>
#include <eng/core/types/typed.hpp>
#include <eng/core/types/types.hpp>

#if defined(ENG_AMIGA)
/// Coloca la variable en la sección `.MEMF_CHIP` (Chip RAM). Solo Amiga: Mega Drive y Atari ST
/// son m68k pero **no** tienen el concepto de Chip RAM, así que `__m68k__` no basta.
#define ENG_CHIP_RAM __attribute__((section(".MEMF_CHIP")))
#else
/// En host u otros targets (sin Chip RAM) no tiene efecto.
#define ENG_CHIP_RAM
#endif

namespace eng {

/// Búfer **estático en Chip RAM** con vista de dominio y dirección DMA.
///
/// Debe declararse como global con `ENG_CHIP_RAM` (el atributo de sección no puede ir en un
/// miembro). `view()`/`address()` existen para que el resto del código no vuelva a tocar
/// punteros crudos ni fabrique un `Address<Chip>` a mano.
template <class Tag, u32 Capacity>
struct ChipStorage {
	static_assert(Capacity >= 1u, "ChipStorage: tamano no nulo");
	alignas(2) u8 storage[Capacity] {};

	/// El **bloque estándar** (`Block<Tag, MemoryKind::Chip>`): reúne la vista y la dirección DMA.
	/// Así no se duplica la API del handle; el resto usa `block().view`/`block().address()`.
	[[nodiscard]] constexpr Block<Tag, MemoryKind::Chip> block() noexcept {
		return Block<Tag, MemoryKind::Chip> {Bytes<Tag> {storage, static_cast<usize>(Capacity)},
						     MemoryKind::Chip};
	}
	/// Dirección DMA-visible de Chip RAM (la procedencia la garantiza `ENG_CHIP_RAM`).
	[[nodiscard]] constexpr Address<MemoryKind::Chip> address() noexcept { return block().address(); }
	/// Vista mutable con el tag de dominio de su contenido.
	[[nodiscard]] constexpr Bytes<Tag> view() noexcept { return block().view; }
};

} // namespace eng
