#pragma once

#include <QWidget>
#include <QString>
#include <memory>
#include "core/grn_types.h"

namespace grn {

class SourceInfoWidget : public QWidget {
    Q_OBJECT
public:
    explicit SourceInfoWidget(QWidget* parent = nullptr);
    ~SourceInfoWidget() override;

    void setSourceModel(const GrnModel* model, const QString& inputPath, bool isGrn);
    void clearSource();

    bool has16BitLimitExceeded() const;

signals:
    void limitDetected(bool exceeded);

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace grn
