#pragma once

/// \file request.hpp
/// **Peticiones de E/S con generación** (`ROADMAP_RESOURCES.md` R6.2): separa la identidad de la
/// petición (`RequestId` = slot + generación) del `IoUser`/asset, de modo que la **respuesta tardía**
/// de una operación antigua **no** complete el slot de una operación nueva que lo reutilizó.
///
/// Es **puro** (sin E/S, host-testable): una tabla de slots con `acquire`/`complete`/`cancel` y el
/// contador de generación por slot. El VFS/loader lo usará para validar cada `FileDone` antes de
/// escribir su buffer; el `IoUser` sigue llevando `tag` + `id` para el enrutado, y el `RequestId`
/// viaja aparte (en el estado del consumidor).

#include <eng/core/types/types.hpp>

namespace eng::os {

/// Identidad de una petición: **slot** de la tabla + **generación** (y un `kind` libre para el uso).
struct RequestId {
	eng::u16 slot = 0u;
	eng::u8 gen = 0u;
	eng::u8 kind = 0u; ///< libre para el consumidor (p. ej. 'A'/'L'/'S')
	[[nodiscard]] constexpr bool valid() const noexcept { return slot != kInvalidSlot; }
	static constexpr eng::u16 kInvalidSlot = 0xffffu;
};

/// Tabla de peticiones de capacidad fija (sin heap, sin E/S): abre, valida por generación, completa
/// y cancela. `MaxSlots` slots reutilizables; cada reutilización **incrementa** la generación.
template <eng::u16 MaxSlots = 16u>
class RequestTable {
public:
	/// Abre una petición: reutiliza un slot libre (o no usado) con la generación siguiente.
	/// `RequestId{}` (inválido) si la tabla está llena.
	[[nodiscard]] RequestId acquire(eng::u8 kind = 0u) noexcept {
		for (eng::u16 i = 0u; i < MaxSlots; ++i) {
			if (!m_slots[i].pending) {
				m_slots[i].gen = static_cast<eng::u8>(m_slots[i].gen + 1u);
				m_slots[i].pending = true;
				return RequestId {i, m_slots[i].gen, kind};
			}
		}
		return RequestId {RequestId::kInvalidSlot, 0u, 0u};
	}

	/// ¿Sigue **viva** la petición? Falso si se canceló/completó o si la generación no coincide
	/// (respuesta **tardía** de un uso anterior del slot).
	[[nodiscard]] bool alive(RequestId r) const noexcept {
		return r.valid() && r.slot < MaxSlots && m_slots[r.slot].pending &&
		       m_slots[r.slot].gen == r.gen;
	}

	/// Completa la petición y libera el slot. Devuelve `false` (y **no** libera nada) si llega
	/// tarde/duplicada: una respuesta vieja no puede cerrar el slot de otra operación.
	[[nodiscard]] bool complete(RequestId r) noexcept {
		if (!alive(r)) {
			return false;
		}
		m_slots[r.slot].pending = false;
		return true;
	}

	/// **Cancela** una petición en vuelo (marca el slot libre; una respuesta posterior se rechaza).
	[[nodiscard]] bool cancel(RequestId r) noexcept { return complete(r); }

	/// Nº de peticiones vivas (para telemetría/presupuesto).
	[[nodiscard]] eng::u16 pending_count() const noexcept {
		eng::u16 n = 0u;
		for (eng::u16 i = 0u; i < MaxSlots; ++i) {
			if (m_slots[i].pending) {
				++n;
			}
		}
		return n;
	}

private:
	struct Slot {
		eng::u8 gen = 0u;
		bool pending = false;
	};
	Slot m_slots[MaxSlots] {};
};

} // namespace eng::os
