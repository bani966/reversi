#include "SettingsDialog.hpp"

#include "GameController.hpp"
#include "Palette.hpp"

#include "reversi/search.hpp"

#include <QCheckBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <dwmapi.h>
#include <windows.h>
#endif

namespace {
#ifdef Q_OS_WIN
// M10 phase 3: unlike MainWindow, this dialog has no custom frameless title bar (a much bigger
// lift - drag-to-move, corner rounding, hand-drawn minimize/maximize/close buttons - not
// attempted for a settings dialog) - its native OS title bar is otherwise completely outside Qt's
// styling reach and stays dark regardless of chrome::palette()'s own theme.
// DWMWA_USE_IMMERSIVE_DARK_MODE is the smaller, targeted fix: it picks which of DWM's two native
// title-bar renderings to use, independent of the user's own Windows-wide dark/light setting -
// toggled to follow THIS app's theme instead.
void applyTitleBarDarkMode(QWidget* window, bool dark) {
    const auto hwnd = reinterpret_cast<HWND>(window->winId());
    const BOOL useDarkMode = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode));
}
#endif

// M10 phase 3 (second attempt - the QSS-only fix didn't hold up in practice): QGroupBox::title's
// `color` QSS property doesn't reliably override Fusion's own title-paint color no matter how it's
// specified (tried both on ::title alone and duplicated onto the outer QGroupBox selector) - the
// title kept rendering in the same near-white tone as the light theme's own background. Setting
// QPalette::WindowText directly is what Fusion's group-box title painter actually reads from, so
// this bypasses the QSS cascade for this one specific piece of chrome entirely rather than trying
// a third QSS variant. findChildren(), not a stored list of the five QGroupBoxes, so any future
// section added to this dialog is covered automatically.
void applyGroupBoxTextColor(QWidget* dialog) {
    const QColor textColor = chrome::palette().textColor;
    for (QGroupBox* box : dialog->findChildren<QGroupBox*>()) {
        QPalette boxPalette = box->palette();
        boxPalette.setColor(QPalette::WindowText, textColor);
        box->setPalette(boxPalette);
    }
}

// M10 phase 3: chrome::panelControlsStyleSheet() styles this dialog's CHILDREN (QGroupBox,
// QPushButton, ...) but was never given a rule for the dialog's own base QWidget background - the
// space between/around the group boxes was left at Qt's own unstyled default the whole time, which
// doesn't track chrome::palette() at all. Invisible-by-accident in the dark theme (an unstyled
// default that happens to also read dark), impossible to miss in the light theme (a dark strip
// behind the now-correctly-dark group-box titles, right back to the same "text blends with its own
// background" problem those titles just got fixed for). Same setAutoFillBackground()+QPalette
// pattern MainWindow.cpp's own `container` widget already uses for exactly this reason.
void applyDialogBackground(QWidget* dialog) {
    dialog->setAutoFillBackground(true);
    QPalette dialogPalette = dialog->palette();
    dialogPalette.setColor(QPalette::Window, chrome::palette().windowBackground);
    dialog->setPalette(dialogPalette);
}
} // namespace

SettingsDialog::SettingsDialog(GameController* controller, QWidget* parent)
    : QDialog(parent), controller_(controller) {
    setWindowTitle(QStringLiteral("Settings"));
    // M9 phase 5: previously entirely unstyled (bare Fusion defaults) - the same shared
    // stylesheet panel_ uses (MainWindow.cpp), so this dialog's QGroupBox/QCheckBox/QPushButton/
    // QSpinBox/QLabel controls can't drift out of sync with the rest of the app's chrome.
    setStyleSheet(chrome::panelControlsStyleSheet());
    applyDialogBackground(this);
#ifdef Q_OS_WIN
    applyTitleBarDarkMode(this,
                          chrome::ThemeManager::instance().currentTheme() == chrome::Theme::Dark);
#endif

    auto* layout = new QVBoxLayout(this);

    // Appearance (M10 phase 3)
    auto* appearanceGroup = new QGroupBox(QStringLiteral("Appearance"), this);
    auto* appearanceLayout = new QVBoxLayout(appearanceGroup);
    auto* lightThemeCheck = new QCheckBox(QStringLiteral("Light theme"), appearanceGroup);
    lightThemeCheck->setChecked(chrome::ThemeManager::instance().currentTheme() ==
                                chrome::Theme::Light);
    connect(lightThemeCheck, &QCheckBox::toggled, this, [](bool checked) {
        chrome::ThemeManager::instance().setTheme(checked ? chrome::Theme::Light
                                                          : chrome::Theme::Dark);
    });
    appearanceLayout->addWidget(lightThemeCheck);
    layout->addWidget(appearanceGroup);

    // Board
    auto* boardGroup = new QGroupBox(QStringLiteral("Board"), this);
    auto* boardLayout = new QVBoxLayout(boardGroup);
    auto* highlightCheck = new QCheckBox(QStringLiteral("Highlight last move"), boardGroup);
    highlightCheck->setChecked(controller_->lastMoveHighlightEnabled());
    connect(highlightCheck, &QCheckBox::toggled, controller_,
            &GameController::setLastMoveHighlightEnabled);
    boardLayout->addWidget(highlightCheck);
    layout->addWidget(boardGroup);

    // Opening Book
    auto* bookGroup = new QGroupBox(QStringLiteral("Opening Book"), this);
    auto* bookLayout = new QVBoxLayout(bookGroup);
    auto* loadBookButton = new QPushButton(QStringLiteral("Load Opening Book..."), bookGroup);
    connect(loadBookButton, &QPushButton::clicked, this, &SettingsDialog::loadOpeningBook);
    bookLayout->addWidget(loadBookButton);
    openingBookStatusLabel_ = new QLabel(QStringLiteral("Not loaded"), bookGroup);
    bookLayout->addWidget(openingBookStatusLabel_);
    openingBookEnabledCheck_ = new QCheckBox(QStringLiteral("Enabled"), bookGroup);
    openingBookEnabledCheck_->setEnabled(controller_->hasOpeningBook());
    openingBookEnabledCheck_->setChecked(controller_->openingBookEnabled());
    connect(openingBookEnabledCheck_, &QCheckBox::toggled, controller_,
            &GameController::setOpeningBookEnabled);
    bookLayout->addWidget(openingBookEnabledCheck_);
    layout->addWidget(bookGroup);

    // Multi-ProbCut
    auto* mpcGroup = new QGroupBox(QStringLiteral("Multi-ProbCut"), this);
    auto* mpcLayout = new QVBoxLayout(mpcGroup);
    auto* loadMpcButton = new QPushButton(QStringLiteral("Load MPC Model..."), mpcGroup);
    connect(loadMpcButton, &QPushButton::clicked, this, &SettingsDialog::loadMpcModel);
    mpcLayout->addWidget(loadMpcButton);
    mpcStatusLabel_ = new QLabel(QStringLiteral("Not loaded"), mpcGroup);
    mpcLayout->addWidget(mpcStatusLabel_);
    mpcEnabledCheck_ = new QCheckBox(QStringLiteral("Enabled"), mpcGroup);
    mpcEnabledCheck_->setEnabled(controller_->hasMpcModel());
    mpcEnabledCheck_->setChecked(controller_->mpcEnabled());
    connect(mpcEnabledCheck_, &QCheckBox::toggled, controller_, &GameController::setMpcEnabled);
    mpcLayout->addWidget(mpcEnabledCheck_);
    layout->addWidget(mpcGroup);

    // AI Strength
    auto* strengthGroup = new QGroupBox(QStringLiteral("AI Strength"), this);
    auto* strengthLayout = new QVBoxLayout(strengthGroup);

    strengthLayout->addWidget(new QLabel(QStringLiteral("Max search depth"), strengthGroup));
    auto* depthSpin = new QSpinBox(strengthGroup);
    depthSpin->setRange(1, 60);
    depthSpin->setValue(controller_->aiMaxSearchDepth());
    connect(depthSpin, qOverload<int>(&QSpinBox::valueChanged), controller_,
            &GameController::setAiMaxSearchDepth);
    strengthLayout->addWidget(depthSpin);

    strengthLayout->addWidget(new QLabel(QStringLiteral("Soft time budget (ms)"), strengthGroup));
    auto* softBudgetSpin = new QSpinBox(strengthGroup);
    softBudgetSpin->setRange(100, 60000);
    softBudgetSpin->setSingleStep(100);
    softBudgetSpin->setValue(static_cast<int>(controller_->aiTimeBudget().soft.count()));
    strengthLayout->addWidget(softBudgetSpin);

    strengthLayout->addWidget(new QLabel(QStringLiteral("Hard time budget (ms)"), strengthGroup));
    auto* hardBudgetSpin = new QSpinBox(strengthGroup);
    hardBudgetSpin->setRange(100, 60000);
    hardBudgetSpin->setSingleStep(100);
    hardBudgetSpin->setValue(static_cast<int>(controller_->aiTimeBudget().hard.count()));
    strengthLayout->addWidget(hardBudgetSpin);

    // Soft/hard are set together (GameController::setAiTimeBudget takes both) - either spin box
    // changing re-reads its sibling's current value rather than needing a third "Apply" step.
    connect(softBudgetSpin, qOverload<int>(&QSpinBox::valueChanged), this,
            [this, hardBudgetSpin](int softMs) {
                controller_->setAiTimeBudget(softMs, hardBudgetSpin->value());
            });
    connect(hardBudgetSpin, qOverload<int>(&QSpinBox::valueChanged), this,
            [this, softBudgetSpin](int hardMs) {
                controller_->setAiTimeBudget(softBudgetSpin->value(), hardMs);
            });

    strengthLayout->addWidget(
        new QLabel(QStringLiteral("Exact solver threshold (empties)"), strengthGroup));
    auto* thresholdSpin = new QSpinBox(strengthGroup);
    thresholdSpin->setRange(0, 30);
    thresholdSpin->setValue(controller_->exactSolverEmptyThreshold());
    connect(thresholdSpin, qOverload<int>(&QSpinBox::valueChanged), controller_,
            &GameController::setExactSolverEmptyThreshold);
    strengthLayout->addWidget(thresholdSpin);

    layout->addWidget(strengthGroup);

    auto* closeButton = new QPushButton(QStringLiteral("Close"), this);
    connect(closeButton, &QPushButton::clicked, this, &SettingsDialog::close);
    layout->addWidget(closeButton);

    // All five QGroupBoxes exist past this point - see applyGroupBoxTextColor()'s own comment for
    // why this can't be QSS alone.
    applyGroupBoxTextColor(this);

    // M10 phase 3: this dialog is non-modal (MainWindow calls show(), not exec() - see the class's
    // own doc comment) and could be left open across a theme switch triggered from its own
    // checkbox above - re-apply the shared stylesheet (and, on Windows, the native title bar's
    // dark-mode attribute, and the dialog's own background, and the group-box title color)
    // immediately rather than waiting for it to be closed and reopened.
    connect(&chrome::ThemeManager::instance(), &chrome::ThemeManager::themeChanged, this,
            [this](chrome::Theme newTheme) {
                setStyleSheet(chrome::panelControlsStyleSheet());
                applyDialogBackground(this);
                applyGroupBoxTextColor(this);
#ifdef Q_OS_WIN
                applyTitleBarDarkMode(this, newTheme == chrome::Theme::Dark);
#endif
            });
}

void SettingsDialog::loadOpeningBook() {
    const QString path =
        QFileDialog::getOpenFileName(this, QStringLiteral("Load Opening Book"), QString(),
                                     QStringLiteral("Opening Book (*.bin)"));
    if (path.isEmpty()) {
        return;
    }
    if (!controller_->loadOpeningBook(path)) {
        QMessageBox::warning(this, QStringLiteral("Load Opening Book"),
                             controller_->lastErrorMessage());
        return;
    }
    openingBookStatusLabel_->setText(QFileInfo(path).fileName());
    openingBookEnabledCheck_->setEnabled(true);
    openingBookEnabledCheck_->setChecked(true);
}

void SettingsDialog::loadMpcModel() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Load MPC Model"), QString(), QStringLiteral("MPC Model (*.bin)"));
    if (path.isEmpty()) {
        return;
    }
    if (!controller_->loadMpcModel(path)) {
        QMessageBox::warning(this, QStringLiteral("Load MPC Model"),
                             controller_->lastErrorMessage());
        return;
    }
    mpcStatusLabel_->setText(QFileInfo(path).fileName());
    mpcEnabledCheck_->setEnabled(true);
    mpcEnabledCheck_->setChecked(true);
}
