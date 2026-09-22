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

    void loadSourceModel(const GrnModel* model, const QString& title = QString());
    void loadTargetModel(const GrnModel* model, const QString& title = QString());
    void loadModel(const GrnModel* model, const QString& title = QString()) { loadSourceModel(model, title); }

    void playSourceAnimation(const GrnAnimation* anim, const QString& animTitle = QString());
    void playTargetAnimation(const GrnAnimation* anim, const QString& animTitle = QString());
    void playAnimation(const GrnAnimation* anim, const QString& animTitle = QString());
    void stopAnimation();

    void setModelScale(float s);
    void setComparisonLayout(ComparisonLayout layout);

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
