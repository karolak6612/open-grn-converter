#include "grn_options_widget.h"
#include "../core/grn_parser.h"

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
#include <QPushButton>
#include <QLabel>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include "section_card.h"
#include "gui_utils.h"

namespace grn {

using Icons16 = oclero::qlementine::icons::Icons16;

struct GrnOptionsWidget::Impl {
    GrnOptionsWidget& owner;

    oclero::qlementine::Switch* coordSwitch{ nullptr };
    oclero::qlementine::Switch* embedTexSwitch{ nullptr };
    QComboBox* looseFmtCombo{ nullptr };
    oclero::qlementine::Switch* vtexDecompressSwitch{ nullptr };
    QDoubleSpinBox* scaleSpin{ nullptr };
    oclero::qlementine::Switch* embedAnimSwitch{ nullptr };

    QWidget* animSectionWidget{ nullptr };
    QPushButton* addBtn{ nullptr };
    QPushButton* clearBtn{ nullptr };
    QTreeWidget* treeWidget{ nullptr };
    QWidget* bottomStretch{ nullptr };

    QString currentModelPath;
    std::vector<std::filesystem::path> externalAnims;

    explicit Impl(GrnOptionsWidget& o) : owner(o) {
        setupUI();
    }

    void setupUI() {
        auto* layout = new QVBoxLayout(&owner);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);

        // Section 1: Extraction Options
        auto* optLabel = new QLabel(owner.tr("Extraction Options"), &owner);
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
        coordSwitch->setToolTip(owner.tr("Convert native Granny Z-up coordinates to glTF standard Y-up (recommended for Blender, Godot, and 3D viewers)"));
        QObject::connect(coordSwitch, &oclero::qlementine::Switch::clicked, &owner, [this]() {
            emit owner.optionsChanged();
        });
        optLayout->addRow(owner.tr("Convert Z-up → Y-up:"), coordSwitch);

        embedTexSwitch = new oclero::qlementine::Switch(optCard);
        embedTexSwitch->setChecked(true);
        embedTexSwitch->setToolTip(owner.tr("Embed all diffuse textures inside the binary GLB buffer"));
        QObject::connect(embedTexSwitch, &oclero::qlementine::Switch::clicked, &owner, [this]() {
            looseFmtCombo->setEnabled(!embedTexSwitch->isChecked());
            emit owner.optionsChanged();
        });
        optLayout->addRow(owner.tr("Embed Textures:"), embedTexSwitch);

        looseFmtCombo = new QComboBox(optCard);
        looseFmtCombo->addItems({"PNG", "TGA (32-bit)"});
        looseFmtCombo->setEnabled(false);
        looseFmtCombo->setFixedHeight(24);
        QObject::connect(looseFmtCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &owner, [this](int) {
            emit owner.optionsChanged();
        });
        optLayout->addRow(owner.tr("Loose Format:"), looseFmtCombo);

        vtexDecompressSwitch = new oclero::qlementine::Switch(optCard);
        vtexDecompressSwitch->setChecked(true);
        vtexDecompressSwitch->setToolTip(owner.tr("Decompress Granny 1.2b proprietary VTex video textures"));
        QObject::connect(vtexDecompressSwitch, &oclero::qlementine::Switch::clicked, &owner, [this]() {
            emit owner.optionsChanged();
        });
        optLayout->addRow(owner.tr("VTex Decompressor:"), vtexDecompressSwitch);

        scaleSpin = new QDoubleSpinBox(optCard);
        scaleSpin->setRange(0.0001, 1000.0);
        scaleSpin->setDecimals(4);
        scaleSpin->setValue(1.0000);
        scaleSpin->setSingleStep(0.1);
        scaleSpin->setSuffix("x");
        scaleSpin->setFixedHeight(24);
        QObject::connect(scaleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), &owner, [this](double) {
            emit owner.optionsChanged();
        });
        optLayout->addRow(owner.tr("Scale Multiplier:"), scaleSpin);

        embedAnimSwitch = new oclero::qlementine::Switch(optCard);
        embedAnimSwitch->setChecked(true);
        embedAnimSwitch->setToolTip(owner.tr("Embed external animations into the generated GLB model"));
        QObject::connect(embedAnimSwitch, &oclero::qlementine::Switch::clicked, &owner, [this]() {
            const bool embed = embedAnimSwitch->isChecked();
            animSectionWidget->setVisible(embed);
            bottomStretch->setVisible(!embed);
            emit owner.optionsChanged();
        });
        optLayout->addRow(owner.tr("Embed Animations:"), embedAnimSwitch);

        layout->addWidget(optCard);

        // Section 2: Model & Animations
        animSectionWidget = new QWidget(&owner);
        animSectionWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        auto* animLayout = new QVBoxLayout(animSectionWidget);
        animLayout->setContentsMargins(0, 4, 0, 0);
        animLayout->setSpacing(4);

        auto* animHeader = new QHBoxLayout();
        animHeader->setContentsMargins(0, 0, 0, 0);
        animHeader->setSpacing(4);

        auto* animTitle = new QLabel(owner.tr("Model & Animations"), animSectionWidget);
        animTitle->setFont(hf);
        animHeader->addWidget(animTitle);
        animHeader->addStretch();

        addBtn = new QPushButton(makeThemedIcon(Icons16::Action_Plus), owner.tr("Add..."), animSectionWidget);
        addBtn->setFixedHeight(24);
        addBtn->setCursor(Qt::PointingHandCursor);
        addBtn->setEnabled(false);
        addBtn->setStyleSheet(
            "QPushButton {"
            "  background-color: #0078d7;"
            "  color: #ffffff;"
            "  border: 1px solid #005a9e;"
            "  border-radius: 4px;"
            "  padding: 2px 10px;"
            "  font-weight: 600;"
            "}"
            "QPushButton:hover:!disabled {"
            "  background-color: #1a88e1;"
            "  border-color: #0078d7;"
            "}"
            "QPushButton:pressed:!disabled {"
            "  background-color: #005a9e;"
            "}"
            "QPushButton:disabled {"
            "  background-color: rgba(0, 120, 215, 0.25);"
            "  color: rgba(255, 255, 255, 0.6);"
            "  border-color: transparent;"
            "}"
        );
        QObject::connect(addBtn, &QPushButton::clicked, &owner, [this]() { onAdd(); });
        animHeader->addWidget(addBtn);

        clearBtn = new QPushButton(makeThemedIcon(Icons16::Action_Trash), owner.tr("Clear"), animSectionWidget);
        clearBtn->setFixedHeight(24);
        clearBtn->setCursor(Qt::PointingHandCursor);
        clearBtn->setEnabled(false);
        clearBtn->setStyleSheet(
            "QPushButton {"
            "  background-color: #0078d7;"
            "  color: #ffffff;"
            "  border: 1px solid #005a9e;"
            "  border-radius: 4px;"
            "  padding: 2px 10px;"
            "  font-weight: 600;"
            "}"
            "QPushButton:hover:!disabled {"
            "  background-color: #1a88e1;"
            "  border-color: #0078d7;"
            "}"
            "QPushButton:pressed:!disabled {"
            "  background-color: #005a9e;"
            "}"
            "QPushButton:disabled {"
            "  background-color: rgba(0, 120, 215, 0.25);"
            "  color: rgba(255, 255, 255, 0.6);"
            "  border-color: transparent;"
            "}"
        );
        QObject::connect(clearBtn, &QPushButton::clicked, &owner, [this]() { onClear(); });
        animHeader->addWidget(clearBtn);

        animLayout->addLayout(animHeader);

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
                emit owner.animationSelected(QString(), -1);
                return;
            }
            QString animPath = current->data(0, Qt::UserRole).toString();
            int intIdx = current->data(0, Qt::UserRole + 1).isValid() ? current->data(0, Qt::UserRole + 1).toInt() : -1;
            emit owner.animationSelected(animPath, intIdx);
        });

        animLayout->addWidget(animCard, 1);

        layout->addWidget(animSectionWidget, 1);

        bottomStretch = new QWidget(&owner);
        bottomStretch->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
        bottomStretch->setVisible(false);
        layout->addWidget(bottomStretch, 1);

        rebuildTree();
    }

    void onAdd() {
        QStringList files = QFileDialog::getOpenFileNames(
            &owner,
            owner.tr("Select External Animation Files (.grn)"),
            QString(),
            owner.tr("Granny 1.2b Animations (*.grn);;All Files (*.*)")
        );
        if (files.isEmpty()) return;

        for (const auto& f : files) {
            std::filesystem::path p(f.toStdString());
            bool exists = false;
            for (const auto& existing : externalAnims) {
                if (existing == p) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                externalAnims.push_back(p);
            }
        }
        rebuildTree();
        emit owner.animFilesChanged();
    }

    void onClear() {
        if (!externalAnims.empty()) {
            externalAnims.clear();
            rebuildTree();
            emit owner.animFilesChanged();
        }
    }

    void rebuildTree() {
        treeWidget->clear();

        bool hasGrnModel = false;
        if (!currentModelPath.isEmpty()) {
            QFileInfo fi(currentModelPath);
            if (fi.exists() && fi.isFile() && fi.suffix().compare("grn", Qt::CaseInsensitive) == 0) {
                hasGrnModel = true;
            }
        }
        addBtn->setEnabled(hasGrnModel);
        clearBtn->setEnabled(hasGrnModel && !externalAnims.empty());

        if (!hasGrnModel) {
            addBtn->setToolTip(owner.tr("Specify a valid .grn model first to add external animations"));
        } else {
            addBtn->setToolTip(owner.tr("Add external Granny animation clips (.grn)"));
        }

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

        auto model = parse_grn_file(fi.filesystemFilePath());
        auto* rootItem = new QTreeWidgetItem(treeWidget);
        rootItem->setIcon(0, makeThemedIcon(Icons16::Misc_Blocks));

        if (model) {
            rootItem->setText(0, QString("%1 (%2 meshes, %3 bones)")
                .arg(fi.fileName())
                .arg(model->meshes.size())
                .arg(model->bones.size()));

            for (size_t ai = 0; ai < model->animations.size(); ++ai) {
                const auto& a = model->animations[ai];
                if (a.tracks.empty()) continue;
                auto* animItem = new QTreeWidgetItem(rootItem);
                animItem->setIcon(0, makeThemedIcon(Icons16::Media_Play));
                QString name = QString::fromStdString(a.name.empty() ? ("Animation_" + std::to_string(ai)) : a.name);
                animItem->setText(0, QString("%1 (%2s)").arg(name).arg(a.duration, 0, 'f', 2));
                animItem->setData(0, Qt::UserRole, QString());
                animItem->setData(0, Qt::UserRole + 1, static_cast<int>(ai));
            }
        } else {
            rootItem->setText(0, fi.fileName());
        }
        rootItem->setData(0, Qt::UserRole, QString());
        rootItem->setData(0, Qt::UserRole + 1, -1);
        rootItem->setExpanded(true);

        if (externalAnims.empty()) {
            auto* infoItem = new QTreeWidgetItem(rootItem);
            infoItem->setText(0, owner.tr("(No external animation clips queued)"));
            infoItem->setFlags(Qt::NoItemFlags);
        } else {
            for (const auto& animPath : externalAnims) {
                auto* animItem = new QTreeWidgetItem(rootItem);
                animItem->setIcon(0, makeThemedIcon(Icons16::Media_Play));
                animItem->setText(0, QString::fromStdString(animPath.filename().string()));
                animItem->setToolTip(0, QString::fromStdString(animPath.string()));
                animItem->setData(0, Qt::UserRole, QString::fromStdString(animPath.string()));
                animItem->setData(0, Qt::UserRole + 1, -1);
            }
        }
    }

    void reset() {
        coordSwitch->setChecked(true);
        embedTexSwitch->setChecked(true);
        looseFmtCombo->setCurrentIndex(0);
        looseFmtCombo->setEnabled(false);
        vtexDecompressSwitch->setChecked(true);
        scaleSpin->setValue(1.0);
        embedAnimSwitch->setChecked(true);
        animSectionWidget->setVisible(true);
        bottomStretch->setVisible(false);
        externalAnims.clear();
        rebuildTree();
        emit owner.optionsChanged();
        emit owner.animFilesChanged();
    }
};

GrnOptionsWidget::GrnOptionsWidget(QWidget* parent)
    : QWidget(parent)
    , _impl(std::make_unique<Impl>(*this)) {}

GrnOptionsWidget::~GrnOptionsWidget() = default;

void GrnOptionsWidget::setModel(const QString& modelPath) {
    _impl->currentModelPath = modelPath;
    _impl->rebuildTree();
}

bool GrnOptionsWidget::convertCoordinates() const {
    return _impl->coordSwitch->isChecked();
}

void GrnOptionsWidget::setConvertCoordinates(bool convert) {
    if (_impl->coordSwitch && _impl->coordSwitch->isChecked() != convert) {
        _impl->coordSwitch->setChecked(convert);
        emit optionsChanged();
    }
}

bool GrnOptionsWidget::embedTextures() const {
    return _impl->embedTexSwitch->isChecked();
}

std::string GrnOptionsWidget::looseTextureFormat() const {
    return (_impl->looseFmtCombo->currentIndex() == 0) ? "png" : "tga";
}

bool GrnOptionsWidget::decompressVTex() const {
    return _impl->vtexDecompressSwitch->isChecked();
}

float GrnOptionsWidget::scaleMultiplier() const {
    return static_cast<float>(_impl->scaleSpin->value());
}

bool GrnOptionsWidget::embedAnimations() const {
    return _impl->embedAnimSwitch->isChecked();
}

std::vector<std::filesystem::path> GrnOptionsWidget::externalAnimFiles() const {
    return _impl->externalAnims;
}

void GrnOptionsWidget::addExternalAnimFile(const QString& path) {
    if (path.isEmpty()) return;
    _impl->externalAnims.emplace_back(path.toStdString());
    _impl->rebuildTree();
    emit animFilesChanged();
}

void GrnOptionsWidget::clearExternalAnims() {
    _impl->onClear();
}

void GrnOptionsWidget::selectAnimationItem(int index) {
    if (!_impl->treeWidget) return;
    auto* root = _impl->treeWidget->topLevelItem(0);
    if (!root) return;
    if (index >= 0 && index < root->childCount()) {
        _impl->treeWidget->setCurrentItem(root->child(index));
    }
}

void GrnOptionsWidget::reemitCurrentAnimation() {
    if (!_impl->treeWidget) return;
    auto* cur = _impl->treeWidget->currentItem();
    if (!cur) {
        auto* root = _impl->treeWidget->topLevelItem(0);
        if (root && root->childCount() > 0) {
            cur = root->child(0);
            _impl->treeWidget->setCurrentItem(cur);
            return;
        }
    }
    if (cur) {
        QString animPath = cur->data(0, Qt::UserRole).toString();
        int intIdx = cur->data(0, Qt::UserRole + 1).isValid() ? cur->data(0, Qt::UserRole + 1).toInt() : -1;
        emit animationSelected(animPath, intIdx);
    }
}

void GrnOptionsWidget::setEmbedTextures(bool embed) {
    _impl->embedTexSwitch->setChecked(embed);
    _impl->looseFmtCombo->setEnabled(!embed);
    emit optionsChanged();
}

void GrnOptionsWidget::setEmbedAnimations(bool enabled) {
    _impl->embedAnimSwitch->setChecked(enabled);
    _impl->animSectionWidget->setVisible(enabled);
    _impl->bottomStretch->setVisible(!enabled);
    emit optionsChanged();
}

void GrnOptionsWidget::reset() {
    _impl->reset();
}

} // namespace grn
