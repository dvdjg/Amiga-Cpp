#pragma once

/// \file mem_probe.hpp
/// **Sonda de reservas de memoria** (`eng::debug::g_mem_probe`): bloque `volatile` de forma fija
/// que un host puede leer por el canal lateral del emulador (o GDB) sin recompilar la demo.
///
/// Responde a las preguntas que un `mark_failed` no contesta: **qué banco** se quedó sin hueco,
/// **por qué** (`MemBank::Status`), **cuántos slots** quedaban y **qué tamaño** se pidió. Nace de
/// la investigación del fallo release de la 213 (`0x21305`): el `detail` codificaba a mano lo que
/// este bloque ofrece tipado y permanente.
///
/// Coste: cero en el frame caliente — la sonda solo escribe en **reservas fallidas** y, si el
/// llamador lo pide, en un refresco explícito. No sustituye a la telemetría de frame
/// (`telemetry.hpp`), que mide uso; esto diagnostica **fallos**.
///
/// Uso típico en un punto de reserva:
///
/// ```cpp
/// const auto block = bank.reserve<Tag>(bytes, align);
/// if (!block.valid()) {
///     eng::debug::record_mem_failure(bank.kind(), bytes, bank);
/// }
/// ```
///
/// El host lo localiza por el **símbolo del `.map`** (`g_mem_probe`, manglado en C++) o por el
/// **magic** (`'MEM\0'`) alineado a 4, igual que `profile.mjs` con `g_eng_prof`.

#include <eng/core/types/memory_kind.hpp>
#include <eng/core/types/types.hpp>

namespace eng::debug {

/// Magic del bloque ('M','E','M',0): el 4.º byte nulo distingue el bloque de cualquier otro
/// `u32` con texto, y el host puede verificar que leyó la dirección correcta.
inline constexpr eng::u32 mem_probe_magic = 0x4d454d00u; // 'MEM\0'

/// Nº de bancos que se distinguen (Chip, Slow, Fast): índice por `mem_probe_index`.
inline constexpr eng::u8 mem_probe_banks = 3u;

/// Estado de un banco en el momento del sondeo (espejo de `ArenaSnapshot` + `MemBank::Status`).
struct MemBankProbe {
	eng::u32 capacity = 0u;   ///< bytes totales del banco
	eng::u32 used = 0u;       ///< bytes útiles vivos
	eng::u32 free_bytes = 0u; ///< suma de huecos libres
	eng::u32 peak = 0u;       ///< pico histórico de `used`
	eng::u32 blocks = 0u;     ///< slots que distinguía el pool
	eng::u32 status = 0u;     ///< `MemBank::Status` (0 Ok, 1 BankAbsent, 2 NoSpace, 3 Fragmented)
};

/// Último fallo de reserva de un banco.
struct MemFailureProbe {
	eng::u32 requested = 0u; ///< bytes que se pidieron (0 = ningún fallo registrado)
	eng::u32 status = 0u;    ///< `MemBank::Status` en el momento del fallo
	eng::u16 tag = 0u;       ///< identidad cualitativa del dominio (0 = sin tag)
	eng::u16 pad = 0u;
};

/// Bloque de sonda (contrato binario de tamaño fijo). `volatile` e `inline`.
struct MemProbeBlock {
	eng::u32 magic;      ///< `mem_probe_magic`
	eng::u32 generation; ///< se incrementa en cada refresco/fallo
	eng::u8 bank_count;  ///< bancos sondeados
	eng::u8 failures_valid; ///< 1 si algún `failures[i].requested != 0`
	eng::u8 pad[2];
	MemBankProbe banks[mem_probe_banks];
	MemFailureProbe failures[mem_probe_banks];
};

inline volatile MemProbeBlock g_mem_probe {
	mem_probe_magic, 0u, 0u, 0u, {}, {{}, {}, {}}, {{}, {}, {}}};

/// Índice del banco en el bloque a partir del `MemoryKind` (Chip=0, Slow=1, Fast=2).
[[nodiscard]] constexpr eng::u8 mem_probe_index(eng::MemoryKind k) noexcept {
	switch (k) {
		case eng::MemoryKind::Slow: return 1u;
		case eng::MemoryKind::Fast: return 2u;
		case eng::MemoryKind::Chip:
		default: return 0u;
	}
}

/// Escribe el estado que el llamador ya leyó del banco (`snapshot()` + `status()` + `block_count`).
/// Sin dependencia de `MemBank` (evita el ciclo de includes); el llamador tiene los tipos.
inline void refresh_mem_probe(eng::MemoryKind k, eng::u32 capacity, eng::u32 used,
			      eng::u32 free_bytes, eng::u32 peak, eng::u32 blocks,
			      eng::u32 status) noexcept {
	const eng::u8 i = mem_probe_index(k);
	if (i >= mem_probe_banks) {
		return;
	}
	g_mem_probe.banks[i].capacity = capacity;
	g_mem_probe.banks[i].used = used;
	g_mem_probe.banks[i].free_bytes = free_bytes;
	g_mem_probe.banks[i].peak = peak;
	g_mem_probe.banks[i].blocks = blocks;
	g_mem_probe.banks[i].status = status;
	g_mem_probe.bank_count = mem_probe_banks;
	g_mem_probe.generation = g_mem_probe.generation + 1u;
}

/// Registra un fallo de reserva: **qué** se pidió y **por qué** falló.
inline void record_mem_failure(eng::MemoryKind k, eng::u32 requested, eng::u32 status,
			       eng::u16 tag = 0u) noexcept {
	const eng::u8 i = mem_probe_index(k);
	if (i >= mem_probe_banks) {
		return;
	}
	g_mem_probe.failures[i].requested = requested;
	g_mem_probe.failures[i].status = status;
	g_mem_probe.failures[i].tag = tag;
	g_mem_probe.failures_valid = 1u;
	g_mem_probe.generation = g_mem_probe.generation + 1u;
}

} // namespace eng::debug

#include <eng/memory/memory_manager.hpp>

namespace eng::debug {

/// Refresca la sonda con el estado de **todos** los bancos del `MemoryManager` (Chip/Slow/Fast).
/// Punto único para el setup: el llamador no necesita conocer la forma interna del bloque.
inline void probe_memory_banks(eng::MemoryManager& mm) noexcept {
	const auto& chip = mm.chip();
	const auto cs = chip.snapshot();
	refresh_mem_probe(eng::MemoryKind::Chip, cs.capacity, cs.used, cs.remaining, cs.peak,
			  chip.block_count(), static_cast<eng::u32>(chip.status()));
	const auto& slow = mm.slow();
	const auto ss = slow.snapshot();
	refresh_mem_probe(eng::MemoryKind::Slow, ss.capacity, ss.used, ss.remaining, ss.peak,
			  slow.block_count(), static_cast<eng::u32>(slow.status()));
	const auto& fast = mm.fast();
	const auto fs = fast.snapshot();
	refresh_mem_probe(eng::MemoryKind::Fast, fs.capacity, fs.used, fs.remaining, fs.peak,
			  fast.block_count(), static_cast<eng::u32>(fast.status()));
}

} // namespace eng::debug
