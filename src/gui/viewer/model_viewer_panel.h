#pragma once

#include <QWidget>
#include <memory>
#include "../../core/grn_types.h"

namespace grn {

class ViewportWidget;
class PlaybackBar;

class ModelViewerPanel : public QWidget {
    Q_OBJECT
public:
    explicit ModelViewerPanel(QWidget* parent = nullptr);
    ~ModelViewerPanel() override;

    void loadModel(const GrnModel* model, const QString& title = QString());
    void playAnimation(const GrnAnimation* anim, const QString& animTitle = QString());
    void stopAnimation();

    ViewportWidget* viewport() const;
    PlaybackBar* playbackBar() const;

signals:
    void detachRequested();
    void closeRequested();

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace grn
