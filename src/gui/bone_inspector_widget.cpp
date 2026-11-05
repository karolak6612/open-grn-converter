#include "bone_inspector_widget.h"
#include "section_card.h"
#include "gui_utils.h"

#include <oclero/qlementine/icons/Icons16.hpp>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QTreeWidget>
#include <QSplitter>
#include <vector>
#include <algorithm>

namespace grn {

using Icons16 = oclero::qlementine::icons::Icons16;

namespace {

struct BoneSubPanel {
    QWidget* container{ nullptr };
    QLabel* headerBadge{ nullptr };
    QLineEdit* filterEdit{ nullptr };
    QTreeWidget* treeWidget{ nullptr };
    SectionCard* detailsCard{ nullptr };

    QLabel* nameLabel{ nullptr };
    QLabel* indexLabel{ nullptr };
    QLabel* parentLabel{ nullptr };
    QLabel* posLabel{ nullptr };
    QLabel* rotLabel{ nullptr };
    QLabel* scaleLabel{ nullptr };

    std::vector<GrnBone> cachedBones;
    bool isSourceSide{ true };

    void setupUI(QWidget* parent, bool isSource, std::function<void(int, bool)> onSelect) {
        isSourceSide = isSource;
        container = new QWidget(parent);

        auto* layout = new QVBoxLayout(container);
        layout->setContentsMargins(4, 4, 4, 4);
        layout->setSpacing(4);

        // Header Title / Badge
        headerBadge = new QLabel(isSource ? container->tr("Source Bones (0)") : container->tr("Target Bones (0)"), container);
        headerBadge->setStyleSheet(
            "QLabel {"
            "  background: rgba(0, 0, 0, 0.06);"
            "  color: #333333;"
            "  border-radius: 4px;"
            "  padding: 3px 8px;"
            "  font-size: 11px;"
            "  font-weight: bold;"
            "}"
        );
        layout->addWidget(headerBadge);

        // Search / Filter
        filterEdit = new QLineEdit(container);
        filterEdit->setPlaceholderText(container->tr("Filter bones..."));
        filterEdit->setClearButtonEnabled(true);
        filterEdit->setFixedHeight(24);
        QObject::connect(filterEdit, &QLineEdit::textChanged, container, [this](const QString& text) {
            filterTree(text);
        });
        layout->addWidget(filterEdit);

        // Hierarchy Tree
        treeWidget = new QTreeWidget(container);
        treeWidget->setHeaderHidden(true);
        treeWidget->setAlternatingRowColors(true);
        treeWidget->setAnimated(true);
        QObject::connect(treeWidget, &QTreeWidget::currentItemChanged, container, [this, onSelect](QTreeWidgetItem* current, QTreeWidgetItem*) {
            if (current) {
                int boneIdx = current->data(0, Qt::UserRole).toInt();
                updateDetails(boneIdx);
                if (onSelect) {
                    onSelect(boneIdx, isSourceSide);
                }
            } else {
                clearDetails();
            }
        });
        layout->addWidget(treeWidget, 1);

        // Details Card
        detailsCard = new SectionCard(container);
        auto* form = new QFormLayout(detailsCard);
        form->setContentsMargins(8, 6, 8, 6);
        form->setVerticalSpacing(3);
        form->setHorizontalSpacing(8);

        nameLabel = new QLabel("-", detailsCard);
        nameLabel->setStyleSheet("font-weight: bold;");
        form->addRow(container->tr("Name:"), nameLabel);

        indexLabel = new QLabel("-", detailsCard);
        form->addRow(container->tr("Index:"), indexLabel);

        parentLabel = new QLabel("-", detailsCard);
        form->addRow(container->tr("Parent:"), parentLabel);

        posLabel = new QLabel("-", detailsCard);
        posLabel->setStyleSheet("font-family: monospace; font-size: 10px;");
        form->addRow(container->tr("Pos:"), posLabel);

        rotLabel = new QLabel("-", detailsCard);
        rotLabel->setStyleSheet("font-family: monospace; font-size: 10px;");
        form->addRow(container->tr("Rot:"), rotLabel);

        scaleLabel = new QLabel("-", detailsCard);
        scaleLabel->setStyleSheet("font-family: monospace; font-size: 10px;");
        form->addRow(container->tr("Scale:"), scaleLabel);

        layout->addWidget(detailsCard);
    }

    void setModel(const GrnModel* model) {
        treeWidget->clear();
        clearDetails();
        filterEdit->clear();

        if (!model || model->bones.empty()) {
            cachedBones.clear();
            headerBadge->setText(isSourceSide ? container->tr("Source Bones (0)") : container->tr("Target Bones (0)"));
            return;
        }

        cachedBones = model->bones;
        size_t n = cachedBones.size();
        for (size_t i = 0; i < n; ++i) {
            if (cachedBones[i].index == 0 && i != 0) {
                cachedBones[i].index = static_cast<int32_t>(i);
            }
        }

        headerBadge->setText(QString("%1 (%2)")
            .arg(isSourceSide ? container->tr("Source Bones") : container->tr("Target Bones"))
            .arg(n));

        // Count direct children for each bone
        std::vector<int> childCount(n, 0);
        for (size_t i = 0; i < n; ++i) {
            int32_t p = cachedBones[i].parent_index;
            if (p >= 0 && static_cast<size_t>(p) < n && static_cast<size_t>(p) != i) {
                childCount[p]++;
            }
        }

        // Create tree items
        std::vector<QTreeWidgetItem*> items(n, nullptr);
        for (size_t i = 0; i < n; ++i) {
            const auto& b = cachedBones[i];
            auto* item = new QTreeWidgetItem();
            int c = childCount[i];
            int32_t boneId = (b.index != 0 || i == 0) ? b.index : static_cast<int32_t>(i);
            QString text = QString("[%1] %2 (%3 %4)")
                .arg(boneId)
                .arg(QString::fromStdString(b.name.empty() ? "(unnamed)" : b.name))
                .arg(c)
                .arg(c == 1 ? container->tr("child") : container->tr("children"));
            item->setText(0, text);
            item->setData(0, Qt::UserRole, static_cast<int>(i));
            items[i] = item;
        }

        // Build parent-child hierarchy
        for (size_t i = 0; i < n; ++i) {
            int32_t p = cachedBones[i].parent_index;
            if (p >= 0 && static_cast<size_t>(p) < n && static_cast<size_t>(p) != i) {
                items[p]->addChild(items[i]);
            } else {
                treeWidget->addTopLevelItem(items[i]);
            }
        }

        // Expand root nodes
        for (int i = 0; i < treeWidget->topLevelItemCount(); ++i) {
            treeWidget->topLevelItem(i)->setExpanded(true);
        }
    }

    void updateDetails(int boneIdx) {
        if (boneIdx < 0 || static_cast<size_t>(boneIdx) >= cachedBones.size()) {
            clearDetails();
            return;
        }
        const auto& b = cachedBones[boneIdx];
        nameLabel->setText(QString::fromStdString(b.name.empty() ? "(unnamed)" : b.name));
        int32_t boneId = (b.index != 0 || boneIdx == 0) ? b.index : static_cast<int32_t>(boneIdx);
        indexLabel->setText(QString::number(boneId));

        if (b.parent_index >= 0 && static_cast<size_t>(b.parent_index) < cachedBones.size()) {
            const auto& p = cachedBones[b.parent_index];
            parentLabel->setText(QString("[%1] %2").arg(b.parent_index).arg(QString::fromStdString(p.name)));
        } else {
            parentLabel->setText(container->tr("None (Root)"));
        }

        posLabel->setText(QString("%1, %2, %3")
            .arg(b.position.x, 0, 'f', 3)
            .arg(b.position.y, 0, 'f', 3)
            .arg(b.position.z, 0, 'f', 3));

        rotLabel->setText(QString("%1, %2, %3, %4")
            .arg(b.rotation.x, 0, 'f', 4)
            .arg(b.rotation.y, 0, 'f', 4)
            .arg(b.rotation.z, 0, 'f', 4)
            .arg(b.rotation.w, 0, 'f', 4));

        scaleLabel->setText(QString("%1, %2, %3")
            .arg(b.scale_3x3[0], 0, 'f', 3)
            .arg(b.scale_3x3[4], 0, 'f', 3)
            .arg(b.scale_3x3[8], 0, 'f', 3));
    }

    void clearDetails() {
        nameLabel->setText("-");
        indexLabel->setText("-");
        parentLabel->setText("-");
        posLabel->setText("-");
        rotLabel->setText("-");
        scaleLabel->setText("-");
    }

    void clear() {
        treeWidget->clear();
        clearDetails();
        filterEdit->clear();
        cachedBones.clear();
        headerBadge->setText(isSourceSide ? container->tr("Source Bones (0)") : container->tr("Target Bones (0)"));
    }

    bool filterItem(QTreeWidgetItem* item, const QString& filter) {
        bool matches = item->text(0).contains(filter, Qt::CaseInsensitive);
        bool anyChildMatches = false;
        for (int i = 0; i < item->childCount(); ++i) {
            if (filterItem(item->child(i), filter)) {
                anyChildMatches = true;
            }
        }
        bool visible = matches || anyChildMatches;
        item->setHidden(!visible);
        if (anyChildMatches && !filter.isEmpty()) {
            item->setExpanded(true);
        }
        return visible;
    }

    void filterTree(const QString& text) {
        QString trimmed = text.trimmed();
        for (int i = 0; i < treeWidget->topLevelItemCount(); ++i) {
            if (trimmed.isEmpty()) {
                showAllItems(treeWidget->topLevelItem(i));
            } else {
                filterItem(treeWidget->topLevelItem(i), trimmed);
            }
        }
    }

    void showAllItems(QTreeWidgetItem* item) {
        item->setHidden(false);
        for (int i = 0; i < item->childCount(); ++i) {
            showAllItems(item->child(i));
        }
    }
};

} // namespace

struct BoneInspectorWidget::Impl {
    BoneInspectorWidget& owner;
    QSplitter* splitter{ nullptr };
    BoneSubPanel sourcePanel;
    BoneSubPanel targetPanel;

    explicit Impl(BoneInspectorWidget& o) : owner(o) {
        setupUI();
    }

    void setupUI() {
        auto* layout = new QVBoxLayout(&owner);
        layout->setContentsMargins(4, 4, 4, 4);
        layout->setSpacing(4);

        splitter = new QSplitter(Qt::Horizontal, &owner);
        splitter->setHandleWidth(4);

        auto onSelect = [this](int boneIdx, bool isSource) {
            emit owner.boneSelected(boneIdx, isSource);
        };

        sourcePanel.setupUI(splitter, true, onSelect);
        targetPanel.setupUI(splitter, false, onSelect);

        splitter->addWidget(sourcePanel.container);
        splitter->addWidget(targetPanel.container);
        splitter->setSizes({ 250, 250 });

        layout->addWidget(splitter, 1);
    }
};

BoneInspectorWidget::BoneInspectorWidget(QWidget* parent)
    : QWidget(parent)
    , _impl(std::make_unique<Impl>(*this)) {}

BoneInspectorWidget::~BoneInspectorWidget() = default;

void BoneInspectorWidget::setSourceModel(const GrnModel* model) {
    _impl->sourcePanel.setModel(model);
}

void BoneInspectorWidget::setTargetModel(const GrnModel* model) {
    _impl->targetPanel.setModel(model);
}

void BoneInspectorWidget::clear() {
    _impl->sourcePanel.clear();
    _impl->targetPanel.clear();
}

} // namespace grn
