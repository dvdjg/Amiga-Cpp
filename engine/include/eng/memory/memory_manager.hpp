#pragma once

/// \file memory_manager.hpp
/// **Bancos de memoria del engine** (`eng::MemoryManager`): reúne los **bancos tipados**
/// `MemBank<Chip>`/`<Slow>`/`<Fast>`. **No** hay `MemoryKind` en runtime: cada reserva sale de un
/// banco concreto, así que `reserve`/`release`/`free_bytes` **no hacen `switch`** ni llevan el
/// medio como argumento — el banco es un **tag de plantilla** (`MemoryKind`).
///
/// El banco de cada uso es una decisión de **setup** (qué buffers entrega el backend): en un A500
/// sin Fast/Slow, esos bancos quedan con 0 bytes y sus reservas devuelven bloques inválidos. Un
/// uso «general» se resuelve eligiendo el banco en el código (compile-time), no con dispatch.
///
/// ```cpp
/// eng::MemoryManager mem;
/// mem.configure(chip_base, chip_bytes, slow_base, slow_bytes, fast_base, fast_bytes);
/// auto planes = mem.chip().reserve<eng::PlaneTag>(n);   // DMA -> Chip (tipado)
/// auto sim    = mem.fast().reserve<eng::SimTag>(m);     // CPU  -> Fast (tipado)
/// ```

#include <eng/core/types/memory_kind.hpp>
#include <eng/core/types/types.hpp>
#include <eng/memory/mem_bank.hpp>

namespace eng {

/// Reúne los tres bancos tipados. No hay API runtime con `MemoryKind`: se usa el banco concreto.
class MemoryManager {
public:
	/// Entrega los buffers por banco (del backend). `slow`/`fast` pueden ser nulos (A500 sin ellos).
	bool configure(void* chip, u32 chip_bytes, void* slow, u32 slow_bytes, void* fast,
		       u32 fast_bytes, u32 align = 16u) noexcept {
		m_chip.configure(chip, chip_bytes, align);
		m_slow.configure(slow, slow_bytes, align);
		m_fast.configure(fast, fast_bytes, align);
		m_configured = chip_bytes != 0u;
		return m_configured;
	}
	[[nodiscard]] constexpr bool configured() const noexcept { return m_configured; }

	/// Enlaza los bancos de Chip/Slow a las **arenas del `MemorySystem`** (mismo buffer y **mismo
	/// cursor**) en vez de a buffers propios, de modo que las arenas y los bancos **no se solapan**:
	/// hay **un único asignador por medio**. Fast no tiene arena (CPU-privada) y recibe su buffer.
	/// Es lo que usa `AmigaBackend::configure_memory`. Ver `INTERNAL_TYPE_SYSTEM.md` §3.6.
	bool configure_backing(LinearArena& chip, LinearArena& slow, void* fast, u32 fast_bytes,
			       u32 align = 16u) noexcept {
		m_chip.pool().configure_backing(chip);
		m_slow.pool().configure_backing(slow);
		m_fast.configure(fast, fast_bytes, align);
		m_configured = chip.capacity() != 0u;
		return m_configured;
	}

	/// **Bancos tipados** (tag de plantilla; sin `MemoryKind` como variable).
	[[nodiscard]] constexpr MemBank<MemoryKind::Chip>& chip() noexcept { return m_chip; }
	[[nodiscard]] constexpr MemBank<MemoryKind::Slow>& slow() noexcept { return m_slow; }
	[[nodiscard]] constexpr MemBank<MemoryKind::Fast>& fast() noexcept { return m_fast; }
	[[nodiscard]] constexpr const MemBank<MemoryKind::Chip>& chip() const noexcept {
		return m_chip;
	}
	[[nodiscard]] constexpr const MemBank<MemoryKind::Slow>& slow() const noexcept {
		return m_slow;
	}
	[[nodiscard]] constexpr const MemBank<MemoryKind::Fast>& fast() const noexcept {
		return m_fast;
	}

	[[nodiscard]] constexpr bool has_slow() const noexcept { return m_slow.capacity() != 0u; }
	[[nodiscard]] constexpr bool has_fast() const noexcept { return m_fast.capacity() != 0u; }

private:
	MemBank<MemoryKind::Chip> m_chip {};
	MemBank<MemoryKind::Slow> m_slow {};
	MemBank<MemoryKind::Fast> m_fast {};
	bool m_configured = false;
};

/// **Política de banco** de una reserva (Fase 7): unifica la elección de banco con **fallback
/// explícito**. El **banco efectivo** viaja en `Block::kind` (el medio es dato, no tipo), así que
/// un consumidor puede saber dónde cayó sin duplicar la decisión. DMA conserva `ChipRequired`.
enum class MemoryPolicy : u8 {
	ChipRequired,  ///< solo Chip (DMA): sin fallback.
	FastRequired,  ///< solo Fast (CPU): sin fallback; inválido si no hay Fast.
	FastPreferred, ///< Fast si hay, si no Slow (CPU, no DMA).
	AnyBank,       ///< Fast → Slow → Chip (cualquier RAM).
};

/// Reserva **CPU** (no DMA): **Fast si la hay, si no Slow**. Es la decisión de runtime para
/// buffers que la CPU procesa intensivamente (descompresión, simulación, pilas): el banco se
/// elige según disponibilidad. Devuelve `Block<Tag>` (el medio va como dato).
template <class Tag>
[[nodiscard]] inline Block<Tag> fast_or_slow(MemoryManager& mm, u32 bytes,
					     u32 alignment = 0u) noexcept {
	if (mm.has_fast()) {
		Block<Tag> b = mm.fast().reserve<Tag>(bytes, alignment);
		if (b.valid()) {
			return b;
		}
	}
	return Block<Tag> {mm.slow().reserve<Tag>(bytes, alignment)};
}

/// Reserva **en cualquier banco** (**Chip, Fast o Slow**): para buffers que NO son DMA pero
/// aceptan cualquier RAM (p. ej. los búferes de plugins del mixer: «any RAM type»). Prioriza
/// **Fast → Slow → Chip** (los bancos de CPU primero; Chip es el último recurso porque es el más
/// escaso y el que necesita el DMA). Devuelve `Block<Tag>` (el medio va como dato).
template <class Tag>
[[nodiscard]] inline Block<Tag> any_bank(MemoryManager& mm, u32 bytes,
					 u32 alignment = 0u) noexcept {
	if (mm.has_fast()) {
		Block<Tag> b = mm.fast().reserve<Tag>(bytes, alignment);
		if (b.valid()) {
			return b;
		}
	}
	if (mm.has_slow()) {
		Block<Tag> b = mm.slow().reserve<Tag>(bytes, alignment);
		if (b.valid()) {
			return b;
		}
	}
	if (mm.chip().capacity() != 0u) {
		return Block<Tag> {mm.chip().reserve<Tag>(bytes, alignment)};
	}
	return {};
}

/// Reserva según una **`MemoryPolicy`** nombrada (Fase 7), reutilizando los helpers de runtime
/// (`fast_or_slow`/`any_bank`) para no duplicar la elección. El banco efectivo queda en
/// `Block::kind`.
template <class Tag>
[[nodiscard]] inline Block<Tag> reserve(MemoryManager& mm, MemoryPolicy policy, u32 bytes,
					u32 alignment = 0u) noexcept {
	switch (policy) {
		case MemoryPolicy::ChipRequired:
			return Block<Tag> {mm.chip().reserve<Tag>(bytes, alignment)};
		case MemoryPolicy::FastRequired:
			return Block<Tag> {mm.fast().reserve<Tag>(bytes, alignment)};
		case MemoryPolicy::FastPreferred:
			return fast_or_slow<Tag>(mm, bytes, alignment);
		case MemoryPolicy::AnyBank:
			return any_bank<Tag>(mm, bytes, alignment);
	}
	return {};
}

} // namespace eng
