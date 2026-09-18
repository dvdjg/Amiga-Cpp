#pragma once

/// \file templates.hpp
/// **Plantillas de frases** del explicador de ajedrez (NLG por plantillas). Cada
/// plantilla existe en español e inglés y puede tener variante neutra y enfática.
///
/// Los huecos son `{side}`, `{side_adj}`, `{diff}`, `{piece}` y `{square}`; el
/// motor de `explain.hpp` los sustituye en tiempo de ejecución. No hay ningún modelo
/// de lenguaje: se eligen y combinan frases preescritas según los rasgos de la
/// posición, que es lo que hace viable el sistema en un A500 (8–50 kB).
///
/// En el futuro las plantillas se cargarán desde `assets/amiga/board/*/explain/` y
/// se empaquetarán como bloques; estas constantes son el pack mínimo incorporado.
///
/// Verificación: HOST-149.

namespace eng::board::chess {

/// Par de frases (español, inglés).
struct Phrase {
	const char* es;
	const char* en;
};

// --- Material ---
inline constexpr Phrase kPhraseMaterial {
    "Las {side} tienen ventaja material de {diff} peones.",
    "{side} has a material advantage of {diff} pawns."};
inline constexpr Phrase kPhraseMaterialEmphatic {
    "¡Las {side} tienen una ventaja material decisiva de {diff} peones!",
    "{side} has a decisive material advantage of {diff} pawns!"};

// --- Desarrollo ---
inline constexpr Phrase kPhraseQueenEarly {
    "Las {side} han sacado la dama demasiado pronto y podrá ser hostigada.",
    "{side} has developed the queen prematurely and it may be harassed."};
inline constexpr Phrase kPhraseUndeveloped {
    "Las {side} tienen un retraso en el desarrollo de las piezas menores.",
    "{side} is behind in the development of the minor pieces."};
inline constexpr Phrase kPhraseKingInCenter {
    "El rey de las {side} sigue en el centro y puede volverse vulnerable.",
    "The {side_adj} king remains in the centre and may become vulnerable."};

// --- Táctica inmediata ---
inline constexpr Phrase kPhraseInCheck {
    "El rey de las {side} está en jaque.",
    "The {side_adj} king is in check."};
inline constexpr Phrase kPhraseInCheckEmphatic {
    "¡El rey de las {side} está en jaque!",
    "The {side_adj} king is in check!"};

// --- Conectores entre frases ---
inline constexpr Phrase kConnectorSecond {
    "Además, ",
    "Moreover, "};
inline constexpr Phrase kConnectorThird {
    "Por otro lado, ",
    "On the other hand, "};

} // namespace eng::board::chess
