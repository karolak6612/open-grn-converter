#pragma once

#include <QWidget>
#include <QString>
#include <memory>

namespace grn {

class GlbOptionsWidget : public QWidget {
    Q_OBJECT
public:
    explicit GlbOptionsWidget(QWidget* parent = nullptr);
    ~GlbOptionsWidget() override;

    void setModel(const QString& modelPath);

    bool convertCoordinates() const;
    bool isTargetHeightMode() const;
    float scaleFactor() const;
    float targetHeight() const;
    bool compressVTex() const;
    bool splitAnimations() const;
    bool autoSplit16Bit() const;
    bool isMeshOptimizerEnabled() const;
    bool decimateEnabled() const;
    uint32_t targetMaxVertices() const;

    void setSplitAnimations(bool split);
    void setMeshOptimizerEnabled(bool enabled);
    void setModelAnalysis(const struct GrnModel* model);
    void reset();

signals:
    void optionsChanged();
    void animationSelected(int animIndex);

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace grn
