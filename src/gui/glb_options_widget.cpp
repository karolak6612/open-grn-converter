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
        coordSwitch->setToolTip(owner.tr("Convert glTF standard Y-up coordinates to Granny Z-up coordinates"));
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

            if (model->animations.empty()) {
                auto* infoItem = new QTreeWidgetItem(rootItem);
                infoItem->setText(0, owner.tr("(No animation clips found in model)"));
                infoItem->setFlags(Qt::NoItemFlags);
            } else {
                for (const auto& anim : model->animations) {
                    auto* animItem = new QTreeWidgetItem(rootItem);
                    animItem->setIcon(0, makeThemedIcon(Icons16::Media_Play));
                    animItem->setText(0, QString("%1 (%2s) → %3_%1.grn")
                        .arg(QString::fromStdString(anim.name))
                        .arg(anim.duration, 0, 'f', 2)
                        .arg(fi.completeBaseName()));
                }
            }
        } else {
            rootItem->setText(0, fi.fileName());
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

void GlbOptionsWidget::setSplitAnimations(bool split) {
    _impl->splitAnimsSwitch->setChecked(split);
    _impl->animSectionWidget->setVisible(split);
    _impl->bottomStretch->setVisible(!split);
    emit optionsChanged();
}

void GlbOptionsWidget::reset() {
    _impl->reset();
}

} // namespace grn
