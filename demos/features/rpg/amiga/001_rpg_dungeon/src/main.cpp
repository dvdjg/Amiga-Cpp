// ============================================================================
// Demo 001 (rpg): mazmorra por casillas con interaccion y crafteo
// ============================================================================
//
// Tutorial de la capa RPG del engine (`eng::rpg`) sobre la fachada de juego:
//
//   - El jugador anda **por casillas de 16 px** (como los RPG clasicos) con el joystick, o
//     reproduce un guion determinista si no hay input.
//   - Pasa por encima de un objeto del suelo y entra en la bolsa (`pick_up`: apilado con
//     division por `stack_max`).
//   - Junto al YUNQUE, dispara: forja una espada (`craft`), la mejora (`reinforce`), le
//     aplica un afijo (`apply_affix`) y la equipa; el ATAQUE del HUD sube por el pipeline
//     de atributos derivados (`gather_equipment_mods` + `recompute_stats`).
//   - Junto al COFRE, dispara: se abre (hecho de historia) y derrama llave y pocima.
//   - Lejos de todo, dispara: come (`eat_item`): `eng::sim::Needs` recibe la nutricion y el
//     hambre baja (la simulacion propone, el RPG interpreta).
//   - Junto a la PUERTA, dispara: la llave (etiqueta `key` + metadato) abre la cerradura
//     (`LockSet::try_open`); el objetivo del HUD pasa a ESCAPED y el hueco queda franco.
//
// Dibujo (leccion de coste real en OCS): cada operacion del plan de Blitter sincroniza con
// el Blitter (~miles de ciclos por job), asi que esta escena NO repinta rectangulos por el
// plan. Pinta la sala UNA vez (en los dos buffers) y luego **copia parches 16x16 de ROM**
// con la racha de streaming (`copy_now`/`clear_now`: un job por sprite). El HUD se pinta
// por palabras (`draw_text_words`) sobre la banda negra inferior.
//
//   bash ./tools/build/build-demo.sh demos/features/rpg/amiga/001_rpg_dungeon --debug
//   bash ./tools/run/run-demo.sh demos/features/rpg/amiga/001_rpg_dungeon

#include <eng/api/api.hpp>
#include <eng/core/util/static_string.hpp>
#include <eng/core/util/text.hpp>
#include <eng/debug/version.hpp>
#include <eng/graphics/blit_job.hpp>
#include <eng/graphics/font8.hpp>
#include <eng/input/input.hpp>
#include <eng/memory/mem_bank.hpp>
#include <eng/memory/memory_manager.hpp>
#include <eng/rpg/affix.hpp>
#include <eng/rpg/clock.hpp>
#include <eng/rpg/craft.hpp>
#include <eng/rpg/equipment.hpp>
#include <eng/rpg/facts.hpp>
#include <eng/rpg/interaction.hpp>
#include <eng/rpg/sim_bridge.hpp>
#include <eng/rpg/stats.hpp>
#include <eng/rpg/world.hpp>
#include <eng/platform/amiga/backend.hpp>

#include <exec/execbase.h>
#include <proto/exec.h>

#include "support/gcc8_c_support.h"

struct ExecBase* SysBase = nullptr;

extern "C" {
__attribute__((used)) volatile eng::debug::RunStatus g_eng_run_status {
	eng::debug::run_status_magic, eng::debug::run_status_version,
	static_cast<eng::u16>(eng::debug::RunState::Cold), 0, 0,
};
}

namespace {

using namespace eng::rpg;

// --- Geometria, paleta y ritmo -------------------------------------------------

constexpr eng::u16 kWidth = 320u;      // 20 casillas de 16 px
constexpr eng::u16 kHeight = 256u;     // 16 filas: 13 de sala + 3 de HUD
constexpr eng::u16 kTile = 16u;        // lado de casilla (alineado a palabra en DMA)
constexpr eng::u16 kHudY = 224u;       // franja inferior reservada (norma del repo)
constexpr eng::u8 kMovePeriod = 8u;    // frames por paso de casilla (6,25 casillas/s)
constexpr eng::debug::Version kVersion {1u, 0u}; // el build lo inyecta build-demo.sh

constexpr eng::u8 kColBack = 0u;        // fondo y paso abierto (negro)
constexpr eng::u8 kColWall = 2u;        // muros
constexpr eng::u8 kColPlayer = 3u;      // cuerpo del jugador
constexpr eng::u8 kColPlayerHead = 4u;  // cabeza del jugador
constexpr eng::u8 kColPotion = 5u;      // pocima
constexpr eng::u8 kColKey = 6u;         // llave
constexpr eng::u8 kColChestOpen = 8u;   // cofre abierto
constexpr eng::u8 kColOre = 9u;         // mineral
constexpr eng::u8 kColAnvil = 10u;      // yunque
constexpr eng::u8 kColSword = 11u;      // espada equipada
constexpr eng::u8 kColDoor = 12u;       // puerta cerrada
constexpr eng::u8 kColText = 13u;       // texto del HUD
constexpr eng::u8 kColChest = 7u;       // cofre cerrado

/// Hechos de historia (indice en el `FactStore`).
enum : eng::u16 {
	kFactChestOpen = 0u,
	kFactDoorOpen = 1u,
	kFactSwordCrafted = 2u,
};

/// Habilidad de crafteo del actor (el juego la aporta; el engine no la inventa).
constexpr eng::u8 kPlayerSkill = 2u;

/// Metadato de instancia de la llave del cofre: abre la cerradura 0.
constexpr eng::u16 kDoorKeyMeta = 42u;

/// Afijo de ejemplo: "de fuego" solo en armas; +2 ataque (tabla de mods de afijo).
constexpr AffixDef kAffixFire {item_tags::weapon, 0u, 1u};

/// Distribucion de la sala (casillas; 20x16, muro de una casilla en el borde).
constexpr eng::u8 kOreTx = 6u, kOreTy = 3u;         // pila de mineral
constexpr eng::u8 kAnvilTx = 14u, kAnvilTy = 3u;    // yunque (bloquea)
constexpr eng::u8 kChestTx = 13u, kChestTy = 8u;    // cofre (bloquea)
constexpr eng::u8 kDoorTx = 19u;                    // hueco de puerta (casillas 6..8 de Y)
constexpr eng::u8 kKeyTx = 14u, kKeyTy = 8u;        // botin al abrir el cofre
constexpr eng::u8 kPotionTx = 14u, kPotionTy = 9u;
constexpr eng::u8 kStartTx = 3u, kStartTy = 7u;

/// Pasos del guion determinista (casilla destino; `fire` = pulsar disparo al llegar).
struct Step {
	eng::u8 x;
	eng::u8 y;
	eng::u8 fire;
};

/// Guion: mineral -> yunque (forjar) -> cofre (abrir) -> botin -> comer -> puerta. Los dos
/// ultimos pasos se repiten en bucle como patrulla para que la animacion no pare. Los
/// rodeos del cofre evitan su casilla (bloquea) sin necesitar busqueda de caminos.
constexpr Step kScript[] = {
	{kOreTx, kOreTy, 0u},                     // 0: pila de mineral (recoge al pisar)
	{13, 3, 0u},                              // 1: junto al yunque
	{13, 3, 1u},                              // 2: forjar
	{12, 7, 0u},                              // 3: bajo el yunque, hacia el cofre
	{12, 8, 0u},                              // 4: junto al cofre
	{12, 8, 1u},                              // 5: abrir el cofre
	{12, 9, 0u},                              // 6: rodear por abajo
	{kPotionTx, kPotionTy, 0u},               // 7: botin: pocima
	{kKeyTx, kKeyTy, 0u},                     // 8: botin: llave
	{kPotionTx, kPotionTy, 0u},               // 9: volver a bajar (rodear)
	{11, 9, 0u},                              // 10: salir hacia el centro
	{11, 7, 0u},                              // 11: lejos de todo
	{11, 7, 1u},                              // 12: comer
	{18, 7, 0u},                              // 13: a la puerta
	{18, 7, 1u},                              // 14: abrir con la llave
	{4, 7, 0u},                               // 15: patrulla...
	{17, 7, 0u},                              // 16: ...y vuelta (se repite)
};
constexpr eng::u8 kScriptCount = static_cast<eng::u8>(sizeof(kScript) / sizeof(kScript[0]));
constexpr eng::u8 kPatrolIndex = 15u;

// --- Parches 16-alineados (planar interleaved 4bpp) en memoria Chip -------------
//
// La hoja de cada sprite vive en ROM como palabras planares intercaladas por fila de
// pixel: [plano0..plano3] de la fila 0, [plano0..plano3] de la fila 1... Es el mismo
// orden fisico que el bitmap de pantalla (interleaved), asi que copiarla con el Blitter
// es un unico job sin mascara ni shift: `copy_now` con `dmod = row_bytes - words*2`.

/// Genera las palabras de un parche `W`x`H` (W multiplo de 16) a partir de `pixel(y, x)`.
/// Devuelve el array **interleaved 4bpp** que espera `copy_now` como origen DMA.
template <eng::usize Words, eng::u8 W, eng::u8 H, class Fn>
constexpr eng::util::Array<eng::u16, Words> make_patch(Fn pixel) {
	eng::util::Array<eng::u16, Words> out {};
	for (eng::u8 y = 0u; y < H; ++y) {
		for (eng::u8 x = 0u; x < W; ++x) {
			const eng::u8 color = pixel(y, x);
			if (color == 0u) {
				continue;
			}
			for (eng::u8 plane = 0u; plane < 4u; ++plane) {
				if ((color & static_cast<eng::u8>(1u << plane)) != 0u) {
					const eng::usize index =
					    static_cast<eng::usize>(y * 4u + plane) * (W / 16u) + (x >> 4u);
					out[index] = static_cast<eng::u16>(
					    out[index] | static_cast<eng::u16>(0x8000u >> (x & 15u)));
				}
			}
		}
	}
	return out;
}

/// Dimensiones de los parches (en palabras por fila y pixeles de alto).
constexpr eng::u16 kPatchWords = 1u;   ///< 16 px de ancho = 1 palabra por plano
constexpr eng::u16 kPatchH16 = 16u;
constexpr eng::u16 kPatchH48 = 48u;
constexpr eng::u16 kBlackWords = 2u;   ///< 32 px de ancho
constexpr eng::u16 kBlackH32 = 32u;

/// Jugador sin arma: cabeza + cuerpo.
constexpr auto kPatchPlayer = make_patch<64, 16u, 16u>([](eng::u8 y, eng::u8 x) -> eng::u8 {
	if (y >= 1u && y <= 5u && x >= 4u && x <= 11u) {
		return kColPlayerHead;
	}
	if (y >= 6u && y <= 14u && x >= 2u && x <= 13u) {
		return kColPlayer;
	}
	return 0u;
});

/// Jugador con espada empunada (variante del parche; cero coste por frame).
constexpr auto kPatchPlayerSword = make_patch<64, 16u, 16u>([](eng::u8 y, eng::u8 x) -> eng::u8 {
	if (y >= 2u && y <= 12u && x >= 13u && x <= 14u) {
		return kColSword;
	}
	if (y >= 1u && y <= 5u && x >= 4u && x <= 11u) {
		return kColPlayerHead;
	}
	if (y >= 6u && y <= 14u && x >= 2u && x <= 13u) {
		return kColPlayer;
	}
	return 0u;
});

/// Mineral: dos pepitas.
constexpr auto kPatchOre = make_patch<64, 16u, 16u>([](eng::u8 y, eng::u8 x) -> eng::u8 {
	if (y >= 4u && y <= 9u && x >= 3u && x <= 7u) {
		return kColOre;
	}
	if (y >= 6u && y <= 11u && x >= 9u && x <= 13u) {
		return kColOre;
	}
	return 0u;
});

/// Llave dorada: anilla + paleton.
constexpr auto kPatchKey = make_patch<64, 16u, 16u>([](eng::u8 y, eng::u8 x) -> eng::u8 {
	if (y >= 5u && y <= 10u && x >= 2u && x <= 4u) {
		return kColKey;
	}
	if (y >= 6u && y <= 9u && x >= 5u && x <= 13u) {
		return kColKey;
	}
	return 0u;
});

/// Pocima: cuello + cuerpo.
constexpr auto kPatchPotion = make_patch<64, 16u, 16u>([](eng::u8 y, eng::u8 x) -> eng::u8 {
	if (y >= 3u && y <= 5u && x >= 6u && x <= 9u) {
		return kColPotion;
	}
	if (y >= 6u && y <= 13u && x >= 4u && x <= 11u) {
		return kColPotion;
	}
	return 0u;
});

/// Cofre abierto: marco oscuro con hueco negro.
constexpr auto kPatchChestOpen = make_patch<64, 16u, 16u>([](eng::u8 y, eng::u8 x) -> eng::u8 {
	if (y >= 2u && y <= 13u && x >= 1u && x <= 14u) {
		if (y >= 5u && y <= 11u && x >= 3u && x <= 12u) {
			return 0u; // hueco interior
		}
		return kColChestOpen;
	}
	return 0u;
});

/// Paso abierto de la puerta: negro con jambas grises (16x48: cubre las 3 casillas).
constexpr auto kPatchDoorway = make_patch<192, 16u, 48u>([](eng::u8 y, eng::u8) -> eng::u8 {
	if (y <= 2u || y >= 45u) {
		return kColWall; // dintel y umbral
	}
	return 0u;
});

/// Parche negro por tamano (borrado de casillas 1x1, 2x1, 1x2 y 2x2).
constexpr auto kPatchBlack16 = make_patch<64, 16u, 16u>([](eng::u8, eng::u8) -> eng::u8 { return 0u; });
constexpr auto kPatchBlackH = make_patch<128, 32u, 16u>([](eng::u8, eng::u8) -> eng::u8 { return 0u; });
constexpr auto kPatchBlackV = make_patch<128, 16u, 32u>([](eng::u8, eng::u8) -> eng::u8 { return 0u; });
constexpr auto kPatchBlackSQ =
    make_patch<256, 32u, 32u>([](eng::u8, eng::u8) -> eng::u8 { return 0u; });

/// El juego: composition root del demo (fachada `App`).
struct RpgGame {
	// Contenido
	ItemCatalog<4> m_catalog {};
	eng::util::Array<Ingredient, 1> m_ingredients {};
	eng::util::Array<Recipe, 1> m_recipes {};
	RecipeBook m_book {};
	eng::util::Array<StatMod, 1> m_def_mods {};   // espada: +3 ataque
	ModSources m_sources {};

	// Estado de partida (kernel RPG)
	ItemStore<12> m_items {};
	ItemBag<5> m_bag {};
	Equipment<1> m_equip {};                      // una ranura: mano principal
	StatSheet<2> m_stats {};                      // 0 = ataque
	WorldClock m_clock {};
	FactStore<8> m_facts {};
	EntityStore<8> m_entities {};
	GroundItems<8> m_ground {};
	LockSet<2> m_locks {};
	eng::sim::Needs m_needs {};

	// Ids de contenido (los asigna el catalogo en `setup_world`)
	ItemTypeId m_ore = no_item_type;
	ItemTypeId m_sword = no_item_type;
	ItemTypeId m_potion = no_item_type;
	ItemTypeId m_key = no_item_type;
	eng::u8 m_door_lock = 0xffu;

	// Actores de la rejilla: posicion actual y de hace dos frames (para borrar el rastro
	// en el buffer que toca, porque con doble buffer cada pase pinta sobre el frame N-2).
	struct Pos {
		eng::u8 x = 0;
		eng::u8 y = 0;
	};
	struct Actor {
		Pos cur {};
		Pos prev1 {};
		Pos prev2 {};
	};
	Actor m_player {};
	bool m_sword_visible = false;
	/// Rastro a borrar tras recoger un objeto (2 frames: uno por buffer).
	struct Ghost {
		eng::u8 x = 0;
		eng::u8 y = 0;
		eng::u8 frames = 0u;
	};
	eng::util::Array<Ghost, 4> m_ghosts {};

	/// Cuántos renders han pintado ya la sala (los dos primeros, uno por buffer).
	eng::u8 m_static_paints = 0u;

	/// Tira del HUD en Chip RAM: 320x24 px, 4 planos, layout **plane-major** (plano 0: 24
	/// lineas de 20 palabras; luego plano 1...). La compone la CPU con los glifos de
	/// `Font8` y se publica con **un blit por plano** (`copy_now`): sin frames lentos
	/// (imprimir con `draw_text_words` por frame costaria campos enteros y desestabiliza
	/// la cadena de espera de VBlank; ver DT-025).
	eng::Block<eng::BobTag, eng::MemoryKind::Chip> m_hud_strip {};
	static constexpr eng::u16 kHudWordsPerRow = 20u;              // 320 px / 16
	static constexpr eng::u16 kHudRows = 24u;

	// Guion
	eng::u8 m_step = 0u;
	eng::u8 m_fire_timer = 0u;
	eng::u32 m_frame_no = 0u;
	bool m_prev_fire = false;
	bool m_scripted = true;

	// Parches en Chip RAM (reubicados por el banco de memoria; se llenan en `init`).
	eng::Block<eng::BobTag, eng::MemoryKind::Chip> m_p_player {};
	eng::Block<eng::BobTag, eng::MemoryKind::Chip> m_p_player_sword {};
	eng::Block<eng::BobTag, eng::MemoryKind::Chip> m_p_ore {};
	eng::Block<eng::BobTag, eng::MemoryKind::Chip> m_p_key {};
	eng::Block<eng::BobTag, eng::MemoryKind::Chip> m_p_potion {};
	eng::Block<eng::BobTag, eng::MemoryKind::Chip> m_p_chest_open {};
	eng::Block<eng::BobTag, eng::MemoryKind::Chip> m_p_doorway {};
	eng::Block<eng::BobTag, eng::MemoryKind::Chip> m_p_black16 {};
	eng::Block<eng::BobTag, eng::MemoryKind::Chip> m_p_black_h {};
	eng::Block<eng::BobTag, eng::MemoryKind::Chip> m_p_black_v {};
	eng::Block<eng::BobTag, eng::MemoryKind::Chip> m_p_black_sq {};

	// --- Inicializacion ---

	/// Registra el catalogo, la receta y las tablas de modificadores (contenido del juego).
	void setup_content() {
		ItemDef ore_def {};
		ore_def.tags = item_tags::material;
		ore_def.weight = 2u;
		ore_def.stack_max = 20u;
		m_ore = m_catalog.add(ore_def);

		ItemDef sword_def {};
		sword_def.tags = item_tags::weapon;
		sword_def.weight = 3u;
		sword_def.durability_max = 100u;
		sword_def.mod_first = 0u;
		sword_def.mod_count = 1u;
		m_sword = m_catalog.add(sword_def);

		ItemDef potion_def {};
		potion_def.tags = item_tags::food | item_tags::drink;
		potion_def.weight = 1u;
		potion_def.stack_max = 5u;
		m_potion = m_catalog.add(potion_def);

		ItemDef key_def {};
		key_def.tags = item_tags::key;
		key_def.weight = 0u;
		m_key = m_catalog.add(key_def);

		// Receta: espada = 2 x material en una estacion con etiqueta `tool` (yunque).
		m_ingredients[0] = Ingredient {no_item_type, item_tags::material, 2u};
		m_recipes[0] = Recipe {item_tags::tool, m_sword, 1u, kPlayerSkill, 0u, 1u};
		m_book.recipes = m_recipes.span();
		m_book.ingredients = m_ingredients.span();

		// Mods: espada +3 ataque; afijo de fuego +2 ataque; +10 % por nivel de mejora.
		m_def_mods[0] = StatMod {0u, 3, 0};
		m_sources.def_mods = m_def_mods.span();
		m_sources.affixes = eng::Span<const AffixDef> {&kAffixFire, 1u};
		static constexpr StatMod kFireMods[1] = {StatMod {0u, 2, 0}};
		m_sources.affix_mods = eng::Span<const StatMod> {kFireMods, 1u};
		m_sources.upgrade_step_pct = 10u;

		m_stats.base[0] = 5; // ataque base del jugador
	}

	/// Coloca el mundo: entidades, cerradura, pila de mineral y estado del jugador.
	void setup_world() {
		(void)m_entities.spawn(EntityKind::Container, static_cast<eng::s16>(kChestTx * kTile),
				       static_cast<eng::s16>(kChestTy * kTile),
				       0u, interaction::open | interaction::take);
		(void)m_entities.spawn(EntityKind::Scenery, static_cast<eng::s16>(kAnvilTx * kTile),
				       static_cast<eng::s16>(kAnvilTy * kTile), 0u, interaction::use);
		(void)m_entities.spawn(EntityKind::Scenery, static_cast<eng::s16>(kDoorTx * kTile),
				       static_cast<eng::s16>(7u * kTile), 0u, interaction::open);
		m_door_lock = m_locks.add(kDoorKeyMeta);

		ItemInstance ore {};
		ore.type = m_ore;
		ore.qty = 3u;
		const ItemHandle pile = m_items.spawn(ore);
		(void)m_ground.drop(pile, static_cast<eng::s16>(kOreTx * kTile + kTile / 2u),
				    static_cast<eng::s16>(kOreTy * kTile + kTile / 2u));

		m_player.cur = Pos {kStartTx, kStartTy};
		m_player.prev1 = m_player.cur;
		m_player.prev2 = m_player.cur;

		m_needs.hunger = 40u; // el hambre sube con el tiempo; la pocima la baja
	}

	/// Sube un parche constexpr a un bloque Chip del banco de memoria (setup; coste libre).
	template <eng::usize N>
	[[nodiscard]] eng::Block<eng::BobTag, eng::MemoryKind::Chip> upload(
	    auto& memory, const eng::util::Array<eng::u16, N>& data) {
		// `memory` es dependiente (parámetro `auto`): `template` desambigua el método.
		auto block = memory.chip().template reserve<eng::BobTag>(static_cast<eng::u32>(N * 2u));
		if (block.alive()) {
			eng::Words<eng::BobTag> words = block.view.as_words();
			for (eng::usize i = 0u; i < N; ++i) {
				words[i] = data[i];
			}
		}
		return block;
	}

	/// Reserva y llena las hojas de sprites en Chip RAM (y la tira del HUD).
	void upload_patches(auto& memory) {
		m_p_player = upload(memory, kPatchPlayer);
		m_p_player_sword = upload(memory, kPatchPlayerSword);
		m_p_ore = upload(memory, kPatchOre);
		m_p_key = upload(memory, kPatchKey);
		m_p_potion = upload(memory, kPatchPotion);
		m_p_chest_open = upload(memory, kPatchChestOpen);
		m_p_doorway = upload(memory, kPatchDoorway);
		m_p_black16 = upload(memory, kPatchBlack16);
		m_p_black_h = upload(memory, kPatchBlackH);
		m_p_black_v = upload(memory, kPatchBlackV);
		m_p_black_sq = upload(memory, kPatchBlackSQ);
		m_hud_strip = memory.chip().template reserve<eng::BobTag>(
		    static_cast<eng::u32>(kHudRows) * kHudWordsPerRow * 4u * 2u);
	}

	/// Recalea los atributos derivados desde el equipo (fases fijas de `stats.hpp`).
	void refresh_stats() {
		eng::util::Array<StatMod, 4> mods {};
		const eng::u16 n =
		    gather_equipment_mods(m_equip, m_items, m_catalog, m_sources, mods.span());
		recompute_stats(m_stats, mods.span().first(n));
	}

	/// `init` de la fachada: registra contenido, mundo y parches; pinta el estado inicial.
	void init(auto& app) {
		eng::debug::mark_init_started(g_eng_run_status);
		setup_content();
		setup_world();
		refresh_stats();
		upload_patches(app.memory_manager());
	}

	// --- Reglas del frame ---

	/// ¿La casilla `(x, y)` esta bloqueada? Muros del borde (y hueco de puerta), cofre y
	/// yunque son solidos; la puerta abierta deja pasar hacia el hueco.
	[[nodiscard]] bool blocked(eng::u8 x, eng::u8 y) const noexcept {
		if (y == 0u || y == 13u) {
			return true; // muro superior y banda inferior (HUD)
		}
		if (x == 0u) {
			return true;
		}
		if (x == kDoorTx) {
			// Hueco de la puerta: franco solo si esta abierta.
			return !(m_facts.has(kFactDoorOpen) && y >= 6u && y <= 8u);
		}
		if (x >= 19u) {
			return true;
		}
		if (x == kChestTx && y == kChestTy) {
			return true;
		}
		if (x == kAnvilTx && y == kAnvilTy) {
			return true;
		}
		return false;
	}

	/// Distancia Chebyshev en casillas entre el jugador y `(x, y)` (alcance de interaccion).
	[[nodiscard]] bool near(eng::u8 x, eng::u8 y) const noexcept {
		const eng::u8 dx = static_cast<eng::u8>(m_player.cur.x > x ? m_player.cur.x - x : x - m_player.cur.x);
		const eng::u8 dy = static_cast<eng::u8>(m_player.cur.y > y ? m_player.cur.y - y : y - m_player.cur.y);
		return dx <= 1u && dy <= 1u;
	}

	/// Intenta un paso de casilla hacia `(dx, dy)` (uno por `kMovePeriod` frames).
	void try_step(eng::s16 dx, eng::s16 dy) {
		eng::u8 nx = m_player.cur.x;
		eng::u8 ny = m_player.cur.y;
		if (dx < 0 && nx > 0u) {
			--nx;
		} else if (dx > 0) {
			++nx;
		} else if (dy < 0 && ny > 1u) {
			--ny;
		} else if (dy > 0 && ny < 12u) {
			++ny;
		}
		if ((nx == m_player.cur.x && ny == m_player.cur.y) || blocked(nx, ny)) {
			return;
		}
		m_player.cur.x = nx;
		m_player.cur.y = ny;
	}

	/// Avanza el guion determinista y produce la entrada sintetica de este frame.
	[[nodiscard]] eng::input::PadState script_input() {
		eng::input::PadState pad {};
		if (m_step >= kScriptCount) {
			m_step = kPatrolIndex;
		}
		const Step& target = kScript[m_step];
		const bool arrived = m_player.cur.x == target.x && m_player.cur.y == target.y;
		if (arrived) {
			if (target.fire == 0u) {
				++m_step;
			} else {
				pad.fire = m_fire_timer < 4u; // pulsacion corta: el borde dispara la accion
				++m_fire_timer;
				if (m_fire_timer >= 10u) {
					m_fire_timer = 0u;
					++m_step;
				}
			}
			return pad;
		}
		pad.right = m_player.cur.x < target.x;
		pad.left = m_player.cur.x > target.x;
		pad.down = pad.right == pad.left && m_player.cur.y < target.y;
		pad.up = pad.right == pad.left && !pad.down && m_player.cur.y > target.y;
		return pad;
	}

	/// Coge un objeto del suelo cuyo centro cae en la casilla del jugador.
	void pickups() {
		const eng::s16 cx = static_cast<eng::s16>(m_player.cur.x * kTile + kTile / 2u);
		const eng::s16 cy = static_cast<eng::s16>(m_player.cur.y * kTile + kTile / 2u);
		for (eng::usize i = 0u; i < m_ground.size();) {
			const GroundItems<8>::Entry& e = m_ground.at(i);
			const eng::s16 dx = static_cast<eng::s16>(cx > e.x ? cx - e.x : e.x - cx);
			const eng::s16 dy = static_cast<eng::s16>(cy > e.y ? cy - e.y : e.y - cy);
			if (dx > 8 || dy > 8) {
				++i;
				continue;
			}
			const eng::usize before = m_ground.size();
			const eng::u8 tile_x = static_cast<eng::u8>(e.x >> 4u);
			const eng::u8 tile_y = static_cast<eng::u8>(e.y >> 4u);
			(void)pick_up(m_ground, i, m_bag, m_items, m_catalog);
			if (m_ground.size() == before) {
				++i;
			} else {
				m_hud_dirty = true; // inventario visible cambiado
				// Rastro: el objeto desaparece; se borra su casilla en los dos buffers.
				for (Ghost& g : m_ghosts) {
					if (g.frames == 0u) {
						g = Ghost {tile_x, tile_y, 2u};
						break;
					}
				}
			}
		}
	}

	/// Abre el cofre: derrama su contenido (llave con metadato + pocima) al suelo.
	void open_chest() {
		ItemInstance key {};
		key.type = m_key;
		key.qty = 1u;
		key.metadata = kDoorKeyMeta; // esta llave abre la puerta del este
		const ItemHandle hk = m_items.spawn(key);
		(void)m_ground.drop(hk, static_cast<eng::s16>(kKeyTx * kTile + kTile / 2u),
				    static_cast<eng::s16>(kKeyTy * kTile + kTile / 2u));
		ItemInstance potion {};
		potion.type = m_potion;
		potion.qty = 1u;
		const ItemHandle hp = m_items.spawn(potion);
		(void)m_ground.drop(hp, static_cast<eng::s16>(kPotionTx * kTile + kTile / 2u),
				    static_cast<eng::s16>(kPotionTy * kTile + kTile / 2u));
		(void)m_facts.set_flag(kFactChestOpen);
		m_hud_dirty = true;
	}

	/// Forja la espada en el yunque: receta -> mejora -> afijo -> equipo -> atributos.
	void craft_sword() {
		ItemHandle produced {};
		if (craft(m_bag, m_items, m_catalog, m_book, 0u, item_tags::tool, kPlayerSkill,
			  produced) != CraftStatus::Done) {
			return;
		}
		eng::Ref<ItemInstance> inst = m_items.get(produced);
		(void)reinforce(*inst, 2u, 5u);                        // forja: +2 niveles
		(void)apply_affix(*inst, *m_catalog.at(m_sword), 0u, kAffixFire);
		ItemHandle replaced {};
		(void)equip_item(m_equip, m_items, m_catalog, 0u, produced, replaced);
		m_sword_visible = true; // el parche del jugador cambia a "con espada"
		(void)m_facts.set_flag(kFactSwordCrafted);
		refresh_stats();
		m_hud_dirty = true;
	}

	/// Dispara el verbo segun donde este el jugador (yunque > cofre > puerta > comer).
	void interact() {
		if (near(kAnvilTx, kAnvilTy)) {
			craft_sword();
			return;
		}
		if (near(kChestTx, kChestTy) && !m_facts.has(kFactChestOpen)) {
			open_chest();
			return;
		}
		if (near(kDoorTx, 7u)) {
			const ItemHandle key = bag_find_match(m_bag, m_items, m_catalog, m_key, 0u);
			if (m_locks.try_open(m_door_lock, key, m_items, m_catalog)) {
				(void)m_facts.set_flag(kFactDoorOpen);
				m_hud_dirty = true;
			}
			return;
		}
		if (near(kAnvilTx, kAnvilTy) || near(kChestTx, kChestTy)) {
			return; // junto a algo interactivo sin verbo disponible: no se come
		}
		const ItemHandle food =
		    bag_find_match(m_bag, m_items, m_catalog, no_item_type, item_tags::food);
		if (food.valid()) {
			if (eat_item(m_bag, m_items, m_catalog, food, m_needs, 60u)) {
				m_hud_dirty = true; // la pocima sale del inventario visible
			}
		}
	}

	/// Logica del frame (la fachada llama aqui una vez por VBlank).
	void update(auto& app) {
		// Entrada del frame: humana o del guion determinista (el humano manda y lo apaga).
		const auto& real = app.input().pad0;
		if (real.any_direction() || real.any_button()) {
			m_scripted = false;
		}
		const eng::input::PadState pad = m_scripted ? script_input() : real;

		// Un paso de casilla cada `kMovePeriod` frames (ritmo uniforme de RPG clasico).
		if ((m_frame_no % kMovePeriod) == 0u) {
			const eng::s16 dx = (pad.right ? 1 : 0) - (pad.left ? 1 : 0);
			const eng::s16 dy = (pad.down ? 1 : 0) - (pad.up ? 1 : 0);
			if (dx != 0 || dy != 0) {
				try_step(dx, dy);
			}
		}
		// Disparo por **borde** (mantener no repite el verbo).
		if (pad.fire && !m_prev_fire) {
			interact();
		}
		m_prev_fire = pad.fire;

		// Recogida por contacto: pisar la casilla de un objeto lo mete en la bolsa.
		pickups();

		// Reloj y necesidades: la simulacion "propone" (el hambre sube), el RPG interpreta.
		if ((m_frame_no % 30u) == 0u) {
			eng::sim::rise(m_needs, eng::sim::Need::Hunger, 1u);
		}
		if ((m_frame_no % 10u) == 0u) {
			advance_minutes(m_clock, 1u);
		}
		// El HUD se recompone solo cuando un numero visible cambia (tramo de hambre o
		// contador); el resto de cambios lo marcan las acciones (pickups/comandos).
		const eng::u8 band = static_cast<eng::u8>(m_needs.hunger >> 4u);
		if (band != m_hud_hunger_band) {
			m_hud_hunger_band = band;
			m_hud_dirty = true;
		}
		if ((m_frame_no & 127u) == 0u) {
			m_hud_dirty = true; // el contador visible se refresca ~2 veces por segundo
		}
		++m_frame_no;
	}

	// --- Dibujo ---

	/// Copia un parche Chip a la pantalla con la racha de streaming (`copy_now`). El parche
	/// vive en el layout **interleaved por fila** ([4 palabras por fila de pixel]); si la
	/// escena es interleaved se copia en **un job**; si es planar contiguo, un job por plano
	/// (la fuente avanza con modulo `cmod = 6` entre filas del mismo plano).
	void stamp(auto& s, const eng::Block<eng::BobTag, eng::MemoryKind::Chip>& patch,
		   eng::u16 words, eng::u16 height_px, eng::u8 tx, eng::u8 ty) {
		if (!patch.alive()) {
			return;
		}
		const auto bt = s.target().bob_target();
		const eng::s32 x_off = static_cast<eng::s32>(tx * (kTile / 8u));
		const auto words_view = patch.view.as_words();
		if (bt.layout == eng::graphics::BobLayout::Interleaved) {
			const eng::s32 row_stride = static_cast<eng::s32>(bt.row_bytes) * bt.plane_count;
			const eng::s16 dmod = static_cast<eng::s16>(static_cast<eng::s32>(bt.row_bytes) -
								    static_cast<eng::s32>(words) * 2);
			const eng::graphics::BlitPtr dst {bt.plane_address(
			    0u, static_cast<eng::s32>(ty) * row_stride * kTile + x_off)};
			(void)s.copy_now(eng::graphics::blit_ptr(words_view), dst, words,
					 static_cast<eng::u16>(height_px * bt.plane_count), 0, dmod);
			return;
		}
		// Planar contiguo: un job por plano (las filas del mismo plano distan 4 palabras).
		const eng::s32 row_bytes = bt.row_bytes;
		const eng::s16 dmod = static_cast<eng::s16>(row_bytes - static_cast<eng::s32>(words) * 2);
		for (eng::u8 plane = 0u; plane < bt.plane_count; ++plane) {
			const eng::graphics::BlitPtr src {
			    eng::graphics::blit_ptr(words_view, static_cast<eng::s32>(plane) * 2)};
			const eng::graphics::BlitPtr dst {
			    bt.plane_address(plane, static_cast<eng::s32>(ty) * row_bytes * kTile + x_off)};
			(void)s.copy_now(src, dst, words, height_px, 6, dmod);
		}
	}

	/// Sala estatica: muros del borde, puerta cerrada, yunque y cofre cerrado (se pinta una
	/// vez en cada buffer; nada se les superpone salvo el cofre abierto, que los tapa).
	void draw_static(auto& s) {
		// Muros: banda superior, banda inferior, columna oeste y columna este con el hueco.
		(void)s.fill_box(eng::Box {0, 0, kWidth, kTile}, kColWall);
		(void)s.fill_box(eng::Box {0, 208, kWidth, kTile}, kColWall);
		(void)s.fill_box(eng::Box {0, kTile, kTile, 192u}, kColWall);
		(void)s.fill_box(eng::Box {304, kTile, kTile, 80u}, kColWall);
		(void)s.fill_box(eng::Box {304, 144, kTile, 64u}, kColWall);
		(void)s.fill_box(eng::Box {304, 96, kTile, 48u}, kColDoor);
		// Yunque (casilla con bloqueo) y cofre cerrado.
		(void)s.fill_box(eng::Box {kAnvilTx * kTile, kAnvilTy * kTile, kTile, kTile}, kColAnvil);
		(void)s.fill_box(eng::Box {kAnvilTx * kTile, kAnvilTy * kTile, kTile, 4u}, kColOre);
		(void)s.fill_box(eng::Box {kChestTx * kTile, kChestTy * kTile, kTile, kTile}, kColChest);
	}

	/// Avanza los actores un frame (memoria de dos frames para el borrado por buffer).
	void advance_actors() {
		m_player.prev2 = m_player.prev1;
		m_player.prev1 = m_player.cur;
	}

	/// Borra el rastro de un actor: caja envolvente de su casilla actual y la de hace dos
	/// frames, con el parche negro del tamano exacto (1x1, 2x1, 1x2 o 2x2).
	void erase_actor(auto& s, const Actor& a) {
		const eng::u8 x0 = a.cur.x < a.prev2.x ? a.cur.x : a.prev2.x;
		const eng::u8 y0 = a.cur.y < a.prev2.y ? a.cur.y : a.prev2.y;
		const eng::u8 x1 = a.cur.x > a.prev2.x ? a.cur.x : a.prev2.x;
		const eng::u8 y1 = a.cur.y > a.prev2.y ? a.cur.y : a.prev2.y;
		const eng::u16 w = static_cast<eng::u16>((x1 - x0 + 1u) * kTile);
		const eng::u16 h = static_cast<eng::u16>((y1 - y0 + 1u) * kTile);
		if (w == 32u && h == 32u) {
			stamp(s, m_p_black_sq, kBlackWords, kBlackH32, x0, y0);
		} else if (w == 32u) {
			stamp(s, m_p_black_h, kBlackWords, kPatchH16, x0, y0);
		} else if (h == 32u) {
			stamp(s, m_p_black_v, kPatchWords, kBlackH32, x0, y0);
		} else {
			stamp(s, m_p_black16, kPatchWords, kPatchH16, x0, y0);
		}
	}

	/// Objetos dinamicos: rastros, jugador, espada empunada, botin, cofre abierto y puerta
	/// abierta. Todo son copias de parches: un job por sprite, coste acotado.
	void draw_dynamic(auto& s) {
		// 1) rastros de objetos recogidos (2 frames: uno por buffer).
		for (Ghost& g : m_ghosts) {
			if (g.frames > 0u) {
				stamp(s, m_p_black16, kPatchWords, kPatchH16, g.x, g.y);
				--g.frames;
			}
		}
		// 2) borrado del rastro del jugador (la espada empunada va en su mismo parche).
		erase_actor(s, m_player);
		// 3) pintado del estado actual.
		if (m_sword_visible) {
			stamp(s, m_p_player_sword, kPatchWords, kPatchH16, m_player.cur.x, m_player.cur.y);
		} else {
			stamp(s, m_p_player, kPatchWords, kPatchH16, m_player.cur.x, m_player.cur.y);
		}
		for (eng::usize i = 0u; i < m_ground.size(); ++i) {
			const GroundItems<8>::Entry& e = m_ground.at(i);
			eng::Ref<const ItemInstance> inst = m_items.get(e.item);
			if (!inst.valid()) {
				continue;
			}
			const eng::u8 tx = static_cast<eng::u8>(e.x >> 4u);
			const eng::u8 ty = static_cast<eng::u8>(e.y >> 4u);
			if (inst->type == m_ore) {
				stamp(s, m_p_ore, kPatchWords, kPatchH16, tx, ty);
			} else if (inst->type == m_key) {
				stamp(s, m_p_key, kPatchWords, kPatchH16, tx, ty);
			} else if (inst->type == m_potion) {
				stamp(s, m_p_potion, kPatchWords, kPatchH16, tx, ty);
			}
		}
		if (m_facts.has(kFactChestOpen)) {
			stamp(s, m_p_chest_open, kPatchWords, kPatchH16, kChestTx, kChestTy);
		}
		if (m_facts.has(kFactDoorOpen)) {
			stamp(s, m_p_doorway, kPatchWords, kPatchH48, kDoorTx, 6u);
		}
	}

	/// Invierte los 8 bits de un glifo (Font8 guarda cada fila con el bit 0 a la izquierda;
	/// el Blitter y los planos usan MSB = pixel izquierdo).
	static constexpr eng::u8 rev8(eng::u8 v) noexcept {
		eng::u8 r = 0u;
		for (eng::u8 i = 0u; i < 8u; ++i) {
			r = static_cast<eng::u8>((r << 1u) | ((v >> i) & 1u));
		}
		return r;
	}

	/// Compone una linea del HUD en la tira: pares de glifos de `Font8` como palabras de
	/// 16 px (patron y fondo de golpe, un valor por plano), en la fila `row0` de la tira.
	void compose_hud_line(eng::Words<eng::BobTag> strip, eng::u8 row0, const char* text,
			      eng::u8 color) {
		eng::u16 n = 0u;
		while (text[n] != '\0' && n < kHudWordsPerRow * 2u) {
			++n;
		}
		for (eng::u16 p = 0u; p * 2u < n; ++p) {
			const eng::u8 c0 = static_cast<eng::u8>(text[p * 2u]);
			const eng::u8 c1 = (p * 2u + 1u < n)
					       ? static_cast<eng::u8>(text[p * 2u + 1u])
					       : static_cast<eng::u8>(' ');
			for (eng::u8 r = 0u; r < eng::Font8::kRows; ++r) {
				const eng::u16 bits = static_cast<eng::u16>(
				    (static_cast<eng::u16>(rev8(eng::Font8::row(c0, r))) << 8u) |
				    rev8(eng::Font8::row(c1, r)));
				for (eng::u8 plane = 0u; plane < 4u; ++plane) {
					const eng::usize off =
					    static_cast<eng::usize>(plane) * (kHudRows * kHudWordsPerRow) +
					    static_cast<eng::usize>(row0 + r) * kHudWordsPerRow + p;
					strip[off] = ((color >> plane) & 1u) != 0u ? bits : 0u;
				}
			}
		}
	}

	/// El HUD se **compone** solo cuando el juego levanta `m_hud_dirty` (mutaciones de
	/// inventario/hechos y tramos de hambre/contador); la publicacion por Blitter es por
	/// frame y gratis. Bandera de un byte: coste cero entre cambios.
	eng::debug::HostHud m_host_hud {}; ///< HUD del host (nombre/número/versión/frame/fps)
	bool m_hud_dirty = true;

	/// Tramo de hambre mostrado (1/16 del valor) y tramo del contador, para marcar el HUD
	/// solo cuando numeros visibles cambian (la division es un desplazamiento).
	eng::u8 m_hud_hunger_band = 0xffu;

	/// Compone las tres lineas del HUD en la tira Chip (llamado solo cuando cambia el
	/// estado): glifos de `Font8` como palabras de 16 px, sin pasar por el rasterizador.
	void compose_hud(eng::Words<eng::BobTag> strip) {
		for (eng::u16 i = 0u; i < kHudRows * kHudWordsPerRow * 4u; ++i) {
			strip[i] = 0u; // fondo negro de la tira (autoborra el HUD anterior)
		}
		eng::util::StaticString<48> line {};
		// Linea 1: nombre + version (build inyectado) + contador de frames (depuracion).
		char label[16] = {};
		(void)eng::debug::version_label(kVersion, label, sizeof(label));
		line.assign("RPG DUNGEON ");
		(void)line.append(label);
		(void)line.append(" F ");
		(void)eng::util::to_chars(line, m_frame_no);
		compose_hud_line(strip, 2u, line.c_str(), kColText);

		// Linea 2: inventario, ataque derivado y hambre.
		line.clear();
		(void)line.append("ORE ");
		(void)eng::util::to_chars(line, static_cast<eng::u32>(bag_count(m_bag, m_items, m_ore)));
		(void)line.append(" KEY ");
		(void)eng::util::to_chars(line,
					  static_cast<eng::u32>(bag_count(m_bag, m_items, m_key)));
		(void)line.append(" POT ");
		(void)eng::util::to_chars(line,
					  static_cast<eng::u32>(bag_count(m_bag, m_items, m_potion)));
		(void)line.append(" ATK ");
		(void)eng::util::to_chars(line, static_cast<eng::s32>(derived_stat(m_stats, 0u)));
		(void)line.append(" HUNGER ");
		(void)eng::util::to_chars(line, static_cast<eng::u32>(m_needs.hunger));
		compose_hud_line(strip, 11u, line.c_str(), kColText);

		// Linea 3: objetivo (se mueve con los hechos de historia).
		line.clear();
		if (m_facts.has(kFactDoorOpen)) {
			(void)line.append("OBJ ESCAPED!");
		} else if (m_facts.has(kFactSwordCrafted)) {
			(void)line.append("OBJ OPEN THE DOOR WITH THE KEY");
		} else {
			(void)line.append("OBJ CRAFT A SWORD AT THE ANVIL");
		}
		compose_hud_line(strip, 20u, line.c_str(), kColText);
	}

	/// Publica el HUD: la tira es plane-major (plano 0: 24x20 palabras; luego plano 1...) y
	/// se copia a la banda inferior con **un blit por plano** (`copy_now`). Al ser una
	/// fuente unica, un cambio de estado se ve en ambos buffers al frame siguiente.
	void draw_hud(auto& s) {
		if (!m_hud_strip.alive()) {
			return;
		}
		const eng::Words<eng::BobTag> strip = m_hud_strip.view.as_words();
		if (m_hud_dirty) {
			m_hud_dirty = false;
			compose_hud(strip);
		}
		const auto bt = s.target().bob_target();
		const eng::s16 dmod = static_cast<eng::s16>(static_cast<eng::s32>(bt.row_bytes) -
							    static_cast<eng::s32>(kHudWordsPerRow) * 2);
		for (eng::u8 plane = 0u; plane < 4u; ++plane) {
			const eng::graphics::BlitPtr src {eng::graphics::blit_ptr(
			    strip, static_cast<eng::s32>(plane) * kHudRows * kHudWordsPerRow * 2)};
			const eng::graphics::BlitPtr dst {
			    bt.plane_address(plane, static_cast<eng::s32>(kHudY) * bt.row_bytes)};
			(void)s.copy_now(src, dst, kHudWordsPerRow, kHudRows, 0, dmod);
		}
	}

	/// `render` de la fachada: sala (solo los dos primeros frames, uno por buffer), parches
	/// dinamicos, HUD y publicacion del frame.
	void render(auto& app) {
		auto s = app.screen();
		g_eng_run_status.detail = 0x1100u; // diagnóstico de fases (cuelgue/ablación)
		if (m_static_paints < 2u) {
			draw_static(s); // pinta la sala en el buffer 0 y luego en el 1
			++m_static_paints;
		}
		g_eng_run_status.detail = 0x1200u;
#ifndef K_RPG_NO_DYN
		draw_dynamic(s);
#endif
		g_eng_run_status.detail = 0x2100u;
#ifndef K_RPG_NO_HUD
		draw_hud(s); // tira Chip + un blit por plano: coste bajo y cadencia uniforme
#else
		(void)s;
#endif
		g_eng_run_status.detail = 0x2200u;
		m_host_hud.draw(app.debug(), "001 RPG DUNGEON", kVersion, app.frame(),
				eng::debug::prof_clock());
		app.present();
		g_eng_run_status.detail = 0x3100u;
		advance_actors();
		eng::debug::mark_ready(g_eng_run_status, 0u);
		eng::debug::probe_when_ready(g_eng_run_status, app.frame());
	}
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	if (!backend.configure_game_memory()) {
		eng::debug::mark_failed(g_eng_run_status, 0x00010101u);
		return 0;
	}

	eng::GameDisplay display {};
	display.width = kWidth;
	display.height = kHeight;
	display.color_depth = 4u;
	display.buffers = 2u; // doble buffer: la escritura del frame no toca el visible
	display.palette.color[kColWall] = 0x777u;       // gris de muro
	display.palette.color[kColPlayer] = 0xf00u;     // rojo
	display.palette.color[kColPlayerHead] = 0xff0u; // amarillo
	display.palette.color[kColPotion] = 0x0f0u;     // verde
	display.palette.color[kColKey] = 0xfd0u;        // dorado
	display.palette.color[kColChestOpen] = 0x420u;  // marron oscuro
	display.palette.color[kColChest] = 0x840u;      // marron (cofre cerrado)
	display.palette.color[kColOre] = 0xaaau;        // gris claro
	display.palette.color[kColAnvil] = 0x555u;      // gris medio
	display.palette.color[kColSword] = 0x0ffu;      // cian
	display.palette.color[kColDoor] = 0xf80u;       // naranja
	display.palette.color[kColText] = 0xbbbu;       // gris de texto

	RpgGame game {};
	eng::App app {backend, game, backend.memory_manager()};
	if (!app.set_display(display) || !app.start()) {
		eng::debug::mark_failed(g_eng_run_status, 0x00010102u);
		return 0;
	}
	app.run(0xffffu);
	return 0;
}
