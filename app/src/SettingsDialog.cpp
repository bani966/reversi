#include "SettingsDialog.hpp"

#include "GameController.hpp"

#include "reversi/search.hpp"

#include <QCheckBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(GameController* controller, QWidget* parent)
    : QDialog(parent), controller_(controller) {
    setWindowTitle(QStringLiteral("Settings"));

    auto* layout = new QVBoxLayout(this);

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
