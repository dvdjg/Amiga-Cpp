#pragma once

/// \file behavior_tree.hpp
/// **Árboles de comportamiento** (`eng::ai`) sin heap: nodos en un array de capacidad
/// fija y hojas como `FunctionRef<BtStatus()>`. Soporta los dos composites clásicos:
///
/// - **Secuencia**: ejecuta los hijos en orden y falla en el primero que falle; si
///   todos tienen éxito, tiene éxito.
/// - **Selector**: prueba los hijos en orden y tiene éxito en el primero que lo logre;
///   si todos fallan, falla.
///
/// Las hojas deben devolver `Success`/`Failure` (no hay estado `Running`: el tick es
/// síncrono y acotado, sin heap). Construcción: primero las hojas y luego los
/// composites que las referencian por rango contiguo `[first_child, +child_count)`.
///
/// Uso:
///   eng::ai::BehaviorTree<6> bt;
///   const auto has_ammo = bt.add_leaf(has_ammo_fn);
///   const auto fire     = bt.add_leaf(fire_fn);
///   const auto shoot    = bt.add_sequence(has_ammo, 2);      // has_ammo -> fire
///   const auto reload   = bt.add_leaf(reload_fn);
///   const auto root     = bt.add_selector(shoot, 2);         // shoot | reload
///   bt.set_root(root);
///   bt.tick();
///
/// ```text
///   Selector(root)                       Nodos en array (capacidad fija, SIN heap)
///   ├─ Sequence(shoot)                   ┌─────┬──────────┬─────────────┬────────────┐
///   │    ├─ has_ammo  (hoja)             │ idx │ kind     │ first_child │ child_count│
///   │    └─ fire      (hoja)             ├─────┼──────────┼─────────────┼────────────┤
///   └─ reload        (hoja)              │  …  │ Leaf/Seq/│  rango contiguo de hijos   │
///                                        │     │ Selector │                            │
///   tick() = run(root) recursivo         └─────┴──────────┴─────────────┴────────────┘
///   Sequence: falla al 1er hijo que falla          hojas = FunctionRef<BtStatus()>
///   Selector: acierta al 1er hijo que acierta
/// ```
///
/// Referencia y encaje: `docs/engine/architecture/GAME_AI_LIBRARY.md`.
/// Verificación: HOST-113.

#include <eng/core/types/types.hpp>
#include <eng/core/util/function_ref.hpp>

namespace eng::ai {

enum class BtStatus : eng::u8 { Success, Failure };

using BtTask = eng::util::FunctionRef<BtStatus()>;

template <eng::usize MaxNodes>
class BehaviorTree {
	static_assert(MaxNodes > 0u, "BehaviorTree: MaxNodes debe ser mayor que 0");

public:
	static constexpr eng::u16 no_node = 0xffffu;

	/// Añade una hoja; devuelve su índice o `no_node` si no cabe.
	[[nodiscard]] constexpr eng::u16 add_leaf(BtTask task) noexcept {
		if (m_count >= MaxNodes) {
			return no_node;
		}
		m_nodes[m_count] = Node {Kind::Leaf, task, 0u, 0u};
		return static_cast<eng::u16>(m_count++);
	}

	/// Añade una secuencia sobre los hijos `[first_child, first_child + count)`.
	[[nodiscard]] constexpr eng::u16 add_sequence(eng::u16 first_child,
						      eng::u16 count) noexcept {
		return add_composite(Kind::Sequence, first_child, count);
	}

	/// Añade un selector sobre los hijos `[first_child, first_child + count)`.
	[[nodiscard]] constexpr eng::u16 add_selector(eng::u16 first_child,
						      eng::u16 count) noexcept {
		return add_composite(Kind::Selector, first_child, count);
	}

	constexpr void set_root(eng::u16 root) noexcept { m_root = root; }
	[[nodiscard]] constexpr eng::usize node_count() const noexcept { return m_count; }

	/// Evalúa el árbol desde la raíz (un árbol vacío o sin raíz falla).
	[[nodiscard]] BtStatus tick() const noexcept {
		return m_root == no_node ? BtStatus::Failure : run(m_root);
	}

private:
	enum class Kind : eng::u8 { Leaf, Sequence, Selector };

	struct Node {
		Kind kind;
		BtTask task;
		eng::u16 first_child;
		eng::u16 child_count;
	};

	/// Registra un nodo **compuesto** (`Sequence`/`Selector`) sobre los `count` hijos
	/// contiguos desde `first_child`. Devuelve su índice, o `no_node` si no cabe.
	[[nodiscard]] constexpr eng::u16 add_composite(Kind kind, eng::u16 first_child,
						       eng::u16 count) noexcept {
		if (m_count >= MaxNodes) {
			return no_node;
		}
		m_nodes[m_count] = Node {kind, BtTask {}, first_child, count};
		return static_cast<eng::u16>(m_count++);
	}

	/// Evalúa el subárbol del nodo `index` (recursivo, sin heap): hoja → su tarea; `Sequence`
	/// falla al primer hijo que falla; `Selector` acierta al primero que acierta. Lo llama el
	/// agente una vez por tick.
	[[nodiscard]] BtStatus run(eng::u16 index) const noexcept {
		if (index >= m_count) {
			return BtStatus::Failure;
		}
		const Node& node = m_nodes[index];
		switch (node.kind) {
		case Kind::Leaf:
			return node.task();
		case Kind::Sequence:
			for (eng::u16 i = 0u; i < node.child_count; ++i) {
				if (run(static_cast<eng::u16>(node.first_child + i)) ==
				    BtStatus::Failure) {
					return BtStatus::Failure;
				}
			}
			return BtStatus::Success;
		case Kind::Selector:
			for (eng::u16 i = 0u; i < node.child_count; ++i) {
				if (run(static_cast<eng::u16>(node.first_child + i)) ==
				    BtStatus::Success) {
					return BtStatus::Success;
				}
			}
			return BtStatus::Failure;
		}
		return BtStatus::Failure;
	}

	Node m_nodes[MaxNodes] {};
	eng::usize m_count = 0u;
	eng::u16 m_root = no_node;
};

} // namespace eng::ai
