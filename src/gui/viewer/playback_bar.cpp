#include "playback_bar.h"
#include "../gui_utils.h"

#include <oclero/qlementine/icons/Icons16.hpp>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QComboBox>

namespace grn {

using Icons16 = oclero::qlementine::icons::Icons16;

struct PlaybackBar::Impl {
    PlaybackBar& owner;

    QPushButton* playBtn{ nullptr };
    QPushButton* rewindBtn{ nullptr };
    QPushButton* loopBtn{ nullptr };
    QSlider* timeSlider{ nullptr };
    QLabel* timeLabel{ nullptr };
    QComboBox* speedCombo{ nullptr };

    bool isPlaying{ false };
    bool isLooping{ true };
    float currentDuration{ 0.0f };
    float currentTime{ 0.0f };
    bool isUserDragging{ false };

    explicit Impl(PlaybackBar& o) : owner(o) {
        setupUI();
    }

    void setupUI() {
        auto* layout = new QHBoxLayout(&owner);
        layout->setContentsMargins(6, 4, 6, 4);
        layout->setSpacing(6);

        // 1. Play / Pause
        playBtn = new QPushButton(makeThemedIcon(Icons16::Media_Play), QString(), &owner);
        playBtn->setFixedSize(28, 24);
        playBtn->setToolTip(owner.tr("Play / Pause"));
        QObject::connect(playBtn, &QPushButton::clicked, &owner, [this]() {
            isPlaying = !isPlaying;
            updatePlayIcon();
            emit owner.playToggled(isPlaying);
        });
        layout->addWidget(playBtn);

        // 2. Rewind
        rewindBtn = new QPushButton(makeThemedIcon(Icons16::Media_SkipBackward), QString(), &owner);
        rewindBtn->setFixedSize(28, 24);
        rewindBtn->setToolTip(owner.tr("Rewind to start (0.00s)"));
        QObject::connect(rewindBtn, &QPushButton::clicked, &owner, [this]() {
            emit owner.rewindClicked();
        });
        layout->addWidget(rewindBtn);

        // 3. Loop
        loopBtn = new QPushButton(makeThemedIcon(Icons16::Media_Repeat), QString(), &owner);
        loopBtn->setFixedSize(28, 24);
        loopBtn->setCheckable(true);
        loopBtn->setChecked(true);
        loopBtn->setToolTip(owner.tr("Toggle Repeat / Loop"));
        QObject::connect(loopBtn, &QPushButton::toggled, &owner, [this](bool chk) {
            isLooping = chk;
            emit owner.loopToggled(chk);
        });
        layout->addWidget(loopBtn);

        // 4. Time Scrubber Slider
        timeSlider = new QSlider(Qt::Horizontal, &owner);
        timeSlider->setRange(0, 1000);
        timeSlider->setValue(0);
        timeSlider->setFixedHeight(20);
        QObject::connect(timeSlider, &QSlider::sliderPressed, &owner, [this]() {
            isUserDragging = true;
        });
        QObject::connect(timeSlider, &QSlider::sliderReleased, &owner, [this]() {
            isUserDragging = false;
            if (currentDuration > 0.0f) {
                float t = (static_cast<float>(timeSlider->value()) / 1000.0f) * currentDuration;
                emit owner.timeSeeked(t);
            }
        });
        QObject::connect(timeSlider, &QSlider::valueChanged, &owner, [this](int val) {
            if (isUserDragging && currentDuration > 0.0f) {
                float t = (static_cast<float>(val) / 1000.0f) * currentDuration;
                updateTimeLabel(t, currentDuration);
                emit owner.timeSeeked(t);
            }
        });
        layout->addWidget(timeSlider, 1);

        // 5. Time Label
        timeLabel = new QLabel(owner.tr("0.00s / 0.00s"), &owner);
        timeLabel->setFixedWidth(90);
        timeLabel->setAlignment(Qt::AlignCenter);
        QFont f = timeLabel->font();
        f.setPointSize(8);
        timeLabel->setFont(f);
        layout->addWidget(timeLabel);

        // 6. Playback Speed Combo
        speedCombo = new QComboBox(&owner);
        speedCombo->addItems({"0.25x", "0.50x", "1.00x", "1.50x", "2.00x"});
        speedCombo->setCurrentIndex(2); // 1.00x
        speedCombo->setFixedHeight(24);
        speedCombo->setFixedWidth(64);
        speedCombo->setToolTip(owner.tr("Playback Speed"));
        QObject::connect(speedCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &owner, [this](int idx) {
            float spd = 1.0f;
            switch (idx) {
                case 0: spd = 0.25f; break;
                case 1: spd = 0.50f; break;
                case 2: spd = 1.00f; break;
                case 3: spd = 1.50f; break;
                case 4: spd = 2.00f; break;
            }
            emit owner.speedChanged(spd);
        });
        layout->addWidget(speedCombo);

        setEnabledState(false);
    }

    void updatePlayIcon() {
        playBtn->setIcon(makeThemedIcon(isPlaying ? Icons16::Media_Pause : Icons16::Media_Play));
    }

    void updateTimeLabel(float cur, float dur) {
        timeLabel->setText(QString("%1s / %2s")
            .arg(cur, 4, 'f', 2)
            .arg(dur, 4, 'f', 2));
    }

    void setEnabledState(bool enabled) {
        playBtn->setEnabled(enabled);
        rewindBtn->setEnabled(enabled);
        loopBtn->setEnabled(enabled);
        timeSlider->setEnabled(enabled);
        speedCombo->setEnabled(enabled);
        if (!enabled) {
            timeLabel->setText(owner.tr("0.00s / 0.00s"));
            timeSlider->setValue(0);
        }
    }
};

PlaybackBar::PlaybackBar(QWidget* parent)
    : QWidget(parent)
    , _impl(std::make_unique<Impl>(*this)) {}

PlaybackBar::~PlaybackBar() = default;

void PlaybackBar::setPlaying(bool playing) {
    _impl->isPlaying = playing;
    _impl->updatePlayIcon();
}

void PlaybackBar::setTimeAndDuration(float curTime, float duration) {
    _impl->currentTime = curTime;
    _impl->currentDuration = duration;

    bool hasAnim = (duration > 0.0f);
    _impl->setEnabledState(hasAnim);

    if (hasAnim) {
        _impl->updateTimeLabel(curTime, duration);
        if (!_impl->isUserDragging) {
            int sliderVal = static_cast<int>((curTime / duration) * 1000.0f);
            _impl->timeSlider->blockSignals(true);
            _impl->timeSlider->setValue(std::clamp(sliderVal, 0, 1000));
            _impl->timeSlider->blockSignals(false);
        }
    }
}

void PlaybackBar::setLooping(bool looping) {
    _impl->isLooping = looping;
    _impl->loopBtn->setChecked(looping);
}

void PlaybackBar::setSpeed(float speed) {
    int idx = 2;
    if (std::abs(speed - 0.25f) < 0.01f) idx = 0;
    else if (std::abs(speed - 0.50f) < 0.01f) idx = 1;
    else if (std::abs(speed - 1.00f) < 0.01f) idx = 2;
    else if (std::abs(speed - 1.50f) < 0.01f) idx = 3;
    else if (std::abs(speed - 2.00f) < 0.01f) idx = 4;
    _impl->speedCombo->setCurrentIndex(idx);
}

} // namespace grn
