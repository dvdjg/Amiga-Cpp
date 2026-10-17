#pragma once

/// \file craft.hpp
/// **Crafteo** (`eng::rpg`, hito M2): recetas declarativas sobre el inventario. Una receta
/// pide **ingredientes por tipo exacto o por capacidades** (etiquetas), exige una **estación**
/// (máscara de etiquetas: yunque, horno, mesa…) y un **nivel de habilidad** del actor, y
/// produce un objeto nuevo (instancia recién creada con durabilidad completa).
///
/// El engine no conoce "forja élfica": conoce tipos, etiquetas y estaciones. El libro de
/// recetas (`RecipeBook`) referencia la tabla de ingredientes por rango (patrón
/// `mod_first/mod_count`): contenido en ROM, sin punteros ni heap. Diseño: `RPG_LAYER.md` §5.2.
///
/// Transaccionalidad: se valida todo antes de tocar nada y **la instancia de salida se crea
/// antes de consumir**: si el pool está lleno o el ingrediente falta, el inventario queda
/// intacto.
///
/// Verificación: HOST-460. NO VERIFICADA por demo (vertical slice en el roadmap, M6).

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/rpg/inventory.hpp>
#include <eng/rpg/items.hpp>

namespace eng::rpg {

/// Ingrediente de receta: `type` exacto **o** capacidades (`tags`); `qty` unidades. Con
/// `type = no_item_type` y `tags != 0` vale cualquier objeto con esas etiquetas (p. ej. "2
/// de cualquier material").
struct Ingredient {
	ItemTypeId type = no_item_type; ///< tipo exacto exigido (no_item_type = cualquiera)
	eng::u16 tags = 0u;             ///< capacidades exigidas del tipo (subconjunto)
	eng::u8 qty = 1u;               ///< unidades necesarias
};

/// Receta: estación, ingredientes (rango en la tabla del libro), salida y habilidad.
struct Recipe {
	eng::u16 station_tags = 0u;   ///< etiquetas que debe tener la estación (0 = en cualquier sitio)
	ItemTypeId output = no_item_type; ///< tipo producido
	eng::u8 output_qty = 1u;      ///< unidades producidas
	eng::u8 skill_req = 0u;       ///< habilidad mínima del actor (el juego la aporta)
	eng::u8 input_first = 0u;     ///< primer índice en la tabla de ingredientes
	eng::u8 input_count = 0u;     ///< nº de ingredientes
};

/// Libro de recetas: vistas sobre las tablas del juego (contenido en ROM).
struct RecipeBook {
	eng::Span<const Recipe> recipes {};         ///< recetas (índice = id de receta)
	eng::Span<const Ingredient> ingredients {}; ///< tabla plana de ingredientes
};

/// Resultado del crafteo (explícito: la UI decide el mensaje, no hay excepciones).
enum class CraftStatus : eng::u8 {
	Done = 0,      ///< fabricado y la instancia vive en el store
	BadRecipe,     ///< índice fuera del libro o rango de ingredientes mal formado
	MissingStation,///< la estación no tiene las etiquetas que la receta exige
	MissingSkill,  ///< la habilidad del actor no llega
	MissingInputs, ///< faltan ingredientes (nada consumido)
	UnknownOutput, ///< el tipo de salida no está en el catálogo
	StoreFull,     ///< no hay hueco en el pool de instancias (nada consumido)
};

/// Nombre legible del estado (diagnóstico y mensajes de UI; sin heap).
[[nodiscard]] constexpr const char* craft_status_name(CraftStatus s) noexcept {
	switch (s) {
		case CraftStatus::Done: return "done";
		case CraftStatus::BadRecipe: return "bad_recipe";
		case CraftStatus::MissingStation: return "missing_station";
		case CraftStatus::MissingSkill: return "missing_skill";
		case CraftStatus::MissingInputs: return "missing_inputs";
		case CraftStatus::UnknownOutput: return "unknown_output";
		case CraftStatus::StoreFull: return "store_full";
		default: return "?";
	}
}

/// Ejecuta la receta `recipe_index` del libro desde la bolsa del actor hacia su store.
/// `station_tags` son las etiquetas de la estación en uso (0 = ninguna) y `skill` el nivel
/// de habilidad del actor. En éxito, `produced` recibe la instancia creada (el llamador
/// decide dónde va: bolsa con `bag_add`, suelo, contenedor…). Cualquier fallo deja el
/// inventario y el store **sin cambios**.
template <eng::u8 Slots, eng::u8 MaxTypes, eng::u8 MaxItems>
[[nodiscard]] CraftStatus craft(ItemBag<Slots>& bag, ItemStore<MaxItems>& store,
				const ItemCatalog<MaxTypes>& catalog, const RecipeBook& book,
				eng::u8 recipe_index, eng::u16 station_tags, eng::u8 skill,
				ItemHandle& produced) noexcept {
	produced = ItemHandle {};
	if (recipe_index >= book.recipes.size()) {
		return CraftStatus::BadRecipe;
	}
	const Recipe& recipe = book.recipes[recipe_index];
	const eng::usize input_end = recipe.input_first + recipe.input_count;
	if (input_end > book.ingredients.size()) {
		return CraftStatus::BadRecipe;
	}
	if (!has_tags(station_tags, recipe.station_tags)) {
		return CraftStatus::MissingStation;
	}
	if (skill < recipe.skill_req) {
		return CraftStatus::MissingSkill;
	}
	eng::Ref<const ItemDef> out_def = catalog.at(recipe.output);
	if (!out_def.valid()) {
		return CraftStatus::UnknownOutput;
	}
	// Validación de ingredientes (sin mutar).
	for (eng::u8 k = 0u; k < recipe.input_count; ++k) {
		const Ingredient& ing = book.ingredients[recipe.input_first + k];
		if (bag_count_match(bag, store, catalog, ing.type, ing.tags) < ing.qty) {
			return CraftStatus::MissingInputs;
		}
	}
	// Salida primero: si el pool está lleno, nada se consume.
	ItemInstance fresh {};
	fresh.type = recipe.output;
	fresh.qty = recipe.output_qty;
	fresh.durability = out_def->durability_max; // pieza nueva a plena durabilidad
	const ItemHandle made = store.spawn(fresh);
	if (!made.valid()) {
		return CraftStatus::StoreFull;
	}
	for (eng::u8 k = 0u; k < recipe.input_count; ++k) {
		const Ingredient& ing = book.ingredients[recipe.input_first + k];
		if (!bag_consume(bag, store, catalog, ing.type, ing.tags, ing.qty)) {
			(void)store.destroy(made); // invariante: nunca se pierde material
			return CraftStatus::MissingInputs;
		}
	}
	produced = made;
	return CraftStatus::Done;
}

} // namespace eng::rpg
