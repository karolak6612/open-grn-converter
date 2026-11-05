#pragma once

#include <QWidget>
#include <memory>
#include "../../core/grn_types.h"

namespace grn {

class ViewportWidget;
class PlaybackBar;

enum class ComparisonLayout {
    SideBySide = 0,
    Stacked = 1,
    SourceOnly = 2,
    TargetOnly = 3
};

class ModelViewerPanel : public QWidget {
    Q_OBJECT
public:
    explicit ModelViewerPanel(QWidget* parent = nullptr);
    ~ModelViewerPanel() override;

    void loadSourceModel(const GrnModel* model, const QString& title = QString(), bool isZUp = false);
    void loadTargetModel(const GrnModel* model, const QString& title = QString(), bool isZUp = false);
    void loadModel(const GrnModel* model, const QString& title = QString(), bool isZUp = false) { loadSourceModel(model, title, isZUp); }

    void playSourceAnimation(const GrnAnimation* anim, const QString& animTitle = QString(), const std::vector<GrnBone>* animBones = nullptr);
    void playTargetAnimation(const GrnAnimation* anim, const QString& animTitle = QString(), const std::vector<GrnBone>* animBones = nullptr);
    void playAnimation(const GrnAnimation* anim, const QString& animTitle = QString(), const std::vector<GrnBone>* animBones = nullptr);
    void stopSourceAnimation();
    void stopTargetAnimation();
    void stopAnimation();

    void setModelScale(float s);
    void setComparisonLayout(ComparisonLayout layout);
    bool isSyncAnim() const;
    void setSyncAnim(bool enabled);
    void setSelectedBone(int boneIndex);
    void setShowSkeleton(bool enabled);
    void setShowBoneLabels(bool enabled);
    void setBoneLabelsLOD(bool enabled);
    bool boneLabelsLOD() const;

    ViewportWidget* sourceViewport() const;
    ViewportWidget* targetViewport() const;
    ViewportWidget* viewport() const { return sourceViewport(); }
    PlaybackBar* playbackBar() const;

signals:
    void detachRequested();
    void closeRequested();

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace grn
