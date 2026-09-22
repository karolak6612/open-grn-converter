#include "source_info_widget.h"
#include "section_card.h"
#include "gui_utils.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QFileInfo>

namespace grn {

static QString formatFileSize(qint64 bytes) {
    if (bytes < 1024) return QString("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
}

struct SourceInfoWidget::Impl {
    SourceInfoWidget& owner;

    QLabel* titleLabel{ nullptr };
    QLabel* statusBadge{ nullptr };

    QLabel* fileNameLabel{ nullptr };
    QLabel* formatLabel{ nullptr };
    QLabel* sizeLabel{ nullptr };
    QLabel* geomLabel{ nullptr };
    QLabel* rigLabel{ nullptr };
    QLabel* limitLabel{ nullptr };

    bool is16BitExceeded{ false };

    explicit Impl(SourceInfoWidget& o) : owner(o) {
        setupUI();
    }

    void setupUI() {
        auto* layout = new QVBoxLayout(&owner);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);

        // Header
        auto* headerLayout = new QHBoxLayout();
        headerLayout->setContentsMargins(0, 0, 0, 0);

        titleLabel = new QLabel(owner.tr("Source Model Info"), &owner);
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

        // Card
        auto* metaCard = new SectionCard(&owner);
        auto* metaLayout = new QFormLayout(metaCard);
        metaLayout->setContentsMargins(8, 6, 8, 6);
        metaLayout->setVerticalSpacing(3);
        metaLayout->setHorizontalSpacing(6);

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

        limitLabel = new QLabel(owner.tr("-"), metaCard);
        metaLayout->addRow(owner.tr("16-Bit Limit:"), limitLabel);

        layout->addWidget(metaCard);
    }
};

SourceInfoWidget::SourceInfoWidget(QWidget* parent)
    : QWidget(parent)
    , _impl(std::make_unique<Impl>(*this)) {}

SourceInfoWidget::~SourceInfoWidget() = default;

void SourceInfoWidget::setSourceModel(const GrnModel* model, const QString& inputPath, bool isGrn) {
    QFileInfo fi(inputPath);
    _impl->fileNameLabel->setText(fi.fileName().isEmpty() ? tr("Unknown") : fi.fileName());
    _impl->formatLabel->setText(isGrn ? tr("Granny 1.2b (.grn)") : tr("glTF 2.0 Binary (.glb)"));

    qint64 sz = fi.exists() ? fi.size() : 0;
    _impl->sizeLabel->setText(formatFileSize(sz));

    if (model) {
        size_t totalVerts = 0;
        size_t totalTris = 0;
        size_t maxMeshVerts = 0;

        for (const auto& m : model->meshes) {
            totalVerts += m.vertices.size();
            totalTris += m.faces.size();
            maxMeshVerts = std::max(maxMeshVerts, m.vertices.size());
        }

        _impl->geomLabel->setText(QString("%1 meshes, %2 verts, %3 tris")
            .arg(model->meshes.size())
            .arg(totalVerts)
            .arg(totalTris));
        _impl->rigLabel->setText(QString("%1 bones").arg(model->bones.size()));

        if (isGrn) {
            // GRN source converting to GLB: GLB has full 32-bit indexing support
            _impl->is16BitExceeded = false;
            _impl->limitLabel->setText(tr("✓ 16-bit Safe (GRN Native)"));
            _impl->limitLabel->setStyleSheet("color: #28a745;");
            _impl->statusBadge->setText(tr("Loaded ✓"));
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
            emit limitDetected(false);
        } else {
            // GLB source converting to GRN: Granny 1.2b has a strict 16-bit vertex index limit (<= 65,535)
            bool exceeds16Bit = (maxMeshVerts > 64000);
            _impl->is16BitExceeded = exceeds16Bit;

            if (exceeds16Bit) {
                _impl->limitLabel->setText(tr("⚠️ >65k Verts (%1 max) → Optimizer ON").arg(maxMeshVerts));
                _impl->limitLabel->setStyleSheet("color: #d88000; font-weight: bold;");
                _impl->statusBadge->setText(tr("⚠️ >65k Verts"));
                _impl->statusBadge->setStyleSheet(
                    "QLabel {"
                    "  background: rgba(255, 140, 0, 0.18);"
                    "  color: #d88000;"
                    "  border-radius: 4px;"
                    "  padding: 2px 6px;"
                    "  font-size: 11px;"
                    "  font-weight: bold;"
                    "}"
                );
            } else {
                _impl->limitLabel->setText(tr("✓ 16-bit Safe (max %1 verts)").arg(maxMeshVerts));
                _impl->limitLabel->setStyleSheet("color: #28a745;");
                _impl->statusBadge->setText(tr("Loaded ✓"));
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
            emit limitDetected(exceeds16Bit);
        }
    } else {
        clearSource();
    }
}

void SourceInfoWidget::clearSource() {
    _impl->is16BitExceeded = false;
    _impl->fileNameLabel->setText(tr("None"));
    _impl->formatLabel->setText(tr("-"));
    _impl->sizeLabel->setText(tr("-"));
    _impl->geomLabel->setText(tr("-"));
    _impl->rigLabel->setText(tr("-"));
    _impl->limitLabel->setText(tr("-"));
    _impl->limitLabel->setStyleSheet("");
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
    emit limitDetected(false);
}

bool SourceInfoWidget::has16BitLimitExceeded() const {
    return _impl->is16BitExceeded;
}

} // namespace grn
