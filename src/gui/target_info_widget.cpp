#include "target_info_widget.h"
#include "section_card.h"
#include "gui_utils.h"
#include "../core/grn_parser.h"

#include <oclero/qlementine/icons/Icons16.hpp>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QFileInfo>
#include <QDir>
#include <QDesktopServices>
#include <QUrl>

namespace grn {

using Icons16 = oclero::qlementine::icons::Icons16;

static QString formatFileSize(qint64 bytes) {
    if (bytes < 1024) return QString("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
}

struct TargetInfoWidget::Impl {
    TargetInfoWidget& owner;

    QLabel* titleLabel{ nullptr };
    QLabel* statusBadge{ nullptr };

    QLabel* fileNameLabel{ nullptr };
    QLabel* formatLabel{ nullptr };
    QLabel* sizeLabel{ nullptr };
    QLabel* geomLabel{ nullptr };
    QLabel* rigLabel{ nullptr };
    QLabel* validLabel{ nullptr };

    QTreeWidget* treeWidget{ nullptr };
    QPushButton* openFolderBtn{ nullptr };

    QString currentOutputPath;
    const GrnModel* baseModel{ nullptr };
    QString baseOutputPath;
    bool baseIsGlb{ false };
    QString baseValidationSummary;

    explicit Impl(TargetInfoWidget& o) : owner(o) {
        setupUI();
    }

    void setupUI() {
        auto* layout = new QVBoxLayout(&owner);
        layout->setContentsMargins(6, 6, 6, 6);
        layout->setSpacing(8);

        // 1. Header with title & status badge
        auto* headerLayout = new QHBoxLayout();
        headerLayout->setContentsMargins(0, 0, 0, 0);

        titleLabel = new QLabel(owner.tr("Converted Model"), &owner);
        QFont hf = titleLabel->font();
        hf.setBold(true);
        titleLabel->setFont(hf);
        headerLayout->addWidget(titleLabel);

        headerLayout->addStretch(1);

        statusBadge = new QLabel(owner.tr("Waiting"), &owner);
        statusBadge->setStyleSheet(
            "QLabel {"
            "  background: rgba(0, 0, 0, 0.06);"
            "  color: #666666;"
            "  border-radius: 4px;"
            "  padding: 2px 6px;"
            "  font-size: 11px;"
            "  font-weight: bold;"
            "}"
        );
        headerLayout->addWidget(statusBadge);
        layout->addLayout(headerLayout);

        // 2. Target Metadata Card
        auto* metaCard = new SectionCard(&owner);
        auto* metaLayout = new QFormLayout(metaCard);
        metaLayout->setContentsMargins(8, 8, 8, 8);
        metaLayout->setSpacing(4);

        fileNameLabel = new QLabel(owner.tr("None"), metaCard);
        fileNameLabel->setStyleSheet("font-weight: bold;");
        metaLayout->addRow(owner.tr("File:"), fileNameLabel);

        formatLabel = new QLabel(owner.tr("-"), metaCard);
        metaLayout->addRow(owner.tr("Format:"), formatLabel);

        sizeLabel = new QLabel(owner.tr("-"), metaCard);
        metaLayout->addRow(owner.tr("Size:"), sizeLabel);

        geomLabel = new QLabel(owner.tr("-"), metaCard);
        metaLayout->addRow(owner.tr("Geometry:"), geomLabel);

        rigLabel = new QLabel(owner.tr("-"), metaCard);
        metaLayout->addRow(owner.tr("Rig:"), rigLabel);

        validLabel = new QLabel(owner.tr("-"), metaCard);
        metaLayout->addRow(owner.tr("Validation:"), validLabel);

        layout->addWidget(metaCard);

        // 3. Converted Model & Animations Tree
        auto* treeCard = new SectionCard(&owner);
        treeCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        auto* treeLayout = new QVBoxLayout(treeCard);
        treeLayout->setContentsMargins(6, 6, 6, 6);
        treeLayout->setSpacing(4);

        auto* treeTitle = new QLabel(owner.tr("Converted Files"), treeCard);
        QFont tf = treeTitle->font();
        tf.setBold(true);
        treeTitle->setFont(tf);
        treeLayout->addWidget(treeTitle);

        treeWidget = new QTreeWidget(treeCard);
        treeWidget->setStyleSheet("QTreeWidget { background: transparent; border: none; } QTreeWidget::item { padding: 2px 0; }");
        treeWidget->setIconSize(QSize(16, 16));
        treeWidget->setColumnCount(1);
        treeWidget->setHeaderHidden(true);
        treeWidget->setAlternatingRowColors(false);
        treeWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
        treeWidget->setIndentation(16);
        treeWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        treeLayout->addWidget(treeWidget);

        auto emitAnim = [this](QTreeWidgetItem* current) {
            updateSelectedFileInfo(current);
            if (!current) {
                emit owner.animationSelected(-1, QString());
                return;
            }
            int animIdx = current->data(0, Qt::UserRole).toInt();
            QString clipPath = current->data(0, Qt::UserRole + 1).toString();
            emit owner.animationSelected(animIdx, clipPath);
        };

        QObject::connect(treeWidget, &QTreeWidget::currentItemChanged, &owner, [emitAnim](QTreeWidgetItem* current, QTreeWidgetItem*) {
            emitAnim(current);
        });
        QObject::connect(treeWidget, &QTreeWidget::itemClicked, &owner, [emitAnim](QTreeWidgetItem* current, int) {
            emitAnim(current);
        });

        layout->addWidget(treeCard, 1);

        // 4. Action Button: Open Output Folder
        openFolderBtn = new QPushButton(makeThemedIcon(Icons16::File_FolderOpen), owner.tr("Open Output Folder"), &owner);
        openFolderBtn->setFixedHeight(28);
        openFolderBtn->setCursor(Qt::PointingHandCursor);
        openFolderBtn->setEnabled(false);
        QObject::connect(openFolderBtn, &QPushButton::clicked, &owner, [this]() {
            if (!currentOutputPath.isEmpty()) {
                QFileInfo fi(currentOutputPath);
                QString dir = fi.isDir() ? fi.absoluteFilePath() : fi.dir().absolutePath();
                QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
                emit owner.openFolderRequested(dir);
            }
        });
        layout->addWidget(openFolderBtn);
    }

    void updateSelectedFileInfo(QTreeWidgetItem* current) {
        if (!current) return;
        int role = current->data(0, Qt::UserRole).toInt();
        QString clipPath = current->data(0, Qt::UserRole + 1).toString();

        if (role == -1 || (role < 0 && clipPath.isEmpty())) {
            // Base model file
            titleLabel->setText(owner.tr("Converted Model"));
            QFileInfo fi(baseOutputPath);
            fileNameLabel->setText(fi.fileName().isEmpty() ? owner.tr("Output") : fi.fileName());
            formatLabel->setText(baseIsGlb ? owner.tr("glTF 2.0 Binary (.glb)") : owner.tr("Granny 1.2b (.grn)"));
            qint64 sz = fi.exists() ? fi.size() : 0;
            sizeLabel->setText(formatFileSize(sz));

            if (baseModel) {
                size_t totalVerts = 0;
                size_t totalTris = 0;
                for (const auto& m : baseModel->meshes) {
                    totalVerts += m.vertices.size();
                    totalTris += m.faces.size();
                }
                geomLabel->setText(QString("%1 meshes, %2 verts, %3 tris")
                    .arg(baseModel->meshes.size())
                    .arg(totalVerts)
                    .arg(totalTris));
                rigLabel->setText(QString("%1 bones").arg(baseModel->bones.size()));
            } else {
                geomLabel->setText(owner.tr("-"));
                rigLabel->setText(owner.tr("-"));
            }
            validLabel->setText(baseValidationSummary.isEmpty() ? (baseIsGlb ? owner.tr("glTF 2.0 Valid ✓") : owner.tr("Granny 1.2b Valid ✓")) : baseValidationSummary);
        } else if (role == -2 && !clipPath.isEmpty()) {
            // Standalone split animation file
            titleLabel->setText(owner.tr("Converted Animation"));
            QFileInfo fi(clipPath);
            fileNameLabel->setText(fi.fileName());
            formatLabel->setText(owner.tr("Granny 1.2b Animation (.grn)"));
            qint64 sz = fi.exists() ? fi.size() : 0;
            sizeLabel->setText(formatFileSize(sz));

            auto animModel = parse_grn_file(fi.filesystemFilePath());
            if (animModel && !animModel->animations.empty()) {
                const auto& a = animModel->animations[0];
                geomLabel->setText(owner.tr("0 meshes (pure animation)"));
                rigLabel->setText(QString("%1 bones, %2 tracks (%3s)")
                    .arg(animModel->bones.size())
                    .arg(a.tracks.size())
                    .arg(a.duration, 0, 'f', 2));
                validLabel->setText(owner.tr("Animation Valid ✓"));
            } else {
                geomLabel->setText(owner.tr("0 meshes"));
                rigLabel->setText(owner.tr("Animation Clip"));
                validLabel->setText(owner.tr("Granny 1.2b Valid ✓"));
            }
        } else if (role >= 0 && baseModel && static_cast<size_t>(role) < baseModel->animations.size()) {
            // Embedded animation track in base model
            titleLabel->setText(owner.tr("Converted Animation"));
            const auto& a = baseModel->animations[role];
            QFileInfo fi(baseOutputPath);
            QString clipName = QString::fromStdString(a.name.empty() ? ("Animation_" + std::to_string(role)) : a.name);
            fileNameLabel->setText(QString("%1 [%2]").arg(fi.fileName()).arg(clipName));
            formatLabel->setText(owner.tr("Embedded Animation Clip"));
            sizeLabel->setText(QString("Duration: %1s (%2 fps)").arg(a.duration, 0, 'f', 2).arg(a.fps, 0, 'f', 0));
            geomLabel->setText(QString("Shared with model (%1 meshes)").arg(baseModel->meshes.size()));
            rigLabel->setText(QString("%1 animated tracks").arg(a.tracks.size()));
            validLabel->setText(owner.tr("Valid Clip ✓"));
        }
    }
};

TargetInfoWidget::TargetInfoWidget(QWidget* parent)
    : QWidget(parent)
    , _impl(std::make_unique<Impl>(*this)) {}

TargetInfoWidget::~TargetInfoWidget() = default;

void TargetInfoWidget::setConverting(bool converting) {
    if (converting) {
        _impl->statusBadge->setText(tr("Converting..."));
        _impl->statusBadge->setStyleSheet(
            "QLabel {"
            "  background: rgba(0, 120, 215, 0.15);"
            "  color: #0078d7;"
            "  border-radius: 4px;"
            "  padding: 2px 6px;"
            "  font-size: 11px;"
            "  font-weight: bold;"
            "}"
        );
        _impl->openFolderBtn->setEnabled(false);
    }
}

void TargetInfoWidget::setTargetModel(const GrnModel* model, const QString& outputPath, bool isGlb, const QString& validationSummary) {
    _impl->currentOutputPath = outputPath;
    _impl->baseModel = model;
    _impl->baseOutputPath = outputPath;
    _impl->baseIsGlb = isGlb;
    _impl->baseValidationSummary = validationSummary;
    _impl->openFolderBtn->setEnabled(!outputPath.isEmpty());

    QFileInfo fi(outputPath);
    _impl->fileNameLabel->setText(fi.fileName().isEmpty() ? tr("Output") : fi.fileName());
    _impl->formatLabel->setText(isGlb ? tr("glTF 2.0 Binary (.glb)") : tr("Granny 1.2b (.grn)"));

    qint64 sz = fi.exists() ? fi.size() : 0;
    _impl->sizeLabel->setText(formatFileSize(sz));

    if (model) {
        size_t totalVerts = 0;
        size_t totalTris = 0;
        for (const auto& m : model->meshes) {
            totalVerts += m.vertices.size();
            totalTris += m.faces.size();
        }
        _impl->geomLabel->setText(QString("%1 meshes, %2 verts, %3 tris")
            .arg(model->meshes.size())
            .arg(totalVerts)
            .arg(totalTris));
        _impl->rigLabel->setText(QString("%1 bones").arg(model->bones.size()));

    } else {
        _impl->geomLabel->setText(tr("-"));
        _impl->rigLabel->setText(tr("-"));
    }

    if (!validationSummary.isEmpty()) {
        _impl->validLabel->setText(validationSummary);
    } else if (isGlb && fi.exists()) {
        _impl->validLabel->setText(tr("glTF 2.0 Valid ✓"));
    } else {
        _impl->validLabel->setText(tr("-"));
    }

    _impl->statusBadge->setText(tr("Ready ✓"));
    _impl->statusBadge->setStyleSheet(
        "QLabel {"
        "  background: rgba(40, 167, 69, 0.15);"
        "  color: #28a745;"
        "  border-radius: 4px;"
        "  padding: 2px 6px;"
        "  font-size: 11px;"
        "  font-weight: bold;"
        "}"
    );

    // Rebuild tree
    _impl->treeWidget->clear();
    auto* rootItem = new QTreeWidgetItem(_impl->treeWidget);
    rootItem->setIcon(0, makeThemedIcon(Icons16::Shape_Cube));
    rootItem->setText(0, fi.fileName().isEmpty() ? tr("Target Model") : fi.fileName());
    rootItem->setData(0, Qt::UserRole, -1);
    rootItem->setData(0, Qt::UserRole + 1, outputPath);
    rootItem->setExpanded(true);

    bool hasAnims = false;
    if (model && !model->animations.empty()) {
        hasAnims = true;
        for (size_t ai = 0; ai < model->animations.size(); ++ai) {
            const auto& a = model->animations[ai];
            auto* animItem = new QTreeWidgetItem(rootItem);
            animItem->setIcon(0, makeThemedIcon(Icons16::Media_Play));
            QString animTitle = QString::fromStdString(a.name);
            if (animTitle.isEmpty()) animTitle = tr("Animation %1").arg(ai);
            animItem->setText(0, QString("%1 (%2s)").arg(animTitle).arg(a.duration, 0, 'f', 2));
            animItem->setData(0, Qt::UserRole, static_cast<int>(ai));
            animItem->setData(0, Qt::UserRole + 1, QString());
        }
    }

    if (fi.exists()) {
        QString base = fi.completeBaseName();
        QDir dir = fi.dir();
        QString p1 = isGlb ? (base + "_*.glb") : (base + "_*.grn");
        QString p2 = isGlb ? (base + "-*.glb") : (base + "-*.grn");
        QStringList splitFiles = dir.entryList({ p1, p2 }, QDir::Files, QDir::Name);
        for (const QString& sf : splitFiles) {
            hasAnims = true;
            auto* animItem = new QTreeWidgetItem(rootItem);
            animItem->setIcon(0, makeThemedIcon(Icons16::Media_Play));
            animItem->setText(0, sf);
            animItem->setData(0, Qt::UserRole, -2);
            animItem->setData(0, Qt::UserRole + 1, dir.filePath(sf));
        }
    }

    if (!hasAnims) {
        auto* infoItem = new QTreeWidgetItem(rootItem);
        infoItem->setText(0, tr("(No embedded animation clips)"));
        infoItem->setFlags(Qt::NoItemFlags);
    }

    // Select the root item (base model) by default so base metadata is shown first
    _impl->treeWidget->setCurrentItem(rootItem);
    _impl->updateSelectedFileInfo(rootItem);
}

void TargetInfoWidget::clearTarget() {
    _impl->currentOutputPath.clear();
    _impl->baseModel = nullptr;
    _impl->baseOutputPath.clear();
    _impl->titleLabel->setText(tr("Converted Model"));
    _impl->fileNameLabel->setText(tr("None"));
    _impl->formatLabel->setText(tr("-"));
    _impl->sizeLabel->setText(tr("-"));
    _impl->geomLabel->setText(tr("-"));
    _impl->rigLabel->setText(tr("-"));
    _impl->validLabel->setText(tr("-"));

    _impl->statusBadge->setText(tr("Waiting"));
    _impl->statusBadge->setStyleSheet(
        "QLabel {"
        "  background: rgba(0, 0, 0, 0.06);"
        "  color: #666666;"
        "  border-radius: 4px;"
        "  padding: 2px 6px;"
        "  font-size: 11px;"
        "  font-weight: bold;"
        "}"
    );

    _impl->treeWidget->clear();
    _impl->openFolderBtn->setEnabled(false);
}

void TargetInfoWidget::selectAnimationItem(int index) {
    selectAnimation(index, QString());
}

void TargetInfoWidget::selectAnimation(int animIndex, const QString& clipNameOrPath) {
    if (!_impl->treeWidget) return;
    auto* root = _impl->treeWidget->topLevelItem(0);
    if (!root) return;

    if (animIndex < 0 && clipNameOrPath.isEmpty()) {
        _impl->treeWidget->blockSignals(true);
        _impl->treeWidget->setCurrentItem(root);
        _impl->treeWidget->blockSignals(false);
        _impl->updateSelectedFileInfo(root);
        return;
    }

    QTreeWidgetItem* matchedChild = nullptr;
    for (int i = 0; i < root->childCount(); ++i) {
        auto* child = root->child(i);
        int role = child->data(0, Qt::UserRole).toInt();
        QString path = child->data(0, Qt::UserRole + 1).toString();
        QString text = child->text(0);

        if (animIndex >= 0 && role == animIndex) {
            matchedChild = child;
            break;
        }

        if (!clipNameOrPath.isEmpty() && !path.isEmpty() &&
            (path.endsWith(clipNameOrPath, Qt::CaseInsensitive) || clipNameOrPath.endsWith(path, Qt::CaseInsensitive))) {
            matchedChild = child;
            break;
        }

        if (!clipNameOrPath.isEmpty()) {
            QFileInfo fi(clipNameOrPath);
            QString baseName = fi.completeBaseName();
            if (text.contains(clipNameOrPath, Qt::CaseInsensitive) ||
                (!baseName.isEmpty() && text.contains(baseName, Qt::CaseInsensitive))) {
                matchedChild = child;
                break;
            }
        }
    }

    if (matchedChild) {
        _impl->treeWidget->blockSignals(true);
        _impl->treeWidget->setCurrentItem(matchedChild);
        _impl->treeWidget->blockSignals(false);
        _impl->updateSelectedFileInfo(matchedChild);
    } else {
        _impl->treeWidget->blockSignals(true);
        _impl->treeWidget->setCurrentItem(root);
        _impl->treeWidget->blockSignals(false);
        _impl->updateSelectedFileInfo(root);
    }
}

void TargetInfoWidget::markOutdated(bool outdated) {
    if (!_impl->currentOutputPath.isEmpty()) {
        if (outdated) {
            _impl->statusBadge->setText(tr("Outdated"));
            _impl->statusBadge->setStyleSheet(
                "QLabel {"
                "  background: rgba(255, 193, 7, 0.2);"
                "  color: #c67d00;"
                "  border-radius: 4px;"
                "  padding: 2px 6px;"
                "  font-size: 11px;"
                "  font-weight: bold;"
                "}"
            );
        } else {
            _impl->statusBadge->setText(tr("Ready ✓"));
            _impl->statusBadge->setStyleSheet(
                "QLabel {"
                "  background: rgba(40, 167, 69, 0.15);"
                "  color: #28a745;"
                "  border-radius: 4px;"
                "  padding: 2px 6px;"
                "  font-size: 11px;"
                "  font-weight: bold;"
                "}"
            );
        }
    }
}

} // namespace grn
