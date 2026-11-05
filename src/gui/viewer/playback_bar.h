#pragma once

#include <QWidget>
#include <memory>

namespace grn {

class PlaybackBar : public QWidget {
    Q_OBJECT
public:
    explicit PlaybackBar(QWidget* parent = nullptr);
    ~PlaybackBar() override;

    void setPlaying(bool playing);
    void setTimeAndDuration(float curTime, float duration);
    void setLooping(bool looping);
    void setSpeed(float speed);

signals:
    void playToggled(bool playing);
    void rewindClicked();
    void loopToggled(bool looping);
    void timeSeeked(float seconds);
    void speedChanged(float speed);

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace grn
