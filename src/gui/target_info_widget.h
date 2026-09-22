#pragma once

#include <QWidget>
#include <QString>
#include <memory>
#include <filesystem>
#include "core/grn_types.h"

namespace grn {

class TargetInfoWidget : public QWidget {
    Q_OBJECT
public:
    explicit TargetInfoWidget(QWidget* parent = nullptr);
    ~TargetInfoWidget() override;

    void setConverting(bool converting);
    void setTargetModel(const GrnModel* model, const QString& outputPath, bool isGlb, const QString& validationSummary = QString());
    void clearTarget();

    void selectAnimationItem(int index);

signals:
    void animationSelected(int animIndex, const QString& clipPath);
    void openFolderRequested(const QString& folderPath);

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace grn
