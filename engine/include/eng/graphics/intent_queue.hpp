#pragma once

/// \file intent_queue.hpp
/// **Vocabulario de dibujo** del planner (capa 2): el tipo de intención (`DrawIntent`), su cola
/// (`DrawQueue`) y la **receta precompilada** (`DrawRecipe`). El **mecanismo** de la cola
/// (no bloqueante + completación) es genérico y vive en `eng/core/util/intent_queue.hpp`; aquí solo está
/// lo específico de gráficos. El planner completo: `docs/engine/architecture/INTENT_PLANNER.md`.

#include <eng/core/types/ptr.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/intent_queue.hpp>
#include <eng/graphics/bob.hpp>
#include <eng/graphics/raster_intent.hpp>

namespace eng::graphics {

/// Asset de sprite (definido en `sprite_asset.hpp`): la intención lo referencia sin depender de él.
class Sprite;

/// Tipo de dibujo pedido. **El juego no elige minterm ni canal**: pide qué quiere.
enum class DrawKind : eng::u8 {
	Rect,   ///< relleno `(x,y,w,h,color)`
	Line,   ///< trazo `(x,y)->(x2,y2,color)`
	Sprite, ///< `(x,y,frame)`
};

/// Una intención de dibujo (valor pequeño y copiable). El planner la compila a `BlitOp`.
struct DrawIntent {
	DrawKind kind = DrawKind::Rect;
	eng::s16 x = 0;
	eng::s16 y = 0;
	eng::u16 w = 0;    ///< Rect
	eng::u16 h = 0;    ///< Rect
	eng::s16 x2 = 0;   ///< Line
	eng::s16 y2 = 0;   ///< Line
	eng::u8 color = 0; ///< Rect/Line
	eng::u8 frame = 0; ///< Sprite
	/// **Política** de la intención (el juego pide CÓMO, sin nombrar el chipset): transparencia
	/// (`BobDraw`) y fondo (`BobErase`).
	BobDraw draw = BobDraw::Or;
	BobErase erase = BobErase::None;
	eng::Ref<const Sprite> sheet {}; ///< `Sprite`: el asset (no propietario; solo `Sprite`)
	/// **Slot de objeto** (0..N-1): el ejecutor lo usa para recordar el rectángulo previo por
	/// objeto (borrado `ClearRect` / save-under `RestoreUnder`). `DrawLayer::emit` lo rellena.
	eng::u8 id = 0;
	/// **Necesidades de Copper** del objeto (líneas RELATIVAS a su Y): el ejecutor las ancla a la
	/// línea del objeto en el `copper::Plan`. Vacío = sin copper propio.
	eng::Span<const CopperIntent> copper {};
};

/// La cola de dibujo del juego: `Item = DrawIntent` (mecanismo genérico de `eng/core/util`).
template <eng::u16 N, class Executor, class Done>
using DrawQueue = eng::IntentQueue<N, DrawIntent, Executor, Done>;

/// **Receta de dibujo precompilada** (setup): una lista FIJA de intenciones que el juego describe
/// **una vez** (en el setup de la escena) y el bucle **reproduce** por frame con trabajo mínimo.
/// Es la aplicación de la regla de coste: lo invariante se resuelve fuera del bucle; el frame solo
/// recorre la receta (sin asignación, sin dispatch). Ver `CODING_STYLE.md` y `INTENT_PLANNER.md` §4.1.
template <eng::u16 N>
class DrawRecipe {
public:
	constexpr void clear() noexcept { m_count = 0u; }
	[[nodiscard]] constexpr eng::u16 count() const noexcept { return m_count; }
	[[nodiscard]] constexpr const DrawIntent& operator[](eng::u16 i) const noexcept {
		return m_items[i];
	}

	/// Añade una intención fija (setup). `false` si la receta está llena.
	/// \param item  intención de dibujo.
	/// \return `false` si la receta está llena.
	constexpr bool add(const DrawIntent& item) noexcept {
		if (m_count >= N) {
			return false;
		}
		m_items[m_count++] = item;
		return true;
	}

	/// **Reproduce** la receta en la cola (una vez por frame): recorrido mínimo, sin bloquear.
	/// \param queue  cola destino (recibe cada intención por `enqueue`).
	template <class Queue>
	void emit(Queue& queue) const noexcept {
		for (eng::u16 i = 0u; i < m_count; ++i) {
			queue.enqueue(m_items[i]);
		}
	}

private:
	DrawIntent m_items[N] {};
	eng::u16 m_count = 0u;
};

} // namespace eng::graphics
