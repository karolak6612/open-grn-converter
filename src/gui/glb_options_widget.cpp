#include "glb_options_widget.h"
#include "../gltf/glb_reader.h"

#include <oclero/qlementine/style/QlementineStyle.hpp>
#include <oclero/qlementine/utils/IconUtils.hpp>
#include <oclero/qlementine/widgets/Switch.hpp>
#include <oclero/qlementine/icons/Icons16.hpp>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QLabel>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QFileInfo>
#include <QFrame>
#include "section_card.h"
#include "gui_utils.h"

namespace grn {

using Icons16 = oclero::qlementine::icons::Icons16;

struct GlbOptionsWidget::Impl {
    GlbOptionsWidget& owner;

    oclero::qlementine::Switch* coordSwitch{ nullptr };
    QComboBox* scaleModeCombo{ nullptr };
    QDoubleSpinBox* scaleFactorSpin{ nullptr };
    QDoubleSpinBox* targetHeightSpin{ nullptr };
    oclero::qlementine::Switch* vtexCompressSwitch{ nullptr };
    oclero::qlementine::Switch* splitAnimsSwitch{ nullptr };
    oclero::qlementine::Switch* autoSplit16BitSwitch{ nullptr };
    QLabel* optimizerStatusLabel{ nullptr };
    size_t lastAnalyzedMaxVerts{ 0 };
    bool lastAnalyzedExceeded{ false };
    bool userManuallyToggled{ false };

    // Animation Optimization controls
    oclero::qlementine::Switch* animOptimizerSwitch{ nullptr };
    QComboBox* animFpsCombo{ nullptr };
    QDoubleSpinBox* animCustomFpsSpin{ nullptr };
    QComboBox* animCullCombo{ nullptr };
    oclero::qlementine::Switch* animLoopSafeSwitch{ nullptr };
    QLabel* animStatusLabel{ nullptr };

    QWidget* animSectionWidget{ nullptr };
    QLabel* animLabel{ nullptr };
    QTreeWidget* treeWidget{ nullptr };
    QWidget* bottomStretch{ nullptr };

    QString currentModelPath;

    explicit Impl(GlbOptionsWidget& o) : owner(o) {
        setupUI();
    }

    void setupUI() {
        auto* layout = new QVBoxLayout(&owner);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);

        // Section 1: Compiler Options
        auto* optLabel = new QLabel(owner.tr("Compiler Options"), &owner);
        QFont hf = optLabel->font();
        hf.setBold(true);
        optLabel->setFont(hf);
        layout->addWidget(optLabel);

        auto* optCard = new SectionCard(&owner);
        auto* optLayout = new QFormLayout(optCard);
        optLayout->setContentsMargins(10, 8, 10, 8);
        optLayout->setVerticalSpacing(4);
        optLayout->setHorizontalSpacing(6);

        coordSwitch = new oclero::qlementine::Switch(optCard);
        coordSwitch->setChecked(true);
        coordSwitch->setToolTip(owner.tr("Convert glTF standard Y-up coordinates to Granny native Z-up (recommended for Sacred Gold and Granny engines)"));
        QObject::connect(coordSwitch, &oclero::qlementine::Switch::clicked, &owner, [this]() {
            emit owner.optionsChanged();
        });
        optLayout->addRow(owner.tr("Convert Y-up → Z-up:"), coordSwitch);

        scaleModeCombo = new QComboBox(optCard);
        scaleModeCombo->addItems({owner.tr("Uniform Multiplier"), owner.tr("Target Height")});
        scaleModeCombo->setFixedHeight(24);
        QObject::connect(scaleModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &owner, [this](int idx) {
            scaleFactorSpin->setEnabled(idx == 0);
            targetHeightSpin->setEnabled(idx == 1);
            emit owner.optionsChanged();
        });
        optLayout->addRow(owner.tr("Scale Mode:"), scaleModeCombo);

        scaleFactorSpin = new QDoubleSpinBox(optCard);
        scaleFactorSpin->setRange(0.0001, 1000.0);
        scaleFactorSpin->setDecimals(4);
        scaleFactorSpin->setValue(1.0000);
        scaleFactorSpin->setSingleStep(0.1);
        scaleFactorSpin->setSuffix("x");
        scaleFactorSpin->setFixedHeight(24);
        QObject::connect(scaleFactorSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), &owner, [this](double) {
            emit owner.optionsChanged();
        });
        optLayout->addRow(owner.tr("Scale Factor:"), scaleFactorSpin);

        targetHeightSpin = new QDoubleSpinBox(optCard);
        targetHeightSpin->setRange(0.0, 10000.0);
        targetHeightSpin->setDecimals(2);
        targetHeightSpin->setValue(0.0);
        targetHeightSpin->setSingleStep(1.0);
        targetHeightSpin->setSuffix(" units");
        targetHeightSpin->setEnabled(false);
        targetHeightSpin->setFixedHeight(24);
        QObject::connect(targetHeightSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), &owner, [this](double) {
            emit owner.optionsChanged();
        });
        optLayout->addRow(owner.tr("Target Height:"), targetHeightSpin);

        vtexCompressSwitch = new oclero::qlementine::Switch(optCard);
        vtexCompressSwitch->setChecked(true);
        vtexCompressSwitch->setToolTip(owner.tr("Compress textures with Granny 1.2b native VTex video codec"));
        QObject::connect(vtexCompressSwitch, &oclero::qlementine::Switch::clicked, &owner, [this]() {
            emit owner.optionsChanged();
        });
        optLayout->addRow(owner.tr("VTex Compression:"), vtexCompressSwitch);

        splitAnimsSwitch = new oclero::qlementine::Switch(optCard);
        splitAnimsSwitch->setChecked(true);
        splitAnimsSwitch->setToolTip(owner.tr("Split multiple animation clips into standalone .grn files"));
        QObject::connect(splitAnimsSwitch, &oclero::qlementine::Switch::clicked, &owner, [this]() {
            const bool split = splitAnimsSwitch->isChecked();
            animSectionWidget->setVisible(split);
            bottomStretch->setVisible(!split);
            emit owner.optionsChanged();
        });
        optLayout->addRow(owner.tr("Split Animations:"), splitAnimsSwitch);

        animOptimizerSwitch = new oclero::qlementine::Switch(optCard);
        animOptimizerSwitch->setChecked(false);
        animOptimizerSwitch->setToolTip(owner.tr("Prune static rest-pose bone tracks and redundant keyframes to prevent 32-bit memory exhaustion and crashes in Granny viewers and engine"));
        QObject::connect(animOptimizerSwitch, &oclero::qlementine::Switch::clicked, &owner, [this]() {
            updateAnimControlsState();
            emit owner.optionsChanged();
        });
        optLayout->addRow(owner.tr("Animation Optimizer:"), animOptimizerSwitch);

        auto* fpsRow = new QHBoxLayout();
        fpsRow->setContentsMargins(0, 0, 0, 0);
        fpsRow->setSpacing(6);

        animFpsCombo = new QComboBox(optCard);
        animFpsCombo->addItems({
            owner.tr("Source FPS (Unchanged)"),
            owner.tr("30 FPS (Standard)"),
            owner.tr("20 FPS (Optimized)"),
            owner.tr("15 FPS (Aggressive)"),
            owner.tr("Custom FPS")
        });
        animFpsCombo->setFixedHeight(24);
        fpsRow->addWidget(animFpsCombo, 1);

        animCustomFpsSpin = new QDoubleSpinBox(optCard);
        animCustomFpsSpin->setRange(1.0, 120.0);
        animCustomFpsSpin->setDecimals(1);
        animCustomFpsSpin->setValue(30.0);
        animCustomFpsSpin->setSingleStep(5.0);
        animCustomFpsSpin->setSuffix(" FPS");
        animCustomFpsSpin->setFixedHeight(24);
        animCustomFpsSpin->setEnabled(false);
        fpsRow->addWidget(animCustomFpsSpin, 0);

        QObject::connect(animFpsCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &owner, [this](int idx) {
            animCustomFpsSpin->setEnabled(animOptimizerSwitch->isChecked() && idx == 4);
            emit owner.optionsChanged();
        });
        QObject::connect(animCustomFpsSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), &owner, [this](double) {
            emit owner.optionsChanged();
        });
        optLayout->addRow(owner.tr("Resample Frame Rate:"), fpsRow);

        animCullCombo = new QComboBox(optCard);
        animCullCombo->addItems({
            owner.tr("Off (Keep all bones)"),
            owner.tr("1.5° (Subtle noise)"),
            owner.tr("3.0° (Recommended >600 bones)"),
            owner.tr("5.0° (Aggressive)")
        });
        animCullCombo->setFixedHeight(24);
        animCullCombo->setToolTip(owner.tr("Prune micro-rotations below threshold to prevent 32-bit viewer memory exhaustion on massive rigs (>600 bones)"));
        QObject::connect(animCullCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &owner, [this](int) {
            emit owner.optionsChanged();
        });
        optLayout->addRow(owner.tr("Micro-Bone Culling:"), animCullCombo);

        animLoopSafeSwitch = new oclero::qlementine::Switch(optCard);
        animLoopSafeSwitch->setChecked(false);
        animLoopSafeSwitch->setToolTip(owner.tr("Enforce exact matching start and end keyframes to eliminate animation loop seam popping"));
        QObject::connect(animLoopSafeSwitch, &oclero::qlementine::Switch::clicked, &owner, [this]() {
            emit owner.optionsChanged();
        });
        optLayout->addRow(owner.tr("Loop-Safe Clamping:"), animLoopSafeSwitch);

        animStatusLabel = new QLabel(owner.tr("(Auto-detected on load)"), optCard);
        animStatusLabel->setStyleSheet("font-size: 11px; color: #888888;");
        optLayout->addRow(owner.tr("Rig Safety Status:"), animStatusLabel);

        auto* optRow = new QHBoxLayout();
        optRow->setContentsMargins(0, 0, 0, 0);
        optRow->setSpacing(6);

        autoSplit16BitSwitch = new oclero::qlementine::Switch(optCard);
        autoSplit16BitSwitch->setChecked(false);
        autoSplit16BitSwitch->setToolTip(owner.tr("Automatically partition high-poly meshes (>64,000 vertices) into 16-bit safe sub-meshes to prevent Granny 1.2b / Sacred Gold crashes. Auto-detected on model load."));
        optRow->addWidget(autoSplit16BitSwitch);

        optimizerStatusLabel = new QLabel(owner.tr("(Auto-detected on load)"), optCard);
        optimizerStatusLabel->setStyleSheet("font-size: 11px; color: #888888;");
        optRow->addWidget(optimizerStatusLabel);
        optRow->addStretch(1);

        QObject::connect(autoSplit16BitSwitch, &oclero::qlementine::Switch::clicked, &owner, [this]() {
            userManuallyToggled = true;
            updateOptimizerStatusText();
            emit owner.optionsChanged();
        });
        optLayout->addRow(owner.tr("Mesh Optimizer (16-bit):"), optRow);

        updateAnimControlsState();

        layout->addWidget(optCard);

        // Section 2: Model & Animations
        animSectionWidget = new QWidget(&owner);
        animSectionWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        auto* animLayout = new QVBoxLayout(animSectionWidget);
        animLayout->setContentsMargins(0, 4, 0, 0);
        animLayout->setSpacing(4);

        auto* animTitle = new QLabel(owner.tr("Model & Animations"), animSectionWidget);
        animTitle->setFont(hf);
        animLayout->addWidget(animTitle);

        auto* animCard = new SectionCard(animSectionWidget);
        animCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        auto* cardLayout = new QVBoxLayout(animCard);
        cardLayout->setContentsMargins(6, 6, 6, 6);

        treeWidget = new QTreeWidget(animCard);
        treeWidget->setStyleSheet("QTreeWidget { background: transparent; border: none; } QTreeWidget::item { padding: 2px 0; }");
        treeWidget->setIconSize(QSize(16, 16));
        treeWidget->setColumnCount(1);
        treeWidget->setHeaderHidden(true);
        treeWidget->setAlternatingRowColors(false);
        treeWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
        treeWidget->setIndentation(16);
        treeWidget->setMinimumHeight(60);
        treeWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        cardLayout->addWidget(treeWidget);

        QObject::connect(treeWidget, &QTreeWidget::currentItemChanged, &owner, [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
            if (!current) {
                emit owner.animationSelected(-1);
                return;
            }
            int animIdx = current->data(0, Qt::UserRole).isValid() ? current->data(0, Qt::UserRole).toInt() : -1;
            emit owner.animationSelected(animIdx);
        });

        animLayout->addWidget(animCard, 1);

        layout->addWidget(animSectionWidget, 1);

        bottomStretch = new QWidget(&owner);
        bottomStretch->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
        bottomStretch->setVisible(false);
        layout->addWidget(bottomStretch, 1);

        rebuildTree();
    }

    void rebuildTree() {
        treeWidget->clear();

        if (currentModelPath.isEmpty()) {
            auto* placeholder = new QTreeWidgetItem(treeWidget);
            placeholder->setText(0, owner.tr("No model selected (drop or browse a file)"));
            placeholder->setFlags(Qt::NoItemFlags);
            return;
        }

        QFileInfo fi(currentModelPath);
        if (!fi.exists()) {
            auto* placeholder = new QTreeWidgetItem(treeWidget);
            placeholder->setText(0, owner.tr("File not found: ") + fi.fileName());
            placeholder->setFlags(Qt::NoItemFlags);
            return;
        }

        auto model = load_glb_file(fi.filesystemFilePath());
        auto* rootItem = new QTreeWidgetItem(treeWidget);
        rootItem->setIcon(0, makeThemedIcon(Icons16::Misc_Blocks));

        if (model) {
            rootItem->setText(0, QString("%1 (%2 meshes, %3 joints)")
                .arg(fi.fileName())
                .arg(model->meshes.size())
                .arg(model->bones.size()));
            rootItem->setExpanded(true);
            rootItem->setData(0, Qt::UserRole, -1);

            if (model->animations.empty()) {
                auto* infoItem = new QTreeWidgetItem(rootItem);
                infoItem->setText(0, owner.tr("(No animation clips found in model)"));
                infoItem->setFlags(Qt::NoItemFlags);
            } else {
                QString baseName = fi.completeBaseName();
                for (size_t ai = 0; ai < model->animations.size(); ++ai) {
                    const auto& anim = model->animations[ai];
                    auto* animItem = new QTreeWidgetItem(rootItem);
                    animItem->setIcon(0, makeThemedIcon(Icons16::Media_Play));
                    QString aName = QString::fromStdString(anim.name);
                    QString targetAnimName;
                    if (aName.startsWith(baseName + "_", Qt::CaseInsensitive) || aName.startsWith(baseName + "-", Qt::CaseInsensitive)) {
                        targetAnimName = aName;
                    } else if (aName.compare(baseName, Qt::CaseInsensitive) == 0) {
                        targetAnimName = baseName + "_anim";
                    } else {
                        targetAnimName = baseName + "_" + aName;
                    }
                    animItem->setText(0, QString("%1 (%2s) → %3.grn")
                        .arg(aName)
                        .arg(anim.duration, 0, 'f', 2)
                        .arg(targetAnimName));
                    animItem->setData(0, Qt::UserRole, static_cast<int>(ai));
                }
            }
        } else {
            rootItem->setText(0, fi.fileName());
            rootItem->setData(0, Qt::UserRole, -1);
        }
    }

    void updateOptimizerStatusText() {
        if (!optimizerStatusLabel) return;
        bool enabled = autoSplit16BitSwitch->isChecked();

        if (userManuallyToggled) {
            if (lastAnalyzedExceeded && !enabled) {
                optimizerStatusLabel->setText(owner.tr("Manual: OFF ⚠️ (May crash Sacred!)"));
                optimizerStatusLabel->setStyleSheet("font-size: 11px; color: #d88000; font-weight: bold;");
            } else if (enabled) {
                optimizerStatusLabel->setText(owner.tr("Manual: ON (Safety active)"));
                optimizerStatusLabel->setStyleSheet("font-size: 11px; color: #0078d7; font-weight: bold;");
            } else {
                optimizerStatusLabel->setText(owner.tr("Manual: OFF"));
                optimizerStatusLabel->setStyleSheet("font-size: 11px; color: #888888;");
            }
        } else {
            if (lastAnalyzedExceeded) {
                optimizerStatusLabel->setText(owner.tr("Auto: ON (>65k detected)"));
                optimizerStatusLabel->setStyleSheet("font-size: 11px; color: #d88000; font-weight: bold;");
            } else if (lastAnalyzedMaxVerts > 0) {
                optimizerStatusLabel->setText(owner.tr("Auto: OFF (Safe <65k)"));
                optimizerStatusLabel->setStyleSheet("font-size: 11px; color: #28a745;");
            } else {
                optimizerStatusLabel->setText(owner.tr("(Auto-detected on load)"));
                optimizerStatusLabel->setStyleSheet("font-size: 11px; color: #888888;");
            }
        }
    }

    void updateAnimControlsState() {
        bool opt = animOptimizerSwitch ? animOptimizerSwitch->isChecked() : false;
        if (animFpsCombo) animFpsCombo->setEnabled(opt);
        if (animCustomFpsSpin) animCustomFpsSpin->setEnabled(opt && animFpsCombo && animFpsCombo->currentIndex() == 4);
        if (animCullCombo) animCullCombo->setEnabled(opt);
        if (animLoopSafeSwitch) animLoopSafeSwitch->setEnabled(opt);
    }

    void setModelAnalysis(const GrnModel* model) {
        userManuallyToggled = false;
        if (!model || model->meshes.empty()) {
            lastAnalyzedMaxVerts = 0;
            lastAnalyzedExceeded = false;
            autoSplit16BitSwitch->setChecked(false);
            updateOptimizerStatusText();
        } else {
            size_t maxVerts = 0;
            for (const auto& m : model->meshes) {
                maxVerts = std::max(maxVerts, m.vertices.size());
            }

            lastAnalyzedMaxVerts = maxVerts;
            lastAnalyzedExceeded = (maxVerts > 64000);

            // Auto-detect: turn ON if > 64000, turn OFF if <= 64000
            autoSplit16BitSwitch->setChecked(lastAnalyzedExceeded);
            updateOptimizerStatusText();
        }

        // Rig safety analysis
        if (animStatusLabel) {
            size_t numBones = model ? model->bones.size() : 0;
            if (numBones > 600) {
                animStatusLabel->setText(owner.tr("Heavy Rig: %1 bones! (3.0° culling recommended)").arg(numBones));
                animStatusLabel->setStyleSheet("font-size: 11px; color: #d88000; font-weight: bold;");
                if (animCullCombo && animCullCombo->currentIndex() == 0) {
                    animCullCombo->setCurrentIndex(2); // Auto-suggest 3.0°
                }
            } else if (numBones > 0) {
                animStatusLabel->setText(owner.tr("Rig: %1 bones (Safe)").arg(numBones));
                animStatusLabel->setStyleSheet("font-size: 11px; color: #28a745;");
            } else {
                animStatusLabel->setText(owner.tr("No skeletal rig in model"));
                animStatusLabel->setStyleSheet("font-size: 11px; color: #888888;");
            }
        }
    }

    void reset() {
        coordSwitch->setChecked(true);
        scaleModeCombo->setCurrentIndex(0);
        scaleFactorSpin->setValue(1.0);
        scaleFactorSpin->setEnabled(true);
        targetHeightSpin->setValue(0.0);
        targetHeightSpin->setEnabled(false);
        vtexCompressSwitch->setChecked(true);
        splitAnimsSwitch->setChecked(true);
        if (animOptimizerSwitch) animOptimizerSwitch->setChecked(false);
        if (animFpsCombo) animFpsCombo->setCurrentIndex(0);
        if (animCustomFpsSpin) {
            animCustomFpsSpin->setValue(30.0);
            animCustomFpsSpin->setEnabled(false);
        }
        if (animCullCombo) animCullCombo->setCurrentIndex(0);
        if (animLoopSafeSwitch) animLoopSafeSwitch->setChecked(false);
        if (animStatusLabel) {
            animStatusLabel->setText(owner.tr("(Auto-detected on load)"));
            animStatusLabel->setStyleSheet("font-size: 11px; color: #888888;");
        }
        autoSplit16BitSwitch->setChecked(false);
        userManuallyToggled = false;
        lastAnalyzedMaxVerts = 0;
        lastAnalyzedExceeded = false;
        updateOptimizerStatusText();
        updateAnimControlsState();
        animSectionWidget->setVisible(true);
        bottomStretch->setVisible(false);
        rebuildTree();
        emit owner.optionsChanged();
    }
};

GlbOptionsWidget::GlbOptionsWidget(QWidget* parent)
    : QWidget(parent)
    , _impl(std::make_unique<Impl>(*this)) {}

GlbOptionsWidget::~GlbOptionsWidget() = default;

void GlbOptionsWidget::setModel(const QString& modelPath) {
    _impl->currentModelPath = modelPath;
    _impl->rebuildTree();
}

bool GlbOptionsWidget::convertCoordinates() const {
    return _impl->coordSwitch->isChecked();
}

void GlbOptionsWidget::setConvertCoordinates(bool convert) {
    if (_impl->coordSwitch && _impl->coordSwitch->isChecked() != convert) {
        _impl->coordSwitch->setChecked(convert);
        emit optionsChanged();
    }
}

bool GlbOptionsWidget::isTargetHeightMode() const {
    return _impl->scaleModeCombo->currentIndex() == 1;
}

float GlbOptionsWidget::scaleFactor() const {
    return static_cast<float>(_impl->scaleFactorSpin->value());
}

float GlbOptionsWidget::targetHeight() const {
    return static_cast<float>(_impl->targetHeightSpin->value());
}

bool GlbOptionsWidget::compressVTex() const {
    return _impl->vtexCompressSwitch->isChecked();
}

bool GlbOptionsWidget::splitAnimations() const {
    return _impl->splitAnimsSwitch->isChecked();
}

bool GlbOptionsWidget::autoSplit16Bit() const {
    return isMeshOptimizerEnabled();
}

bool GlbOptionsWidget::isMeshOptimizerEnabled() const {
    return _impl->autoSplit16BitSwitch->isChecked();
}

float GlbOptionsWidget::animTargetFps() const {
    if (!_impl->animOptimizerSwitch || !_impl->animOptimizerSwitch->isChecked()) return 0.0f;
    int idx = _impl->animFpsCombo ? _impl->animFpsCombo->currentIndex() : 0;
    switch (idx) {
        case 1: return 30.0f;
        case 2: return 20.0f;
        case 3: return 15.0f;
        case 4: return _impl->animCustomFpsSpin ? static_cast<float>(_impl->animCustomFpsSpin->value()) : 30.0f;
        default: return 0.0f; // Source FPS
    }
}

float GlbOptionsWidget::animMinRotationDeg() const {
    if (!_impl->animOptimizerSwitch || !_impl->animOptimizerSwitch->isChecked()) return 0.0f;
    int idx = _impl->animCullCombo ? _impl->animCullCombo->currentIndex() : 0;
    switch (idx) {
        case 1: return 1.5f;
        case 2: return 3.0f;
        case 3: return 5.0f;
        default: return 0.0f;
    }
}

bool GlbOptionsWidget::animLoopSafe() const {
    if (!_impl->animOptimizerSwitch || !_impl->animOptimizerSwitch->isChecked()) return false;
    return _impl->animLoopSafeSwitch ? _impl->animLoopSafeSwitch->isChecked() : false;
}

void GlbOptionsWidget::setAnimTargetFps(float fps) {
    if (!_impl->animFpsCombo) return;
    if (std::abs(fps - 0.0f) < 0.1f) {
        _impl->animFpsCombo->setCurrentIndex(0);
    } else if (std::abs(fps - 30.0f) < 0.1f) {
        _impl->animFpsCombo->setCurrentIndex(1);
    } else if (std::abs(fps - 20.0f) < 0.1f) {
        _impl->animFpsCombo->setCurrentIndex(2);
    } else if (std::abs(fps - 15.0f) < 0.1f) {
        _impl->animFpsCombo->setCurrentIndex(3);
    } else {
        _impl->animFpsCombo->setCurrentIndex(4);
        if (_impl->animCustomFpsSpin) {
            _impl->animCustomFpsSpin->setValue(fps);
        }
    }
    emit optionsChanged();
}

void GlbOptionsWidget::setAnimMinRotationDeg(float deg) {
    if (!_impl->animCullCombo) return;
    if (deg >= 4.5f) {
        _impl->animCullCombo->setCurrentIndex(3);
    } else if (deg >= 2.5f) {
        _impl->animCullCombo->setCurrentIndex(2);
    } else if (deg >= 1.0f) {
        _impl->animCullCombo->setCurrentIndex(1);
    } else {
        _impl->animCullCombo->setCurrentIndex(0);
    }
    emit optionsChanged();
}

void GlbOptionsWidget::setAnimLoopSafe(bool safe) {
    if (_impl->animLoopSafeSwitch) {
        _impl->animLoopSafeSwitch->setChecked(safe);
        emit optionsChanged();
    }
}

void GlbOptionsWidget::setMeshOptimizerEnabled(bool enabled) {
    _impl->userManuallyToggled = true;
    _impl->autoSplit16BitSwitch->setChecked(enabled);
    _impl->updateOptimizerStatusText();
    emit optionsChanged();
}

bool GlbOptionsWidget::isAnimOptimizerEnabled() const {
    return _impl->animOptimizerSwitch ? _impl->animOptimizerSwitch->isChecked() : false;
}

void GlbOptionsWidget::setAnimOptimizerEnabled(bool enabled) {
    if (_impl->animOptimizerSwitch) {
        _impl->animOptimizerSwitch->setChecked(enabled);
        emit optionsChanged();
    }
}

void GlbOptionsWidget::setModelAnalysis(const GrnModel* model) {
    _impl->setModelAnalysis(model);
}

void GlbOptionsWidget::setSplitAnimations(bool split) {
    _impl->splitAnimsSwitch->setChecked(split);
    _impl->animSectionWidget->setVisible(split);
    _impl->bottomStretch->setVisible(!split);
    emit optionsChanged();
}

void GlbOptionsWidget::selectAnimationItem(int index) {
    if (!_impl->treeWidget) return;
    auto* root = _impl->treeWidget->topLevelItem(0);
    if (!root) return;
    if (index >= 0 && index < root->childCount()) {
        _impl->treeWidget->blockSignals(true);
        _impl->treeWidget->setCurrentItem(root->child(index));
        _impl->treeWidget->blockSignals(false);
    } else if (index < 0) {
        _impl->treeWidget->blockSignals(true);
        _impl->treeWidget->setCurrentItem(root);
        _impl->treeWidget->blockSignals(false);
    }
}

void GlbOptionsWidget::reset() {
    _impl->reset();
}

} // namespace grn
