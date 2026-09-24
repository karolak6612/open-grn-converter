#pragma once

#include <QWidget>
#include <memory>
#include "../core/grn_types.h"

namespace grn {

class BoneInspectorWidget : public QWidget {
    Q_OBJECT
public:
    explicit BoneInspectorWidget(QWidget* parent = nullptr);
    ~BoneInspectorWidget() override;

    void setSourceModel(const GrnModel* model);
    void setTargetModel(const GrnModel* model);
    void clear();

signals:
    void boneSelected(int boneIndex, bool isSource);

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace grn
