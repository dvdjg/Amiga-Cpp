#pragma once

/// \file multi_buffered.hpp
/// **N buffers de display del mismo driver** (planos + copperlist por buffer) con swap
/// de copperlist tras VBlank. Es el patrón que 061/080/082/083 repetían a mano con dos
/// instancias del driver y un índice `active`/`show`.
///
/// El swap es solo `COP1LC` (`MinimalBackend::install_copper_list`), sin `COPJMP1`: el
/// Copper recarga al comienzo del siguiente VBlank, así que el display nunca muestra el
/// buffer que se está escribiendo. Por eso `commit` debe llamarse **tras VBlank** (o
/// desde la IRQ de blit cuando el C2P está encadenado, como en la 080).
///
/// Separación de responsabilidades: el wrapper es dueño de la **memoria** (N bloques de
/// planos + N de copperlist) y el driver solo **emite** su lista sobre esos bloques
/// (`Driver::bind`). Así el mismo driver sirve con 1, 2 o 3 buffers sin tocar su código.
///
/// `N = 1` equivale a un driver suelto: no hay flip efectivo (el llamador dibuja siempre
/// en el mismo buffer; para evitar tearing sin flip se usan márgenes ocultos, como hacen
/// los drivers de scroll).
///
/// Casos de uso:
///   - Efectos que **reescriben lo visible** cada frame (rotozoom, fuego, plasma): `N = 2`.
///   - Encadenado por IRQ de blit (080): `N = 2`, `commit` en la IRQ al completar el C2P.
///   - `N = 3` solo si el productor tarda > 1 campo y se quiere desacoplar; añade un
///     frame de latencia y `planes·bytes_por_fila·alto` de Chip RAM por buffer.

#include <eng/core/domains.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/array.hpp>
#include <eng/memory/arena.hpp>

namespace eng::graphics::drivers {

template <class Driver, u8 N>
class MultiBuffered {
	static_assert(N >= 1u, "N debe ser >= 1");

public:
	static constexpr u8 count() { return N; }

	/// Reserva N buffers (planos + copperlist por slot) y enlaza cada driver a los
	/// suyos. El llamador ya ha reservado la arena Chip del backend.
	///
	/// Un driver **sin bitplanes** (p. ej. `CopperChunkyScene`, cuyo buffer es la propia
	/// copperlist) declara `bitplane_bytes_for == 0` y no se le reserva bloque de planos.
	template <class Config>
	bool init(eng::MemorySystem& memory, const Config& config) {
		const u32 plane_bytes = Driver::bitplane_bytes_for(config);
		for (u8 i = 0; i < N; ++i) {
			if (plane_bytes != 0u) {
				m_planes[i] = memory.chip.allocate_block<eng::PlaneTag>(plane_bytes, 16);
			}
			m_copper[i] = memory.chip.allocate_block<eng::CopperTag>(config.copper_bytes, 16);
			if (!m_driver[i].bind(m_planes[i], m_copper[i], config)) {
				return false;
			}
		}
		// Con N>1 se arranca escribiendo en el slot 1: el 0 es el que se muestra tras
		// el takeover, así ni el primer frame se dibuja sobre lo visible.
		m_back = (N > 1u) ? 1u : 0u;
		return true;
	}

	/// Toma el control del display mostrando el slot 0 (una sola vez).
	template <class Backend>
	void takeover(Backend& backend) const {
		m_driver[0].takeover(backend);
	}

	/// Publica la copperlist del buffer en escritura (el que acaba de convertirse) y
	/// pasa al siguiente. Llamar tras VBlank, o desde la IRQ que completa el C2P.
	template <class Backend>
	void commit(Backend& backend) {
		m_driver[m_back].install(backend);
		m_back = static_cast<u8>((m_back + 1u == N) ? 0u : (m_back + 1u));
	}

	/// Driver del buffer en escritura (el llamador dibuja aquí).
	Driver& back() { return m_driver[m_back]; }
	const Driver& back() const { return m_driver[m_back]; }
	u8 back_slot() const { return m_back; }

	/// Driver de un slot concreto (p. ej. para leer sus planos o su informe).
	Driver& slot(u8 i) { return m_driver[i % N]; }
	const Driver& slot(u8 i) const { return m_driver[i % N]; }

private:
	eng::util::Array<Driver, N> m_driver {};
	eng::util::Array<eng::Block<eng::PlaneTag>, N> m_planes {};
	eng::util::Array<eng::Block<eng::CopperTag>, N> m_copper {};
	u8 m_back = 0;
};

} // namespace eng::graphics::drivers
