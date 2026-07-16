#pragma once

#include <QDialog>

class GameController;
class QCheckBox;
class QLabel;

// M9 phase 4: the first QDialog in this app (no prior QTabWidget/QDialog precedent existed -
// every other file-picking flow uses QFileDialog's static helpers, and errors use
// QMessageBox::warning - both stay in use here too). Non-modal (MainWindow calls show(), not
// exec()) so the user can keep playing/watching an AI-vs-AI game while adjusting settings.
// Every control applies LIVE as changed - no OK/Apply/Cancel step - mirroring
// GameController::setLastMoveHighlightEnabled()'s own "reflect the change immediately" doc
// comment. Nothing here is persisted across app restarts or saved into a game's JSON; see
// GameController.hpp's own doc comment on the new settings surface for why.
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(GameController* controller, QWidget* parent = nullptr);

private:
    GameController* controller_; // non-owning, same pattern as MainWindow's own controller_

    QCheckBox* openingBookEnabledCheck_;
    QLabel* openingBookStatusLabel_;
    QCheckBox* mpcEnabledCheck_;
    QLabel* mpcStatusLabel_;

    void loadOpeningBook();
    void loadMpcModel();
};
