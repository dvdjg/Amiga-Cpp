#pragma once

/// \file scroll_layer.hpp
/// **Interfaz de capa de scroll conducida por `App`** (`eng::playfield::ScrollLayer<Backend>`): una
/// abstracción **C++** (sin `void*` ni punteros a función sueltos) con la que el `App` arranca y
/// conduce **cualquier** motor —el camino de tiras o el corcóscru XYLimited— por el mismo contrato.
///
/// La **memoria llega tipada**: `begin` recibe el `MemoryManager&`, que entrega `Block<Tag,
/// MemoryKind>` **tageados por banco** (Chip/Fast/Slow); el motor nunca ve un `void*` ni un banco sin
/// tipo. El **backend** llega por su tipo real (`Backend&`), no borrado.
///
/// Un motor se adapta **heredando** de esta interfaz (o con un adaptador como
/// `playfield::XlimitedScrollLayer`) y sobreescribiendo:
///   - `begin(memory, backend)`: reserva, pinta, compone y **toma el display** (una vez).
///   - `frame(backend)`: conduce un frame (lee la cámara del juego, blitea lo que cambia, parchea el Copper).
///
/// El `App` la conduce por frame (una llamada virtual por capa y frame: despreciable, no está en el
/// lazo por píxel); el juego no ve el compositor, los buffers ni el backend.

#include <eng/core/types/types.hpp>
#include <eng/field/playfield_base.hpp> // `PlayfieldHardwareView` (vista que expone la capa)
#include <eng/memory/memory_manager.hpp>

namespace eng::playfield {

/// Contrato de una capa de scroll que el `App` arranca y conduce. `Backend` es el backend concreto
/// (el `App` ya está parametrizado por él), así que la interfaz es **no borrada**.
template <class Backend>
class ScrollLayer {
public:
	constexpr ScrollLayer() noexcept = default;
	ScrollLayer(const ScrollLayer&) = delete;
	ScrollLayer& operator=(const ScrollLayer&) = delete;

	/// **Arranque** (una vez): reserva en `memory` (bloques **tageados** por banco), prepara el
	/// contenido y toma el display en `backend`. `false` si no cabe o algo falla.
	[[nodiscard]] virtual bool begin(MemoryManager& memory, Backend& backend) = 0;

	/// **Un frame**: conduce la capa (cámara del juego → blit de lo que cambia → Copper).
	virtual void frame(Backend& backend) = 0;

	/// **Número de vistas de banda** que la capa aporta a la composición del `App` (planner §7):
	/// `0` = la capa compone su propio copperlist (camino de tiras); `1` = un campo (`Single`/`Bands`);
	/// `2` = dual playfield (`Dpf`). Con estas vistas el `App` **deriva el `RasterLayout`** del plan
	/// (`App::scene_layout`) sin que el juego monte el layout a mano.
	[[nodiscard]] virtual eng::u8 band_view_count() const noexcept { return 0u; }

	/// **Vista de hardware** de la banda `i` (`i < band_view_count()`), en orden PF1→PF2. El `App`
	/// la usa para `plan_raster_layout`; el juego no manipula planos. Devuelve una vista vacía si la
	/// capa no la expone.
	[[nodiscard]] virtual PlayfieldHardwareView band_view(eng::u8 i) const noexcept {
		(void)i;
		return {};
	}

protected:
	/// Destructor **no virtual y protegido**: la capa la **posee el juego** y **no se borra** por el
	/// puntero a la base (el `App` solo la conduce). Un dtor virtual arrastraría el *deleting
	/// destructor* → `operator delete` (y el engine es freestanding, sin heap).
	~ScrollLayer() = default;
};

} // namespace eng::playfield
