// ============================================================================
// Juego 400: RPG SLICE (aldea + bosque + mazmorra) -- capa RPG del engine
// ============================================================================
//
// Vertical slice de la capa RPG (`eng::rpg`) sobre la fachada de juego: **una aldea, un
// bosque y una mazmorra** con las mecánicas del diseño (`RPG_LAYER.md`) cerrando el bucle:
// quests declarativas sobre hechos (`quest.hpp`), combate por fases (`combat.hpp`),
// interacción/crafteo ligero (`interaction.hpp`), equipo y atributos (`stats.hpp`),
// necesidades (`sim_bridge.hpp`) y save por componente (`save.hpp`, tras la misión).
//
// Flujo del juego (también reproducible por guion determinista sin input):
//   1. La Anciana (aldea) te encarga cazar lobos y te da una espada y pociones.
//   2. En el bosque hay tres lobos territoriales: dos golpes de espada cada uno; dejan
//      pieles.
//   3. Con la caza hecha (`wolf_kills >= 3`) el Mercader abre el trato: compra pieles y
//      vende la **llave del torreón** (20 monedas).
//   4. La puerta del torreón (bosque este) se abre con la llave; dentro, un cofre con la
//      **reliquia** (+50 monedas) completa la crónica.
//
// Quests declarativas (`QuestBook`): la caza y el trato comparten el hecho `hunt_done`
// sin conocerse entre sí; la reliquia cierra la quest final (journal en el HUD).
//
// Rendimiento (lecciones de DT-025): la sala se copia por frame desde una imagen Chip
// cocinada al entrar (4 jobs de Blitter), los actores son parches 16x16 en streaming y el
// HUD es una tira Chip compuesta solo al cambiar el estado. Sin frames lentos.
//
//   bash tools/build/build-demo.sh games/400_rpg_slice --debug --clean
//   bash tools/run/run-demo.sh games/400_rpg_slice

#include <eng/api/api.hpp>
#include <eng/core/util/static_string.hpp>
#include <eng/core/util/text.hpp>
#include <eng/debug/version.hpp>
#include <eng/graphics/blit_job.hpp>
#include <eng/graphics/font8.hpp>
#include <eng/graphics/planar_patch.hpp>
#include <eng/memory/mem_bank.hpp>
#include <eng/memory/memory_manager.hpp>
#include <eng/rpg/combat.hpp>
#include <eng/rpg/craft.hpp>
#include <eng/rpg/equipment.hpp>
#include <eng/rpg/facts.hpp>
#include <eng/rpg/interaction.hpp>
#include <eng/rpg/quest.hpp>
#include <eng/rpg/save.hpp>
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

constexpr eng::u16 kWidth = 320u;    // 20 casillas de 16 px
constexpr eng::u16 kHeight = 256u;   // 14 filas de mapa (224 px) + 2 de HUD
constexpr eng::u16 kTile = 16u;
constexpr eng::u16 kMapW = 20u;
constexpr eng::u16 kMapH = 14u;      // fila 13 = banda inferior del mapa (muro)
constexpr eng::u16 kHudY = 224u;
constexpr eng::u8 kMovePeriod = 8u;  // frames por paso de casilla
constexpr eng::u8 kWolfPeriod = 10u; // frames por paso de lobo en persecucion
constexpr eng::debug::Version kVersion {1u, 0u};

constexpr eng::u8 kColBack = 0u;
constexpr eng::u8 kColText = 13u;

// --- Contenido: hechos, tipos de objeto, quests ---------------------------------

/// Hechos de la cronica (indice en el `FactStore`).
enum : eng::u16 {
	kWolfKills = 0u, ///< contador de lobos cazados (AddFact)
	kHuntDone = 1u,  ///< la caza esta completa (hecho COMPARTIDO: mercader + quests)
	kMerchantOpen = 2u, ///< el mercader acepta trafico (lo abre la quest)
	kDungeonReady = 3u, ///< tienes la llave (lo cierra la quest)
	kRelicFound = 4u,  ///< reliquia encontrada (cofre)
	kVictory = 5u,     ///< cronica completada
	kTalked = 6u,      ///< la anciana te ha encargado la caza
	kDoorOpen = 7u,    ///< puerta del torreon desbloqueada (llave)
	kGold = 8u,        ///< monedas (contador)
};

/// Tipos de objeto del catalogo.
enum : eng::u8 {
	kItemPotion = 0u,
	kItemPelt = 1u,
	kItemSword = 2u,
	kItemKey = 3u,
	kItemRelic = 4u,
};

/// Etiqueta de instancia de la llave: abre la cerradura del torreon.
constexpr eng::u16 kTowerKeyMeta = 7u;

// --- Mapas (contenido en ROM; 20x14 casillas por sala) --------------------------
// Leyenda: 0 piedra | 1 hierba | 2 arbol(bloquea) | 3 agua(bloquea) | 4 sendero |
//          5 suelo de mazmorra | 6 muro de mazmorra(bloquea) | 8 casa(bloquea).

constexpr eng::u8 kTileStone = 0u;
constexpr eng::u8 kTileGrass = 1u;
constexpr eng::u8 kTileTree = 2u;
constexpr eng::u8 kTileWater = 3u;
constexpr eng::u8 kTilePath = 4u;
constexpr eng::u8 kTileDungeon = 5u;
constexpr eng::u8 kTileDWall = 6u;
constexpr eng::u8 kTileHouse = 8u;

/// SALA 0: aldea (sendero central, casas, agua al este; puerta al bosque en x=19).
constexpr eng::u8 kMapVillage[kMapH][kMapW] = {
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{0, 1, 1, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 0},
	{0, 1, 8, 8, 4, 1, 1, 1, 1, 1, 4, 1, 1, 8, 8, 8, 1, 1, 4, 0},
	{0, 1, 8, 8, 4, 1, 1, 1, 1, 1, 4, 1, 1, 8, 8, 8, 1, 1, 4, 0},
	{0, 1, 1, 1, 4, 1, 1, 1, 1, 1, 4, 1, 1, 1, 1, 1, 1, 1, 4, 0},
	{0, 1, 1, 1, 4, 1, 1, 2, 2, 1, 4, 1, 1, 1, 1, 1, 1, 3, 4, 0},
	{0, 1, 1, 1, 4, 4, 4, 4, 4, 4, 4, 1, 1, 1, 1, 1, 1, 3, 4, 0},
	{0, 1, 1, 1, 1, 1, 1, 4, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3, 4, 0},
	{0, 1, 1, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 0},
	{0, 1, 1, 4, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0},
	{0, 1, 1, 4, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0},
	{0, 1, 1, 4, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0},
	{0, 1, 1, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 0},
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
};

/// SALA 1: bosque (lobos territoriales; puerta oeste a la aldea, torreon al este).
constexpr eng::u8 kMapForest[kMapH][kMapW] = {
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{0, 1, 1, 2, 1, 1, 1, 1, 2, 1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 0},
	{0, 1, 2, 2, 1, 1, 2, 1, 1, 1, 1, 2, 1, 1, 2, 1, 1, 1, 1, 0},
	{0, 1, 1, 1, 1, 2, 1, 1, 2, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 0},
	{0, 1, 1, 2, 1, 1, 1, 1, 1, 2, 1, 1, 1, 2, 1, 1, 1, 2, 1, 0},
	{0, 1, 2, 1, 1, 1, 1, 2, 1, 1, 1, 2, 1, 1, 1, 1, 2, 1, 1, 0},
	{0, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 0},
	{0, 4, 1, 1, 1, 1, 1, 1, 1, 4, 1, 1, 1, 1, 1, 1, 1, 1, 4, 0},
	{0, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 0},
	{0, 1, 1, 2, 1, 1, 1, 1, 2, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 0},
	{0, 1, 2, 1, 1, 1, 2, 1, 1, 1, 1, 1, 2, 1, 1, 1, 2, 1, 1, 0},
	{0, 1, 1, 1, 2, 1, 1, 1, 1, 2, 1, 1, 1, 1, 2, 1, 1, 1, 1, 0},
	{0, 1, 2, 1, 1, 1, 1, 2, 1, 1, 1, 2, 1, 1, 1, 1, 1, 2, 1, 0},
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
};

/// SALA 2: mazmorra (muros de piedra; cofre con la reliquia al noreste; salida al oeste).
constexpr eng::u8 kMapDungeon[kMapH][kMapW] = {
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{0, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 0},
	{0, 6, 5, 5, 5, 5, 5, 6, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 0},
	{0, 6, 5, 5, 6, 5, 5, 6, 5, 5, 6, 6, 5, 5, 6, 5, 5, 5, 6, 0},
	{0, 6, 5, 5, 6, 5, 5, 5, 5, 5, 6, 6, 5, 5, 6, 5, 5, 5, 6, 0},
	{0, 6, 5, 5, 5, 5, 6, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 5, 6, 0},
	{0, 4, 4, 4, 4, 4, 6, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 0},
	{0, 4, 5, 5, 5, 5, 5, 5, 6, 5, 5, 6, 5, 5, 5, 5, 5, 5, 6, 0},
	{0, 4, 4, 4, 4, 4, 5, 5, 6, 5, 5, 6, 5, 5, 5, 5, 5, 6, 6, 0},
	{0, 6, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 5, 5, 5, 5, 6, 0},
	{0, 6, 5, 6, 6, 5, 5, 6, 5, 5, 6, 5, 5, 6, 5, 5, 5, 5, 6, 0},
	{0, 6, 5, 5, 5, 5, 5, 6, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 0},
	{0, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 0},
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
};

constexpr const eng::u8* room_map(eng::u8 room) noexcept {
	switch (room) {
		case 0u: return &kMapVillage[0][0];
		case 1u: return &kMapForest[0][0];
		default: return &kMapDungeon[0][0];
	}
}

/// Puertas entre salas: pisar la casilla cambia de sala y coloca al jugador al otro lado.
struct Door {
	eng::u8 room;      ///< sala donde esta la puerta
	eng::u8 x, y;      ///< casilla de la puerta
	eng::u8 target;    ///< sala destino
	eng::u8 tx, ty;    ///< casilla de llegada
	eng::u8 locked;    ///< 1 = requiere `kDoorOpen` (torreon)
};
constexpr Door kDoors[] = {
	{0u, 19u, 7u, 1u, 1u, 7u, 0u}, // aldea -> bosque
	{1u, 0u, 7u, 0u, 18u, 7u, 0u}, // bosque -> aldea
	{1u, 19u, 7u, 2u, 1u, 7u, 1u}, // bosque -> torreon (cerrada: llave)
	{2u, 0u, 7u, 1u, 18u, 7u, 0u}, // torreon -> bosque
};

/// Posiciones de contenido (casillas de cada sala).
constexpr eng::u8 kElderX = 6u, kElderY = 10u;     // aldea
constexpr eng::u8 kMerchantX = 12u, kMerchantY = 10u;
constexpr eng::u8 kBedX = 4u, kBedY = 2u;
constexpr eng::u8 kChestX = 16u, kChestY = 4u;     // torreon
constexpr eng::u8 kPotionDropX = 8u, kPotionDropY = 6u; // bosque (sendero)
constexpr eng::u8 kWolfHome[3][2] = {{6u, 4u}, {10u, 10u}, {15u, 5u}};

// --- Contenido declarativo: catalogo, equipamiento, quests ----------------------

/// Definiciones del catalogo (contenido en ROM).
constexpr eng::util::Array<ItemDef, 5> kItemDefs {{
	// pocima: se come (cura via use_effect del juego), apilable.
	ItemDef {static_cast<eng::u16>(item_tags::food | item_tags::drink), 2u, 3u, 0u, 1u, 0u, 0u},
	// pelt: material valioso (se vende).
	ItemDef {static_cast<eng::u16>(item_tags::material | item_tags::valuable), 3u, 5u, 0u, 0u, 0u, 0u},
	// espada de madera: arma con +3 de ataque (rango de mods del catalogo).
	ItemDef {static_cast<eng::u16>(item_tags::weapon), 3u, 1u, 100u, 0u, 0u, 1u},
	// llave del torreon.
	ItemDef {static_cast<eng::u16>(item_tags::key), 1u, 1u, 0u, 0u, 0u, 0u},
	// reliquia: objeto de mision.
	ItemDef {static_cast<eng::u16>(item_tags::quest | item_tags::valuable), 4u, 1u, 0u, 0u, 0u, 0u},}};

/// Mods del catalogo: [0] espada +3 ataque.
constexpr eng::util::Array<StatMod, 1> kDefMods {{StatMod {0u, 3, 0}}};

/// Quests declarativas (M5): condiciones y acciones como datos.
/// Hechos: ver enum. Quest 0 (caza): wolf_kills>=3 -> hunt_done (cierra). Quest 1 (trato):
/// hunt_done>=1 -> merchant_open; luego ItemGe(llave) -> dungeon_ready (cierra). Quest 2
/// (reliquia): relic_found>=1 -> victory (cierra).
constexpr eng::util::Array<QuestCond, 4> kQuestConds {{
	QuestCond {static_cast<eng::u8>(CondKind::FactGe), kWolfKills, 3u, 0u},
	QuestCond {static_cast<eng::u8>(CondKind::FactGe), kHuntDone, 1u, 0u},
	QuestCond {static_cast<eng::u8>(CondKind::ItemGe), kItemKey, 1u, 0u},
	QuestCond {static_cast<eng::u8>(CondKind::FactGe), kRelicFound, 1u, 0u},
}};
constexpr eng::util::Array<QuestAct, 4> kQuestActs {{
	QuestAct {static_cast<eng::u8>(ActKind::SetFact), kHuntDone, 1u, 0u},
	QuestAct {static_cast<eng::u8>(ActKind::SetFact), kMerchantOpen, 1u, 0u},
	QuestAct {static_cast<eng::u8>(ActKind::SetFact), kDungeonReady, 1u, 0u},
	QuestAct {static_cast<eng::u8>(ActKind::SetFact), kVictory, 1u, 0u},
}};
constexpr eng::util::Array<QuestStage, 4> kQuestStages {{
	// q0 (caza): stage global 0.
	QuestStage {0u, 1u, 0u, 1u, true},
	// q1 (trato): stages globales 1 y 2.
	QuestStage {1u, 1u, 1u, 1u, false},
	QuestStage {2u, 1u, 2u, 1u, true},
	// q2 (reliquia): stage global 3.
	QuestStage {3u, 1u, 3u, 1u, true},
}};
constexpr eng::util::Array<QuestDef, 3> kQuestDefs {{
	QuestDef {0u, 1u},
	QuestDef {1u, 2u},
	QuestDef {3u, 1u},
}};

// --- Parches 16x16 (constexpr en ROM, subidos a Chip en el setup) ----------------

using PlanarPatch16 = eng::util::Array<eng::u16, 64>;
constexpr eng::u8 kColPlayer = 3u;
constexpr eng::u8 kColPlayerHead = 4u;
constexpr eng::u8 kColPotion = 5u;
constexpr eng::u8 kColKey = 6u;
constexpr eng::u8 kColPelt = 7u;
constexpr eng::u8 kColWolf = 10u;
constexpr eng::u8 kColWolfEye = 12u;
constexpr eng::u8 kColElder = 9u;
constexpr eng::u8 kColMerchant = 11u;
constexpr eng::u8 kColChest = 8u;
constexpr eng::u8 kColDoorLocked = 2u;   // naranja de puerta cerrada
constexpr eng::u8 kColRelic = 14u;

constexpr auto kPatchPlayer = eng::graphics::make_planar_patch<64, 16u, 16u>(
    [](eng::u8 y, eng::u8 x) -> eng::u8 {
	    if (y >= 1u && y <= 5u && x >= 4u && x <= 11u) {
		    return kColPlayerHead;
	    }
	    if (y >= 6u && y <= 14u && x >= 2u && x <= 13u) {
		    return kColPlayer;
	    }
	    return 0u;
    });
constexpr auto kPatchPlayerSword = eng::graphics::make_planar_patch<64, 16u, 16u>(
    [](eng::u8 y, eng::u8 x) -> eng::u8 {
	    if (y >= 2u && y <= 12u && x >= 13u && x <= 14u) {
		    return kColPlayerHead; // hoja clara (amarilla)
	    }
	    if (y >= 1u && y <= 5u && x >= 4u && x <= 11u) {
		    return kColPlayerHead;
	    }
	    if (y >= 6u && y <= 14u && x >= 2u && x <= 13u) {
		    return kColPlayer;
	    }
	    return 0u;
    });
constexpr auto kPatchWolf = eng::graphics::make_planar_patch<64, 16u, 16u>(
    [](eng::u8 y, eng::u8 x) -> eng::u8 {
	    if (y >= 2u && y <= 6u && x >= 3u && x <= 8u) {
		    return kColWolf; // cabeza
	    }
	    if (y >= 5u && y <= 12u && x >= 2u && x <= 14u) {
		    return kColWolf; // cuerpo
	    }
	    if (y >= 3u && y <= 4u && x >= 4u && x <= 7u) {
		    return kColWolfEye; // ojos brillantes
	    }
	    return 0u;
    });
constexpr auto kPatchElder = eng::graphics::make_planar_patch<64, 16u, 16u>(
    [](eng::u8 y, eng::u8 x) -> eng::u8 {
	    if (y >= 1u && y <= 4u && x >= 5u && x <= 10u) {
		    return kColPlayerHead; // cara
	    }
	    if (y >= 5u && y <= 14u && x >= 3u && x <= 12u) {
		    return kColElder; // tunica
	    }
	    return 0u;
    });
constexpr auto kPatchMerchant = eng::graphics::make_planar_patch<64, 16u, 16u>(
    [](eng::u8 y, eng::u8 x) -> eng::u8 {
	    if (y >= 1u && y <= 4u && x >= 5u && x <= 10u) {
		    return kColPlayerHead;
	    }
	    if (y >= 5u && y <= 14u && x >= 3u && x <= 12u) {
		    return kColMerchant;
	    }
	    return 0u;
    });
constexpr auto kPatchBed = eng::graphics::make_planar_patch<64, 16u, 16u>(
    [](eng::u8 y, eng::u8 x) -> eng::u8 {
	    if (y >= 3u && y <= 12u && x >= 2u && x <= 13u) {
		    return kColPelt; // marco de madera
	    }
	    if (y >= 4u && y <= 7u && x >= 3u && x <= 12u) {
		    return kColPlayerHead; // almohada
	    }
	    return 0u;
    });
constexpr auto kPatchPotion = eng::graphics::make_planar_patch<64, 16u, 16u>(
    [](eng::u8 y, eng::u8 x) -> eng::u8 {
	    if (y >= 3u && y <= 5u && x >= 6u && x <= 9u) {
		    return kColPotion;
	    }
	    if (y >= 6u && y <= 13u && x >= 4u && x <= 11u) {
		    return kColPotion;
	    }
	    return 0u;
    });
constexpr auto kPatchPelt = eng::graphics::make_planar_patch<64, 16u, 16u>(
    [](eng::u8 y, eng::u8 x) -> eng::u8 {
	    if (y >= 3u && y <= 12u && x >= 2u && x <= 13u) {
		    return kColPelt;
	    }
	    if (y >= 5u && y <= 7u && x >= 6u && x <= 10u) {
		    return kColPlayerHead; // cara del lobo
	    }
	    return 0u;
    });
constexpr auto kPatchKey = eng::graphics::make_planar_patch<64, 16u, 16u>(
    [](eng::u8 y, eng::u8 x) -> eng::u8 {
	    if (y >= 5u && y <= 10u && x >= 2u && x <= 4u) {
		    return kColKey;
	    }
	    if (y >= 6u && y <= 9u && x >= 5u && x <= 13u) {
		    return kColKey;
	    }
	    return 0u;
    });
constexpr auto kPatchRelic = eng::graphics::make_planar_patch<64, 16u, 16u>(
    [](eng::u8 y, eng::u8 x) -> eng::u8 {
	    const bool dia = (y >= 5u && y <= 10u && x >= 4u && x <= 11u);
	    if (dia && (y + x) % 2u == 0u) {
		    return kColRelic;
	    }
	    if (y >= 7u && y <= 8u && x >= 2u && x <= 13u) {
		    return kColKey; // base dorada
	    }
	    return 0u;
    });
constexpr auto kPatchChestClosed = eng::graphics::make_planar_patch<64, 16u, 16u>(
    [](eng::u8 y, eng::u8 x) -> eng::u8 {
	    if (y >= 4u && y <= 13u && x >= 1u && x <= 14u) {
		    return kColChest;
	    }
	    if (y >= 6u && y <= 7u && x >= 1u && x <= 14u) {
		    return kColPelt; // banda de la tapa
	    }
	    return 0u;
    });
constexpr auto kPatchChestOpen = eng::graphics::make_planar_patch<64, 16u, 16u>(
    [](eng::u8 y, eng::u8 x) -> eng::u8 {
	    if (y >= 4u && y <= 13u && x >= 1u && x <= 14u) {
		    if (y >= 6u && y <= 11u && x >= 3u && x <= 12u) {
			    return kColBack; // hueco interior (negro sobre el suelo)
		    }
		    return kColChest;
	    }
	    return 0u;
    });
constexpr auto kPatchDoorLocked = eng::graphics::make_planar_patch<64, 16u, 16u>(
    [](eng::u8 y, eng::u8) -> eng::u8 {
	    return (y >= 2u && y <= 13u) ? kColDoorLocked : 0u;
    });
/// Parche negro (todas las palabras a 0): pinta la banda inferior (filas 224..255) una vez
/// por buffer en el arranque, porque la imagen de sala solo cubre 0..223.
constexpr PlanarPatch16 kPatchBlack16 =
    eng::graphics::make_planar_patch<64, 16u, 16u>([](eng::u8, eng::u8) -> eng::u8 { return 0u; });

// --- Tiles de las salas (16x16, se cocinan en la imagen de sala) -----------------

constexpr eng::u8 kColStone = 1u;
constexpr eng::u8 kColGrass = 15u;
constexpr eng::u8 kColTree = 10u;
constexpr eng::u8 kColWater = 6u;
constexpr eng::u8 kColPath = 7u;
constexpr eng::u8 kColDungeon = 1u;

/// Patron de tile por id de mapa (mismo generador planar; el color 0 es fondo).
constexpr eng::u8 tile_pixel(eng::u8 tile, eng::u8 y, eng::u8 x) noexcept {
	switch (tile) {
		case kTileStone:
			return kColStone;
		case kTileGrass:
			return (x == 3u && y == 4u) || (x == 11u && y == 9u) ? kColTree : kColGrass;
		case kTileTree:
			if (y >= 7u && y <= 13u && x >= 7u && x <= 8u) {
				return kColPath; // tronco
			}
			return (y >= 1u && y <= 10u && x >= 2u && x <= 13u) ? kColTree : kColGrass;
		case kTileWater:
			if (y == 4u && (x >= 2u && x <= 6u)) {
				return kColText; // brillo
			}
			if (y == 10u && (x >= 8u && x <= 12u)) {
				return kColText;
			}
			return kColWater;
		case kTilePath:
			return kColPath;
		case kTileDungeon:
			return (x == 0u || y == 0u) ? kColText : kColDungeon;
		case kTileDWall:
			return (x == 0u || y == 0u || x == 15u || y == 15u) ? kColText : kColStone;
		case kTileHouse:
			if (x >= 2u && x <= 13u && y >= 3u && y <= 13u) {
				if (y >= 7u && x >= 6u && x <= 9u && y <= 13u) {
					return kColBack; // puerta (negro)
				}
				return kColPelt;
			}
			return kColGrass;
		default:
			return 0u;
	}
}

/// Rejilla de tiles de una sala para el cocinado de la imagen.
constexpr eng::u8 map_tile(const eng::u8* map, eng::u8 x, eng::u8 y) noexcept {
	return map[static_cast<eng::usize>(y) * kMapW + x];
}

/// Patron de un tile precalculado en ROM: la palabra de cada (fila, plano). El cocinado
/// indexa la tabla en vez de recorrer 16 px por palabra — el bucle por pixel costaba
/// ~12 M ciclos por sala (segundos de CPU en el cruce de puerta; AGENTS §1.15).
constexpr eng::u8 kTilePatternCount = 9u; // ids 0..8 (kTileHouse = 8)
struct TilePattern {
	eng::util::Array<eng::u16, 4> plane[16]; ///< [fila][plano] = palabras del tile
};
constexpr eng::util::Array<TilePattern, kTilePatternCount> kTilePatterns = [] {
	eng::util::Array<TilePattern, kTilePatternCount> table {};
	for (eng::u8 t = 0u; t < kTilePatternCount; ++t) {
		for (eng::u8 y = 0u; y < 16u; ++y) {
			for (eng::u8 p = 0u; p < 4u; ++p) {
				eng::u16 bits = 0u;
				for (eng::u8 x = 0u; x < 16u; ++x) {
					if ((tile_pixel(t, y, x) & static_cast<eng::u8>(1u << p)) != 0u) {
						bits = static_cast<eng::u16>(bits | static_cast<eng::u16>(0x8000u >> x));
					}
				}
				table[t].plane[y][p] = bits;
			}
		}
	}
	return table;
}();

// --- El juego -------------------------------------------------------------------

/// Pixeles del area de mapa (14 filas de tile) y palabras por fila de la imagen.
constexpr eng::u16 kImageRows = kMapH * kTile;         // 224 (la banda 208..223 lleva el HUD)
constexpr eng::u16 kImageWordsPerRow = kMapW;          // 20
constexpr eng::u32 kImagePlaneWords = kImageRows * kImageWordsPerRow;
constexpr eng::u32 kImageBytes = kImagePlaneWords * 4u * 2u;
constexpr eng::u32 kImageHudWord = 208u * kImageWordsPerRow; // fila de la banda de HUD (2 lineas)

struct RpgSlice {
	// Contenido
	ItemCatalog<5> m_catalog {};
	RecipeBook m_book {};          // sin recetas en el slice
	ModSources m_sources {};
	QuestBook m_quest_book {};
	CombatTables m_combat {};      // sin mods/resistencias: base x factor 1.0

	// Estado de partida
	ItemStore<16> m_items {};
	ItemBag<6> m_bag {};
	Equipment<1> m_equip {};   // mano principal
	StatSheet<2> m_stats {};   // 0 = ataque
	WorldClock m_clock {};
	FactStore<16> m_facts {};
	QuestRuntime<3> m_quests {};
	EntityStore<8> m_entities {};
	GroundItems<8> m_ground {};
	LockSet<2> m_locks {};
	eng::sim::Needs m_needs {};
	Vitality m_player_hp {};
	eng::u8 m_tower_lock = 0xffu;

	// Jugador y mundo
	eng::u8 m_room = 0u;
	eng::u8 m_tx = 3u, m_ty = 11u;
	eng::u8 m_step = 0u;
	eng::u8 m_fire_timer = 0u;
	eng::u32 m_frame_no = 0u;
	bool m_prev_fire = false;
	bool m_scripted = true;

	/// Lobos: territoriales hasta el primer golpe; entonces persiguen.
	struct Wolf {
		eng::u8 x = 0, y = 0;
		eng::u8 hp = 10u;
		eng::u8 alert = 0u;
		eng::u8 cd = 0u;
		bool dead = false;
		eng::u8 p1x = 0, p1y = 0, p2x = 0, p2y = 0; ///< historial (2 frames) para borrar estela
	};
	eng::util::Array<Wolf, 3> m_wolves {};

	eng::u8 m_atk_cd = 0u; // enfriamiento del jugador
	eng::u8 m_sword_owned = 0u;
	eng::u8 m_chest_open = 0u;

	// Chip: una imagen pre-cocinada por sala (plane-major, con el HUD dentro) y los parches.
	// `m_room_active` indexa la sala vigente; `active_image()` da su bloque (Block no es
	// asignable: la imagen activa se conmuta por indice, sin copias).
	eng::util::Array<eng::Block<eng::BobTag, eng::MemoryKind::Chip>, 3> m_room_imgs {};
	eng::u8 m_room_active = 0u;
	eng::util::Array<eng::Block<eng::BobTag, eng::MemoryKind::Chip>, 14> m_patches {};

	/// Renders que ya pintaron la banda inferior negra (los dos primeros, uno por buffer).
	eng::u8 m_static_paints = 0u;

	/// Renders que faltan para publicar la imagen de sala en el buffer trasero (2 cubre los
	/// dos buffers). Se repone al entrar en sala y en cada cambio de la imagen (eventos).
	eng::u8 m_room_publish = 0u;

	/// Renders que faltan para republicar solo la **banda del HUD** (2 cubre los dos
	/// buffers): el HUD cambia a menudo y arrastrarlo con la imagen entera costaba un
	/// publicacion completa por cambio.
	eng::u8 m_hud_band = 0u;

	/// Historial del jugador (posicion hace 1 y 2 renders) para borrar la estela del buffer
	/// de turno restaurando desde la imagen cocinada.
	eng::u8 m_prev1x = 3u, m_prev1y = 11u, m_prev2x = 3u, m_prev2y = 11u;

	/// Entradas de sala acumuladas: se publica en el `detail` del run-status (un ping-pong
	/// de puertas lo dispara; un valor estable = transiciones puntuales).
	eng::u16 m_entry_count = 0u;

	/// Bloque de la imagen de sala vigente (`Block` no es asignable: se indexa).
	[[nodiscard]] eng::Block<eng::BobTag, eng::MemoryKind::Chip>& active_image() noexcept {
		return m_room_imgs[m_room_active < 3u ? m_room_active : 0u];
	}

	// Ablación de comprobación (misma familia que la demo 001).
	bool m_hud_dirty = true;
	eng::u8 m_hud_hunger_band = 0xffu;

	// --- Setup -------------------------------------------------------------------

	/// Registra catalogo, tablas y coloca el mundo inicial (aldea con lobos en el bosque).
	void setup_world() {
		for (eng::u8 i = 0u; i < 5u; ++i) {
			(void)m_catalog.add(kItemDefs[i]);
		}
		m_sources.def_mods = kDefMods.span();
		m_quest_book.quests = kQuestDefs.span();
		m_quest_book.stages = kQuestStages.span();
		m_quest_book.conds = kQuestConds.span();
		m_quest_book.acts = kQuestActs.span();
		m_stats.base[0] = 4; // ataque del jugador (puños); la espada suma +3
		m_player_hp = Vitality {12u, 12u};
		m_needs.hunger = 30u;
		m_tower_lock = m_locks.add(kTowerKeyMeta);

		// Pocima en el sendero del bosque (se recoge al pisarla).
		spawn_ground(kItemPotion, kPotionDropX, kPotionDropY, 1u);
		// Lobos territoriales.
		for (eng::u8 i = 0u; i < 3u; ++i) {
			m_wolves[i].x = kWolfHome[i][0];
			m_wolves[i].y = kWolfHome[i][1];
			m_wolves[i].p1x = m_wolves[i].x;
			m_wolves[i].p1y = m_wolves[i].y;
			m_wolves[i].p2x = m_wolves[i].x;
			m_wolves[i].p2y = m_wolves[i].y;
		}
		// Quest inicial: la caza (se completa la apertura al hablar con la anciana).
		quest_open(m_quest_book, m_quests, 0u);
		quest_open(m_quest_book, m_quests, 1u);
		quest_open(m_quest_book, m_quests, 2u);
		m_hud_dirty = true;
	}

	/// Crea una instancia en el suelo (helper de contenido).
	ItemHandle spawn_ground(eng::u8 type, eng::u8 tx, eng::u8 ty, eng::u8 qty) {
		ItemInstance inst {};
		inst.type = type;
		inst.qty = qty;
		const ItemHandle h = m_items.spawn(inst);
		(void)m_ground.drop(h, static_cast<eng::s16>(tx * kTile + kTile / 2u),
				    static_cast<eng::s16>(ty * kTile + kTile / 2u));
		return h;
	}

	/// Sube un parche constexpr a Chip (setup).
	template <eng::usize N>
	[[nodiscard]] eng::Block<eng::BobTag, eng::MemoryKind::Chip> upload(
	    auto& memory, const eng::util::Array<eng::u16, N>& data) {
		auto block = memory.chip().template reserve<eng::BobTag>(static_cast<eng::u32>(N * 2u));
		if (block.alive()) {
			auto words = block.view.as_words();
			for (eng::usize i = 0u; i < N; ++i) {
				words[i] = data[i];
			}
		}
		return block;
	}

	/// Reserva las imagenes de sala y sube los parches (setup; coste libre).
	void upload_assets(auto& memory) {
		// Una imagen por sala (3): se pre-cocinan en el setup y el cambio de sala solo
		// conmuta el bloque activo — cero coste y cero tiron en las puertas.
		for (eng::u8 r = 0u; r < 3u; ++r) {
			m_room_imgs[r] = memory.chip().template reserve<eng::BobTag>(kImageBytes);
		}
		m_patches[0] = upload(memory, kPatchPlayer);
		m_patches[1] = upload(memory, kPatchPlayerSword);
		m_patches[2] = upload(memory, kPatchWolf);
		m_patches[3] = upload(memory, kPatchElder);
		m_patches[4] = upload(memory, kPatchMerchant);
		m_patches[5] = upload(memory, kPatchBed);
		m_patches[6] = upload(memory, kPatchPotion);
		m_patches[7] = upload(memory, kPatchPelt);
		m_patches[8] = upload(memory, kPatchKey);
		m_patches[9] = upload(memory, kPatchRelic);
		m_patches[10] = upload(memory, kPatchChestClosed);
		m_patches[11] = upload(memory, kPatchDoorLocked);
		m_patches[12] = upload(memory, kPatchChestOpen);
		m_patches[13] = upload(memory, kPatchBlack16);
	}

	/// Cocina la imagen de la sala `room` en `img_block`: tiles del mapa + decorado estatico
	/// (anciana, mercader, cama; cofre cerrado en el torreon). Se llama en el setup para las
	/// tres salas (una vez cada una), de modo que entrar en una sala NO cocina: solo
	/// conmuta la imagen activa (`enter_room`).
	void cook_room_into(eng::Block<eng::BobTag, eng::MemoryKind::Chip>& img_block,
			    eng::u8 room) {
		if (!img_block.alive()) {
			return;
		}
		auto img = img_block.view.as_words();
		const eng::u8* map = room_map(room);
		for (eng::u16 ty = 0u; ty < kMapH; ++ty) {
			for (eng::u16 tx = 0u; tx < kMapW; ++tx) {
				const eng::u8 tile = map_tile(map, static_cast<eng::u8>(tx),
							      static_cast<eng::u8>(ty));
				stamp_tile(img, tile, tx, ty);
			}
		}
		// Decorado estatico por sala (parches cocinados por CPU en la imagen).
		if (room == 0u) {
			stamp_patch(img, m_patches[3], kElderX, kElderY);     // anciana
			stamp_patch(img, m_patches[4], kMerchantX, kMerchantY); // mercader
			stamp_patch(img, m_patches[5], kBedX, kBedY);         // cama
		} else if (room == 2u) {
			// Cofre cerrado horneado; al abrirlo, el parche de cofre abierto lo cubre.
			stamp_patch(img, m_patches[10], kChestX, kChestY);
		}
	}

	/// Entra en la sala `m_room`: conmuta la imagen pre-cocinada y programa su publicacion
	/// en los dos buffers. Si una reserva fallo (sin Chip), cocina esa sala al vuelo.
	void enter_room() {
		m_room_active = m_room;
		if (!active_image().alive()) {
			cook_room_into(active_image(), m_room);
		}
		m_room_publish = 2u;
		++m_entry_count;
	}

	/// Escribe el patron del tile `tile` en la imagen (casilla `tx,ty`), en formato
	/// plane-major (`[plano][fila][palabra]`), indexando la tabla ROM.
	void stamp_tile(eng::Words<eng::BobTag> img, eng::u8 tile, eng::u16 tx, eng::u16 ty) {
		const TilePattern& pat = kTilePatterns[tile < kTilePatternCount ? tile : 0u];
		eng::u32 dest = static_cast<eng::u32>(ty * kTile) * kImageWordsPerRow + tx;
		for (eng::u8 y = 0u; y < kTile; ++y) {
			for (eng::u8 plane = 0u; plane < 4u; ++plane) {
				img[dest + static_cast<eng::u32>(plane) * kImagePlaneWords] =
				    pat.plane[y][plane];
			}
			dest += kImageWordsPerRow;
		}
	}

	/// Cocinado de un parche 16x16 (interleaved en ROM) a la imagen (plane-major).
	void stamp_patch(eng::Words<eng::BobTag> img,
			 const eng::Block<eng::BobTag, eng::MemoryKind::Chip>& patch, eng::u8 tx,
			 eng::u8 ty) {
		if (!patch.alive()) {
			return;
		}
		const auto src = patch.view.as_words();
		for (eng::u8 y = 0u; y < kTile; ++y) {
			for (eng::u8 plane = 0u; plane < 4u; ++plane) {
				const eng::u16 bits = src[y * 4u + plane];
				if (bits == 0u) {
					continue; // transparente sobre el tile cocinado
				}
				const eng::u32 dest = plane * kImagePlaneWords +
						      static_cast<eng::u32>(ty * kTile + y) *
							  kImageWordsPerRow + tx;
				img[dest] = static_cast<eng::u16>(img[dest] | bits);
			}
		}
	}

	/// `init` de la fachada.
	void init(auto& app) {
		eng::debug::mark_init_started(g_eng_run_status);
		setup_world();
		// Fases de init en `detail` (state queda en init): un cuelgue deja la ultima fase vista.
		g_eng_run_status.detail = 0x40110u; // mundo construido
		upload_assets(app.memory_manager());
		g_eng_run_status.detail = 0x40111u; // assets subidos a Chip
		// Pre-cocina las tres salas (coste unico en init): entrar en sala no vuelve a cocinar.
		for (eng::u8 r = 0u; r < 3u; ++r) {
			cook_room_into(m_room_imgs[r], r);
		}
		enter_room(); // activa la imagen de la sala inicial y pide su publicacion
		g_eng_run_status.detail = 0x40112u; // salas cocinadas
		refresh_stats();
		g_eng_run_status.detail = 0x40113u; // init completa
	}

	/// Recalcula atributos derivados desde el equipo.
	void refresh_stats() {
		eng::util::Array<StatMod, 4> mods {};
		const eng::u16 n =
		    gather_equipment_mods(m_equip, m_items, m_catalog, m_sources, mods.span());
		recompute_stats(m_stats, mods.span().first(n));
	}

	// --- Reglas ------------------------------------------------------------------

	/// ¿Casilla bloqueada en la sala actual? (muros, agua, arboles, casas + NPCs solidos).
	[[nodiscard]] bool blocked(eng::u8 x, eng::u8 y) const noexcept {
		if (x >= kMapW - 1u || y == 0u || y >= kMapH - 1u) {
			return true;
		}
		const eng::u8 tile = map_tile(room_map(m_room), x, y);
		if (tile == kTileTree || tile == kTileWater || tile == kTileDWall ||
		    tile == kTileHouse) {
			return true;
		}
		if (m_room == 0u) {
			if ((x == kElderX && y == kElderY) || (x == kMerchantX && y == kMerchantY) ||
			    (x == kBedX && y == kBedY)) {
				return true;
			}
		}
		if (m_room == 2u && m_chest_open == 0u && x == kChestX && y == kChestY) {
			return true;
		}
		for (eng::u8 i = 0u; i < 3u; ++i) {
			if (!m_wolves[i].dead && m_wolves[i].x == x && m_wolves[i].y == y) {
				return true;
			}
		}
		return false;
	}

	/// Distancia Chebyshev en casillas entre el jugador y `(x, y)`.
	[[nodiscard]] eng::u8 chebyshev(eng::u8 x, eng::u8 y) const noexcept {
		const eng::u8 dx = static_cast<eng::u8>(m_tx > x ? m_tx - x : x - m_tx);
		const eng::u8 dy = static_cast<eng::u8>(m_ty > y ? m_ty - y : y - m_ty);
		return dx > dy ? dx : dy;
	}

	/// Puerta en la casilla `(x,y)` de la sala actual (nullptr si no hay).
	[[nodiscard]] const Door* door_at(eng::u8 x, eng::u8 y) const noexcept {
		for (eng::usize i = 0u; i < sizeof(kDoors) / sizeof(kDoors[0]); ++i) {
			const Door& d = kDoors[i];
			if (d.room == m_room && d.x == x && d.y == y) {
				return &d;
			}
		}
		return nullptr;
	}

	/// Intenta un paso del jugador; si pisa una puerta, cambia de sala.
	void try_step(eng::s16 dx, eng::s16 dy) {
		eng::u8 nx = m_tx;
		eng::u8 ny = m_ty;
		if (dx < 0 && nx > 0u) {
			--nx;
		} else if (dx > 0) {
			++nx;
		} else if (dy < 0 && ny > 0u) {
			--ny;
		} else if (dy > 0) {
			++ny;
		}
		if (nx == m_tx && ny == m_ty) {
			return;
		}
		if (const Door* d = door_at(nx, ny)) {
			if (d->locked != 0u && !m_facts.has(kDoorOpen)) {
				return; // el torreon aun esta cerrado
			}
			m_room = d->target;
			m_tx = d->tx;
			m_ty = d->ty;
			// El historial de estela no cruza salas: la imagen se conmuta y se publica.
			m_prev1x = m_tx;
			m_prev1y = m_ty;
			m_prev2x = m_tx;
			m_prev2y = m_ty;
			enter_room();
			m_hud_dirty = true;
			return;
		}
		if (blocked(nx, ny)) {
			return;
		}
		m_tx = nx;
		m_ty = ny;
	}

	// --- Interacciones -----------------------------------------------------------

	/// La anciana: entrega el encargo (hecho), la espada y dos pocimas (una sola vez).
	void talk_elder() {
		if (m_facts.has(kTalked)) {
			return;
		}
		(void)m_facts.set_flag(kTalked);
		ItemInstance sword {};
		sword.type = kItemSword;
		sword.qty = 1u;
		sword.durability = 100u;
		const ItemHandle sw = m_items.spawn(sword);
		(void)bag_add(m_bag, m_items, m_catalog, sw);
		ItemHandle replaced {};
		(void)equip_item(m_equip, m_items, m_catalog, 0u, sw, replaced);
		m_sword_owned = 1u;
		spawn_ground(kItemPotion, static_cast<eng::u8>(kElderX + 1u),
			     static_cast<eng::u8>(kElderY + 1u), 2u);
		refresh_stats();
		m_hud_dirty = true;
	}

	/// El mercader: vende una piel (10 oro) o la llave del torreon (20 oro).
	void trade_merchant() {
		if (!m_facts.has(kMerchantOpen)) {
			return;
		}
		const ItemHandle pelt = bag_find_match(m_bag, m_items, m_catalog, kItemPelt, 0u);
		if (pelt.valid()) {
			// `consume` sin cualificar resolveria a la politica `Items` del struct.
			(void)eng::rpg::consume(m_bag, m_items, pelt, 1u);
			(void)m_facts.add(kGold, 10u);
			m_hud_dirty = true;
			return;
		}
		if (!m_facts.has(kDungeonReady) && m_facts.value_or(kGold, 0u) >= 20u) {
			// Cobrar: set con el valor recalculado (FactStore no resta; add es saturado).
			(void)m_facts.set(kGold, static_cast<eng::u16>(m_facts.value(kGold) - 20u));
			ItemInstance key {};
			key.type = kItemKey;
			key.qty = 1u;
			key.metadata = kTowerKeyMeta;
			const ItemHandle kh = m_items.spawn(key);
			(void)bag_add(m_bag, m_items, m_catalog, kh);
			quest_invalidate(m_quests); // el inventario cambio: reevalua las quests
			m_hud_dirty = true;
		}
	}

	/// La cama: descanso completo (cura la vitalidad).
	void rest_bed() {
		m_player_hp.hp = m_player_hp.hp_max;
		eng::sim::rest(m_needs, 120u);
		m_hud_dirty = true;
	}

	/// El cofre del torreon: reliquia + 50 monedas.
	void open_chest() {
		if (m_chest_open != 0u) {
			return;
		}
		m_chest_open = 1u;
		// Botin al alcance (el suelo vecino del cofre es transitable).
		spawn_ground(kItemRelic, kChestX, static_cast<eng::u8>(kChestY - 1u), 1u);
		spawn_ground(kItemPotion, static_cast<eng::u8>(kChestX - 1u), kChestY, 1u);
		(void)m_facts.set_flag(kRelicFound);
		(void)m_facts.add(kGold, 50u);
		m_room_publish = 2u; // el parche de cofre abierto cubre el cerrado horneado
		m_hud_dirty = true;
	}

	/// Bebe una pocima: cura 8 de vitalidad (y alimenta via el puente de la sim).
	void drink_potion_if_useful() {
		if (static_cast<eng::u16>(m_player_hp.hp_max - m_player_hp.hp) < 2u) {
			return; // solo merece la pena con dos o mas puntos perdidos
		}
		const ItemHandle potion =
		    bag_find_match(m_bag, m_items, m_catalog, kItemPotion, 0u);
		if (potion.valid() && eat_item(m_bag, m_items, m_catalog, potion, m_needs, 20u)) {
			(void)heal(m_player_hp, 8u);
			m_hud_dirty = true;
		}
	}

	/// Ataque del jugador a un lobo adyacente (pipeline de dano con las tablas del juego).
	void player_attack() {
		if (m_atk_cd != 0u) {
			return;
		}
		m_atk_cd = 18u;
		for (eng::u8 i = 0u; i < 3u; ++i) {
			Wolf& w = m_wolves[i];
			if (w.dead || m_room != 1u || chebyshev(w.x, w.y) > 1u) {
				continue;
			}
			const DamageInput atk {
			    static_cast<eng::u16>(derived_stat(m_stats, 0u)),
			    static_cast<eng::u16>(damage_tags::physical | damage_tags::slash |
						  (m_sword_owned != 0u ? 0u : damage_tags::blunt))};
			const eng::u16 dmg = resolve_damage(atk, m_combat);
			const eng::u16 done = dmg < w.hp ? dmg : w.hp;
			w.hp = static_cast<eng::u8>(w.hp - done);
			w.alert = 1u;
			if (w.hp == 0u) {
				w.dead = true;
				spawn_ground(kItemPelt, w.x, w.y, 1u);
				(void)m_facts.add(kWolfKills, 1u); // hecho compartido con las quests
				m_room_publish = 2u; // el lobo sale de la imagen (y suelta la piel)
			}
			m_hud_dirty = true;
			return; // un objetivo por golpe
		}
	}

	/// Dispara el verbo segun el contexto (casillas adyacentes primero).
	void interact() {
		if (m_room == 0u) {
			if (chebyshev(kElderX, kElderY) <= 1u) {
				talk_elder();
				return;
			}
			if (chebyshev(kMerchantX, kMerchantY) <= 1u) {
				trade_merchant();
				return;
			}
			if (chebyshev(kBedX, kBedY) <= 1u) {
				rest_bed();
				return;
			}
		}
		if (m_room == 1u) {
			// Puerta del torreon adyacente: abrir con la llave (si se tiene).
			if (chebyshev(19u, 7u) <= 1u && !m_facts.has(kDoorOpen)) {
				const ItemHandle key =
				    bag_find_match(m_bag, m_items, m_catalog, kItemKey, 0u);
				if (key.valid() && m_locks.try_open(m_tower_lock, key, m_items,
								    m_catalog)) {
					(void)m_facts.set_flag(kDoorOpen);
					m_hud_dirty = true;
					m_room_publish = 2u; // el parche de puerta cerrada se retira
				}
				return;
			}
			player_attack();
			return;
		}
		if (m_room == 2u && chebyshev(kChestX, kChestY) <= 1u) {
			open_chest();
			return;
		}
		drink_potion_if_useful();
	}

	/// Recoge objetos del suelo en la casilla actual (marca quests y HUD).
	void pickups() {
		const eng::s16 cx = static_cast<eng::s16>(m_tx * kTile + kTile / 2u);
		const eng::s16 cy = static_cast<eng::s16>(m_ty * kTile + kTile / 2u);
		for (eng::usize i = 0u; i < m_ground.size();) {
			const GroundItems<8>::Entry& e = m_ground.at(i);
			const eng::s16 dx = static_cast<eng::s16>(cx > e.x ? cx - e.x : e.x - cx);
			const eng::s16 dy = static_cast<eng::s16>(cy > e.y ? cy - e.y : e.y - cy);
			if (dx > 8 || dy > 8) {
				++i;
				continue;
			}
			const eng::usize before = m_ground.size();
			(void)pick_up(m_ground, i, m_bag, m_items, m_catalog);
			if (m_ground.size() == before) {
				++i;
			} else {
				quest_invalidate(m_quests); // inventario cambiado: reevaluar quests
				m_hud_dirty = true;
				m_room_publish = 2u; // el objeto del suelo desaparece de la imagen
			}
		}
	}

	/// Tick de los lobos: persiguen al jugador alertado y muerden si estan al lado.
	void wolves_tick() {
		if (m_room != 1u) {
			return;
		}
		if ((m_frame_no % kWolfPeriod) == 0u) {
			for (eng::u8 i = 0u; i < 3u; ++i) {
				Wolf& w = m_wolves[i];
				if (w.dead || w.alert == 0u || chebyshev(w.x, w.y) > 6u) {
					continue;
				}
				eng::u8 nx = w.x;
				eng::u8 ny = w.y;
				if (w.x != m_tx) {
					nx = w.x < m_tx ? static_cast<eng::u8>(w.x + 1u)
							: static_cast<eng::u8>(w.x - 1u);
				} else if (w.y != m_ty) {
					ny = w.y < m_ty ? static_cast<eng::u8>(w.y + 1u)
							: static_cast<eng::u8>(w.y - 1u);
				}
				// No pisa la casilla del jugador (se queda al lado para morder) ni
				// casillas bloqueadas ni a otro lobo.
				bool free = !(nx == m_tx && ny == m_ty) && !blocked(nx, ny);
				for (eng::u8 j = 0u; j < 3u && free; ++j) {
					if (j != i && !m_wolves[j].dead && m_wolves[j].x == nx &&
					    m_wolves[j].y == ny) {
						free = false;
					}
				}
				if (free) {
					w.x = nx;
					w.y = ny;
				}
			}
		}
		// Mordisco: adyacente y con enfriamiento.
		for (eng::u8 i = 0u; i < 3u; ++i) {
			Wolf& w = m_wolves[i];
			if (w.dead) {
				continue;
			}
			if (w.cd > 0u) {
				--w.cd;
			}
			if (w.alert != 0u && w.cd == 0u && chebyshev(w.x, w.y) <= 1u) {
				const DamageInput bite {3u, damage_tags::blunt};
				const eng::u16 dmg = resolve_damage(bite, m_combat);
				(void)hurt_actor(m_player_hp, m_needs, dmg);
				w.cd = 45u; // enfriamiento del mordisco
				m_hud_dirty = true;
				if (!m_player_hp.alive()) {
					// Misericordia del slice: despiertas en la cama de la aldea.
					m_room = 0u;
					m_tx = kBedX;
					m_ty = static_cast<eng::u8>(kBedY + 1u);
					m_player_hp.hp = m_player_hp.hp_max;
					m_prev1x = m_tx;
					m_prev1y = m_ty;
					m_prev2x = m_tx;
					m_prev2y = m_ty;
					enter_room();
					m_hud_dirty = true;
				}
			}
		}
	}

	/// Guion determinista (sin input): la cronica completa y despues patrulla.
	struct Step {
		eng::u8 room;
		eng::u8 x;
		eng::u8 y;
		eng::u8 fire;
	};
	static constexpr Step kScript[] = {
		{0u, 5u, 10u, 0u}, {0u, 5u, 10u, 1u},                   // 0-1 anciana: encargo+espada
		{0u, 7u, 11u, 0u},                                      // 2 recoger las pocimas
		{0u, 7u, 8u, 0u}, {0u, 18u, 8u, 0u}, {0u, 18u, 7u, 0u}, // 3-5 sendero al este
		{0u, 19u, 7u, 0u},                                      // 6 puerta (a bosque)
		{1u, 1u, 6u, 0u}, {1u, 6u, 6u, 0u}, {1u, 6u, 5u, 0u},   // 7-9 lobo 1
		// Tres amagos por lobo: el golpe efectivo va en el 1.er y 3.er flanco del fuego
		// (cadencia 12 frames por paso < enfriamiento 18 de `player_attack`): dos golpes
		// de espada dejan al lobo en 0 y sueltan la piel. Con dos pasos el 2.o golpe
		// caia en enfriamiento y el lobo sobrevivia persiguiendo al jugador.
		{1u, 6u, 5u, 1u}, {1u, 6u, 5u, 1u}, {1u, 6u, 5u, 1u},   // 10-12 dos espadazos
		{1u, 6u, 4u, 0u},                                       // 13 piel
		{1u, 6u, 6u, 0u}, {1u, 10u, 6u, 0u}, {1u, 10u, 9u, 0u}, // 14-16 lobo 2
		{1u, 10u, 9u, 1u}, {1u, 10u, 9u, 1u}, {1u, 10u, 9u, 1u}, // 17-19
		{1u, 10u, 10u, 0u},                                     // 20 piel
		{1u, 10u, 6u, 0u}, {1u, 15u, 6u, 0u},                   // 21-22 lobo 3
		{1u, 15u, 6u, 1u}, {1u, 15u, 6u, 1u}, {1u, 15u, 6u, 1u}, // 23-25
		{1u, 15u, 5u, 0u},                                      // 26 piel
		{1u, 8u, 6u, 0u},                                       // 27 pocima del sendero
		{1u, 1u, 7u, 0u}, {1u, 0u, 7u, 0u},                     // 28-29 puerta oeste
		{0u, 18u, 7u, 0u}, {0u, 18u, 8u, 0u}, {0u, 11u, 9u, 0u}, {0u, 11u, 10u, 0u}, // 30-33 mercader
		{0u, 11u, 10u, 1u}, {0u, 11u, 10u, 1u}, {0u, 11u, 10u, 1u}, {0u, 11u, 10u, 1u}, // 34-37 vender x3 y comprar la llave
		{0u, 11u, 11u, 0u}, {0u, 18u, 11u, 0u}, {0u, 18u, 8u, 0u}, {0u, 18u, 7u, 0u}, // 38-41 volver al este por abajo (el mercader bloquea la fila 10)
		{0u, 19u, 7u, 0u},                                      // 42 puerta (a bosque)
		{1u, 18u, 7u, 0u}, {1u, 18u, 7u, 1u},                   // 43-44 abrir el torreon
		{1u, 19u, 7u, 0u},                                      // 45 puerta (a mazmorra)
		{2u, 1u, 6u, 0u}, {2u, 5u, 6u, 0u}, {2u, 5u, 8u, 0u}, {2u, 7u, 8u, 0u}, // 46-49 rodeo
		{2u, 7u, 4u, 0u}, {2u, 8u, 5u, 0u}, {2u, 15u, 5u, 0u}, {2u, 15u, 4u, 0u}, // 50-53 al cofre
		{2u, 15u, 4u, 1u},                                      // 54 abrir el cofre
		{2u, 16u, 4u, 0u}, {2u, 16u, 3u, 0u},                   // 55-56 recoger la reliquia
		{2u, 8u, 5u, 0u}, {2u, 15u, 5u, 0u},                    // 57-58 patrulla (se repite)
	};
	static constexpr eng::u8 kScriptCount = static_cast<eng::u8>(sizeof(kScript) / sizeof(kScript[0]));
	static constexpr eng::u8 kPatrolIndex = static_cast<eng::u8>(sizeof(kScript) / sizeof(kScript[0]) - 2u);

	/// Entrada sintetica del guion.
	[[nodiscard]] eng::input::PadState script_input() {
		eng::input::PadState pad {};
		if (m_step >= kScriptCount) {
			m_step = kPatrolIndex;
		}
		const Step& t = kScript[m_step];
		if (t.room != m_room) {
			// El guion espera el cambio de sala (puerta); si no llega, avanza.
			++m_step;
			return pad;
		}
		const bool arrived = (m_tx == t.x && m_ty == t.y);
		if (arrived) {
			if (t.fire == 0u) {
				++m_step;
			} else {
				pad.fire = m_fire_timer < 4u;
				++m_fire_timer;
				if (m_fire_timer >= 12u) { // 3-4 golpes por paso con enfriamiento 18
					m_fire_timer = 0u;
					++m_step;
				}
			}
			return pad;
		}
		// Paseo por ejes con **rodeo de obstaculos**: el guion no lleva pathfinding (la
		// navegacion real llegara con la integracion RPG<->sim); si la casilla del eje
		// preferido (X) esta ocupada (NPC, arbol, lobo), el paso va por Y para bordearla
		// en vez de empujar indefinidamente contra ella.
		const bool want_x = (m_tx != t.x);
		const bool want_y = (m_ty != t.y);
		bool use_x = want_x;
		if (want_x) {
			const eng::u8 sx = static_cast<eng::u8>(m_tx < t.x ? m_tx + 1u : m_tx - 1u);
			use_x = !blocked(sx, m_ty);
		}
		if (use_x) {
			pad.right = m_tx < t.x;
			pad.left = m_tx > t.x;
		} else if (want_y) {
			pad.down = m_ty < t.y;
			pad.up = m_ty > t.y;
		} else {
			// X tapado y fila ya alineada: reintenta X (p. ej. una puerta transitoria);
			// si sigue tapado, el guion avanza de paso al cambiar de sala.
			pad.right = m_tx < t.x;
			pad.left = m_tx > t.x;
		}
		return pad;
	}

	// --- Frame -------------------------------------------------------------------

	void update(auto& app) {
		if (m_atk_cd > 0u) {
			--m_atk_cd;
		}
		const auto& real = app.input().pad0;
		if (real.any_direction() || real.any_button()) {
			m_scripted = false;
		}
		const eng::input::PadState pad = m_scripted ? script_input() : real;

		if ((m_frame_no % kMovePeriod) == 0u) {
			const eng::s16 dx = (pad.right ? 1 : 0) - (pad.left ? 1 : 0);
			const eng::s16 dy = (pad.down ? 1 : 0) - (pad.up ? 1 : 0);
			if (dx != 0 || dy != 0) {
				try_step(dx, dy);
			}
		}
		if (pad.fire && !m_prev_fire) {
			interact();
		}
		m_prev_fire = pad.fire;

		pickups();
		wolves_tick();
		(void)quest_tick(m_facts, m_quest_book, m_quests, *this);

		// Necesidades y reloj (el hambre sube; la sim propone, el RPG interpreta).
		if ((m_frame_no % 40u) == 0u) {
			eng::sim::rise(m_needs, eng::sim::Need::Hunger, 1u);
		}
		if ((m_frame_no % 10u) == 0u) {
			advance_minutes(m_clock, 1u);
		}
		const eng::u8 band = static_cast<eng::u8>(m_needs.hunger >> 4u);
		if (band != m_hud_hunger_band) {
			m_hud_hunger_band = band;
			m_hud_dirty = true;
		}
		if ((m_frame_no & 127u) == 0u) {
			m_hud_dirty = true; // contador del HUD (~2 veces por segundo)
		}
		++m_frame_no;
	}

	/// Politica `Items` para `quest_tick`: cuenta/consume del inventario real.
	[[nodiscard]] eng::u16 count(eng::u16 type, eng::u16 tags) const noexcept {
		return bag_count_match(m_bag, m_items, m_catalog,
				       type >= 0xffu ? no_item_type : static_cast<ItemTypeId>(type),
				       tags);
	}
	bool consume(eng::u16 type, eng::u16 tags, eng::u8 qty) noexcept {
		return bag_consume(m_bag, m_items, m_catalog,
				   type >= 0xffu ? no_item_type : static_cast<ItemTypeId>(type), tags,
				   qty);
	}

	// --- Dibujo ------------------------------------------------------------------

	/// Copia un bloque Chip plane-major a la pantalla (`one job per plane`).
	void blit_plane_major(auto& s,
			      const eng::Block<eng::BobTag, eng::MemoryKind::Chip>& block,
			      eng::u16 words, eng::u16 rows, eng::u16 dest_y) {
		if (!block.alive()) {
			return;
		}
		const auto src = block.view.as_words();
		const auto bt = s.target().bob_target();
		const eng::s16 dmod = static_cast<eng::s16>(static_cast<eng::s32>(bt.row_bytes) -
							    static_cast<eng::s32>(words) * 2);
		for (eng::u8 plane = 0u; plane < 4u; ++plane) {
			const eng::graphics::BlitPtr sp {eng::graphics::blit_ptr(
			    src, static_cast<eng::s32>(plane) * rows * words * 2)};
			const eng::graphics::BlitPtr dp {
			    bt.plane_address(plane, static_cast<eng::s32>(dest_y) * bt.row_bytes)};
			(void)s.copy_now(sp, dp, words, rows, 0, dmod);
		}
	}

	/// Copia un parche 16x16 (interleaved en Chip) a una casilla de la pantalla.
	void stamp_cell(auto& s, const eng::Block<eng::BobTag, eng::MemoryKind::Chip>& patch,
			eng::u8 tx, eng::u8 ty) {
		if (!patch.alive()) {
			return;
		}
		const auto src = patch.view.as_words();
		const auto bt = s.target().bob_target();
		const eng::s16 dmod = static_cast<eng::s16>(static_cast<eng::s32>(bt.row_bytes) - 2);
		for (eng::u8 plane = 0u; plane < 4u; ++plane) {
			const eng::graphics::BlitPtr sp {
			    eng::graphics::blit_ptr(src, static_cast<eng::s32>(plane) * 2)};
			const eng::graphics::BlitPtr dp {bt.plane_address(
			    plane, static_cast<eng::s32>(ty) * kTile * bt.row_bytes +
				       static_cast<eng::s32>(tx * 2))};
			(void)s.copy_now(sp, dp, 1u, kTile, 6, dmod);
		}
	}

	/// Restaura una casilla 16x16 **desde la imagen cocinada** a la pantalla (un blit por
	/// plano): asi se borra la estela de un actor sin repintar la sala entera. El origen es
	/// plane-major (filas contiguas de 40 B → `cmod = 38`); el destino, la rejilla de la
	/// pantalla (`dmod = row_bytes - 2`).
	void restore_cell(auto& s, eng::u8 tx, eng::u8 ty) {
		if (!active_image().alive()) {
			return;
		}
		const auto img = active_image().view.as_words();
		const auto bt = s.target().bob_target();
		const eng::s16 dmod = static_cast<eng::s16>(static_cast<eng::s32>(bt.row_bytes) - 2);
		for (eng::u8 plane = 0u; plane < 4u; ++plane) {
			const eng::graphics::BlitPtr src {eng::graphics::blit_ptr(
			    img, static_cast<eng::s32>(plane) * kImagePlaneWords * 2 +
				 static_cast<eng::s32>(ty) * kTile * kImageWordsPerRow * 2 +
				 static_cast<eng::s32>(tx) * 2)};
			const eng::graphics::BlitPtr dst {bt.plane_address(
			    plane, static_cast<eng::s32>(ty) * kTile * bt.row_bytes +
				       static_cast<eng::s32>(tx) * 2)};
			(void)s.copy_now(src, dst, 1u, kTile, 38, dmod);
		}
	}

	/// Restaura el rectangulo de casillas `[x0..x1]x[y0..y1]` (sin ordenar) desde la imagen.
	void erase_cells(auto& s, eng::u8 x0, eng::u8 y0, eng::u8 x1, eng::u8 y1) {
		const eng::u8 lx = x0 < x1 ? x0 : x1;
		const eng::u8 ly = y0 < y1 ? y0 : y1;
		const eng::u8 hx = x0 < x1 ? x1 : x0;
		const eng::u8 hy = y0 < y1 ? y1 : y0;
		for (eng::u8 ty = ly; ty <= hy; ++ty) {
			for (eng::u8 tx = lx; tx <= hx; ++tx) {
				restore_cell(s, tx, ty);
			}
		}
	}

	/// Republica la **banda del HUD** (filas 208..223) desde la imagen al buffer trasero: un
	/// blit por plano de 20x16 palabras. Es lo que se copia al cambiar el HUD (no la imagen
	/// entera: el HUD cambia a menudo y la sala no).
	void publish_hud_band(auto& s) {
		if (!active_image().alive()) {
			return;
		}
		const auto img = active_image().view.as_words();
		const auto bt = s.target().bob_target();
		// Origen y destino tienen filas contiguas de 40 B: `cmod = dmod = 0`.
		for (eng::u8 plane = 0u; plane < 4u; ++plane) {
			const eng::graphics::BlitPtr src {eng::graphics::blit_ptr(
			    img, static_cast<eng::s32>(plane) * kImagePlaneWords * 2 +
				     static_cast<eng::s32>(kImageHudWord) * 2)};
			const eng::graphics::BlitPtr dst {bt.plane_address(
			    plane, static_cast<eng::s32>(208u) * bt.row_bytes)};
			(void)s.copy_now(src, dst, kImageWordsPerRow, 16u, 0, 0);
		}
	}

	/// HUD: tira Chip compuesta al cambiar el estado y publicada cada frame.
	void draw_hud(auto&) {
		if (!active_image().alive()) {
			return;
		}
		const auto img = active_image().view.as_words();
		if (m_hud_dirty) {
			m_hud_dirty = false;
			m_hud_band = 2u; // la banda del HUD vive en la imagen: republicar solo la banda
			// La banda del HUD (filas 208..223, dos lineas de 8 px) vive en la propia
			// imagen de sala: cero por plano y componer; lo publica la copia de la sala
			// (cero jobs de Blitter extra por frame).
			for (eng::u8 plane = 0u; plane < 4u; ++plane) {
				for (eng::u32 i = 0u; i < 16u * kImageWordsPerRow; ++i) {
					img[plane * kImagePlaneWords + kImageHudWord + i] = 0u;
				}
			}
			eng::util::StaticString<48> line {};
			char label[16] = {};
			(void)eng::debug::version_label(kVersion, label, sizeof(label));
			// Linea A: nombre + version + contador + vitalidad y ataque.
			line.assign("RPG SLICE ");
			(void)line.append(label);
			(void)line.append(" F ");
			(void)eng::util::to_chars(line, m_frame_no);
			(void)line.append(" HP");
			(void)eng::util::to_chars(line, static_cast<eng::u32>(m_player_hp.hp));
			(void)line.append(" ATK");
			(void)eng::util::to_chars(line, static_cast<eng::s32>(derived_stat(m_stats, 0u)));
			compose_line(img, 0u, line.c_str());

			// Linea B: monedas, botin y objetivo (hechos de las quests).
			line.clear();
			(void)line.append("G");
			(void)eng::util::to_chars(line, static_cast<eng::u32>(m_facts.value_or(kGold, 0u)));
			(void)line.append(" PELT");
			(void)eng::util::to_chars(
			    line, static_cast<eng::u32>(bag_count(m_bag, m_items, kItemPelt)));
			(void)line.append(" POT");
			(void)eng::util::to_chars(
			    line, static_cast<eng::u32>(bag_count(m_bag, m_items, kItemPotion)));
			(void)line.append(" ");
			if (m_facts.has(kVictory)) {
				(void)line.append("OBJ:VICTORY!");
			} else if (m_facts.has(kDoorOpen) || m_facts.has(kRelicFound)) {
				(void)line.append("OBJ:RELIC IN THE TOWER");
			} else if (m_facts.has(kHuntDone)) {
				(void)line.append("OBJ:SELL PELTS GET KEY");
			} else if (m_facts.has(kTalked)) {
				(void)line.append("OBJ:HUNT 3 WOLVES");
			} else {
				(void)line.append("OBJ:TALK TO THE ELDER");
			}
			compose_line(img, 8u, line.c_str());
		}
	}

	/// Compone una linea del HUD en la tira (mismo algoritmo que la demo 001).
	void compose_line(eng::Words<eng::BobTag> strip, eng::u8 row0, const char* text) {
		eng::u16 n = 0u;
		while (text[n] != '\0' && n < 40u) {
			++n;
		}
		for (eng::u16 p = 0u; p * 2u < n; ++p) {
			const eng::u8 c0 = static_cast<eng::u8>(text[p * 2u]);
			const eng::u8 c1 = (p * 2u + 1u < n) ? static_cast<eng::u8>(text[p * 2u + 1u])
							     : static_cast<eng::u8>(' ');
			for (eng::u8 r = 0u; r < eng::Font8::kRows; ++r) {
				const eng::u16 bits = static_cast<eng::u16>(
				    (static_cast<eng::u16>(rev8(eng::Font8::row(c0, r))) << 8u) |
				    rev8(eng::Font8::row(c1, r)));
				for (eng::u8 plane = 0u; plane < 4u; ++plane) {
					const eng::u32 off =
					    static_cast<eng::u32>(plane) * kImagePlaneWords +
					    static_cast<eng::u32>(208u + row0 + r) * kImageWordsPerRow + p;
					strip[off] = ((kColText >> plane) & 1u) != 0u ? bits
										      : static_cast<eng::u16>(0u);
				}
			}
		}
	}

	/// Bit-reverse de una fila de glifo (Font8 guarda el bit 0 a la izquierda).
	[[nodiscard]] static constexpr eng::u8 rev8(eng::u8 v) noexcept {
		eng::u8 r = 0u;
		for (eng::u8 i = 0u; i < 8u; ++i) {
			r = static_cast<eng::u8>((r << 1u) | ((v >> i) & 1u));
		}
		return r;
	}

	/// `render` de la fachada: imagen de sala (solo al cambiar) + actores + HUD.
	void render(auto& app) {
		auto s = app.screen();
		// **Publicacion de la imagen por cambio, no por frame**: la sala cocinada solo se
		// copia al buffer trasero cuando cambia (entrada de sala, HUD o eventos), en los
		// DOS buffers (2 renders). Copiarla cada frame costaba ~1 campo de CPU/Blitter.
		if (m_room_publish != 0u) {
			blit_plane_major(s, active_image(), kImageWordsPerRow,
					 static_cast<eng::u16>(kImageRows), 0u);
			--m_room_publish;
			m_hud_band = 0u; // la publicacion completa ya incluye la banda del HUD
		} else if (m_hud_band != 0u) {
			publish_hud_band(s);
			--m_hud_band;
		}
		// Banda inferior (224..255) negra: dos filas de parches negros en los dos primeros
		// renders (uno por buffer); despues nadie la toca.
		if (m_static_paints < 2u) {
			for (eng::u8 tx = 0u; tx < 20u; ++tx) {
				stamp_cell(s, m_patches[13], tx, 14u);
				stamp_cell(s, m_patches[13], tx, 15u);
			}
			++m_static_paints;
		}
#ifndef K_SLICE_NO_DYN
		// **Borrado de estelas** antes de pintar: restaura desde la imagen las casillas
		// donde estaba cada actor dos frames atras (la huella que queda en este buffer).
		if (m_tx != m_prev2x || m_ty != m_prev2y) {
			erase_cells(s, m_tx, m_ty, m_prev2x, m_prev2y);
		}
		if (m_room == 1u) {
			for (eng::u8 i = 0u; i < 3u; ++i) {
				Wolf& w = m_wolves[i];
				if (w.dead) {
					continue;
				}
				if (w.x != w.p2x || w.y != w.p2y) {
					erase_cells(s, w.x, w.y, w.p2x, w.p2y);
				}
				w.p2x = w.p1x;
				w.p2y = w.p1y;
				w.p1x = w.x;
				w.p1y = w.y;
			}
		}
		// Avance del historial del jugador (dos frames: bbox cur/prev2 cubre este buffer).
		m_prev2x = m_prev1x;
		m_prev2y = m_prev1y;
		m_prev1x = m_tx;
		m_prev1y = m_ty;
		// Puerta del torreon cerrada (parche solo mientras no se desbloquea).
		if (m_room == 1u && !m_facts.has(kDoorOpen)) {
			stamp_cell(s, m_patches[11], 19u, 7u);
		}
		// Cofre abierto (el cerrado va en la imagen cocinada).
		if (m_room == 2u && m_chest_open != 0u) {
			stamp_cell(s, m_patches[12], kChestX, kChestY);
		}
		// Lobos vivos.
		if (m_room == 1u) {
			for (eng::u8 i = 0u; i < 3u; ++i) {
				if (!m_wolves[i].dead) {
					stamp_cell(s, m_patches[2], m_wolves[i].x, m_wolves[i].y);
				}
			}
		}
		// Objetos del suelo.
		for (eng::usize i = 0u; i < m_ground.size(); ++i) {
			const GroundItems<8>::Entry& e = m_ground.at(i);
			eng::Ref<const ItemInstance> inst = m_items.get(e.item);
			if (!inst.valid()) {
				continue;
			}
			const eng::u8 tx = static_cast<eng::u8>(e.x >> 4u);
			const eng::u8 ty = static_cast<eng::u8>(e.y >> 4u);
			switch (inst->type) {
				case kItemPotion: stamp_cell(s, m_patches[6], tx, ty); break;
				case kItemPelt: stamp_cell(s, m_patches[7], tx, ty); break;
				case kItemKey: stamp_cell(s, m_patches[8], tx, ty); break;
				case kItemRelic: stamp_cell(s, m_patches[9], tx, ty); break;
				default: break;
			}
		}
		// Jugador (con espada si la lleva equipada).
		if (m_equip.slots[0].valid()) {
			stamp_cell(s, m_patches[1], m_tx, m_ty);
		} else {
			stamp_cell(s, m_patches[0], m_tx, m_ty);
		}
#endif
		draw_hud(s);
		app.present();
		eng::debug::mark_ready(g_eng_run_status, static_cast<eng::u32>(m_entry_count));
		eng::debug::probe_when_ready(g_eng_run_status, app.frame());
	} 	

};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	if (!backend.configure_game_memory()) {
		eng::debug::mark_failed(g_eng_run_status, 0x00040101u);
		return 0;
	}
	// **Espera activa de VBlank** (`set_vblank_sleep(false)`): el juego corre como proceso
	// AmigaDOS con el SO vivo y NO hace takeover del sistema; en ese escenario AmigaDOS
	// reescribe INTENA y el camino de `Wait()` de Exec puede morir (cuelgue observado con
	// PC en la ROM de dispatch). Es la opción documentada para demos sin takeover:
	// `docs/debugging/investigaciones/212-vblank-irq-apagada-por-el-so.md`.
	backend.set_vblank_sleep(false);

	eng::GameDisplay display {};
	display.width = kWidth;
	display.height = kHeight;
	display.color_depth = 4u;
	display.buffers = 2u;
	display.palette.color[1] = 0x222u;  // piedra / mazmorra (gris oscuro)
	display.palette.color[2] = 0xf80u;  // puerta cerrada (naranja)
	display.palette.color[3] = 0xf00u;  // jugador (rojo)
	display.palette.color[4] = 0xfd0u;  // cara / espada (dorado claro)
	display.palette.color[5] = 0x0f0u;  // pócima (verde)
	display.palette.color[6] = 0x06fu;  // agua (azul)
	display.palette.color[7] = 0x840u;  // sendero (marrón)
	display.palette.color[8] = 0x642u;  // cofre (marrón verdoso)
	display.palette.color[9] = 0x08fu;  // túnica de la anciana (azul)
	display.palette.color[10] = 0x165u; // árbol / lobo (verde oscuro)
	display.palette.color[11] = 0x80fu; // mercader (violeta)
	display.palette.color[12] = 0xfffu; // ojos del lobo (blanco)
	display.palette.color[13] = 0xbbbu; // texto del HUD
	display.palette.color[14] = 0x0ffu; // reliquia (cian)
	display.palette.color[15] = 0x0c0u; // hierba (verde claro)

	static RpgSlice game {};
	eng::App app {backend, game, backend.memory_manager()};
	if (!app.set_display(display) || !app.start()) {
		eng::debug::mark_failed(g_eng_run_status, 0x00040102u);
		return 0;
	}
	g_eng_run_status.detail = 0x40114u; // init + start completos; esperando primer render
	app.run(0xffffu);
	return 0;
}
