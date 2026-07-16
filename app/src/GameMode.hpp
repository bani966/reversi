#pragma once

// Split out of GameController.hpp (M9 phase 2) so GameNotation.hpp - which needs this enum for
// its save-file schema - doesn't have to include GameController.hpp itself. GameController.hpp
// pulls in BoardWidget.hpp (QWidget), which would force GameNotation.hpp's dependents into
// linking Qt6::Widgets; GameNotation is meant to stay pure QtCore-only so app_tests can test it
// without any GUI dependency. This header has zero Qt dependency of its own.
enum class GameMode {
    HumanVsHuman,
    HumanIsBlack,
    HumanIsWhite,
    AiVsAi, // M9 phase 4: both sides driven by the AI, no human turn ever
};
