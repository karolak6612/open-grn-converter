#include "model_viewer_panel.h"
#include "viewport_widget.h"
#include "playback_bar.h"
#include "../gui_utils.h"

#include <oclero/qlementine/icons/Icons16.hpp>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QFrame>

namespace grn {

using Icons16 = oclero::qlementine::icons::Icons16;

struct ModelViewerPanel::Impl {
    ModelViewerPanel& owner;

    QLabel* titleLabel{ nullptr };
    QLabel* statsLabel{ nullptr };
    QComboBox* shadingCombo{ nullptr };
    QPushButton* wireBtn{ nullptr };
    QPushButton* gridBtn{ nullptr };
    QPushButton* frameBtn{ nullptr };
    QPushButton* detachBtn{ nullptr };
    QPushButton* closeBtn{ nullptr };

    ViewportWidget* viewport{ nullptr };
    PlaybackBar* playbackBar{ nullptr };

    QString currentModelTitle;

    explicit Impl(ModelViewerPanel& o) : owner(o) {
        setupUI();
    }

    void setupUI() {
        auto* mainLayout = new QVBoxLayout(&owner);
        mainLayout->setContentsMargins(6, 6, 6, 6);
        mainLayout->setSpacing(4);

        // --- 1. Top Header Toolbar ---
        auto* headerLayout = new QHBoxLayout();
        headerLayout->setContentsMargins(4, 2, 4, 2);
        headerLayout->setSpacing(6);

        titleLabel = new QLabel(owner.tr("3D Preview"), &owner);
        QFont hf = titleLabel->font();
        hf.setBold(true);
        titleLabel->setFont(hf);
        headerLayout->addWidget(titleLabel);

        statsLabel = new QLabel(&owner);
        statsLabel->setStyleSheet("color: #888888;");
        QFont sf = statsLabel->font();
        sf.setPointSize(8);
        statsLabel->setFont(sf);
        headerLayout->addWidget(statsLabel);

        headerLayout->addStretch(1);

        // Shading dropdown
        shadingCombo = new QComboBox(&owner);
        shadingCombo->addItems({
            owner.tr("Textured Lit"),
            owner.tr("Unlit"),
            owner.tr("Clay Matcap"),
            owner.tr("Normals")
        });
        shadingCombo->setFixedHeight(24);
        QObject::connect(shadingCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &owner, [this](int idx) {
            ShadingMode mode = ShadingMode::TexturedLit;
            switch (idx) {
                case 0: mode = ShadingMode::TexturedLit; break;
                case 1: mode = ShadingMode::UnlitTextured; break;
                case 2: mode = ShadingMode::ClayMatcap; break;
                case 3: mode = ShadingMode::Normals; break;
            }
            viewport->setShadingMode(mode);
        });
        headerLayout->addWidget(shadingCombo);

        // Wireframe toggle
        wireBtn = new QPushButton(makeThemedIcon(Icons16::Shape_Cube), owner.tr("Wire"), &owner);
        wireBtn->setCheckable(true);
        wireBtn->setChecked(false);
        wireBtn->setFixedHeight(24);
        wireBtn->setToolTip(owner.tr("Toggle wireframe overlay"));
        QObject::connect(wireBtn, &QPushButton::toggled, &owner, [this](bool chk) {
            viewport->setWireframe(chk);
        });
        headerLayout->addWidget(wireBtn);

        // Grid toggle
        gridBtn = new QPushButton(makeThemedIcon(Icons16::Misc_Grid), owner.tr("Grid"), &owner);
        gridBtn->setCheckable(true);
        gridBtn->setChecked(true);
        gridBtn->setFixedHeight(24);
        gridBtn->setToolTip(owner.tr("Toggle ground plane grid"));
        QObject::connect(gridBtn, &QPushButton::toggled, &owner, [this](bool chk) {
            viewport->setShowGrid(chk);
        });
        headerLayout->addWidget(gridBtn);

        // Frame Bounds
        frameBtn = new QPushButton(makeThemedIcon(Icons16::Action_Enlarge), owner.tr("Frame"), &owner);
        frameBtn->setFixedHeight(24);
        frameBtn->setToolTip(owner.tr("Fit model inside camera view"));
        QObject::connect(frameBtn, &QPushButton::clicked, &owner, [this]() {
            viewport->frameBounds();
        });
        headerLayout->addWidget(frameBtn);

        // Detach / Pop Out
        detachBtn = new QPushButton(makeThemedIcon(Icons16::Action_ExternalLink), QString(), &owner);
        detachBtn->setFixedSize(24, 24);
        detachBtn->setToolTip(owner.tr("Pop out to separate window"));
        QObject::connect(detachBtn, &QPushButton::clicked, &owner, [this]() {
            emit owner.detachRequested();
        });
        headerLayout->addWidget(detachBtn);

        // Close
        closeBtn = new QPushButton(makeThemedIcon(Icons16::Action_Close), QString(), &owner);
        closeBtn->setFixedSize(24, 24);
        closeBtn->setToolTip(owner.tr("Close 3D preview"));
        QObject::connect(closeBtn, &QPushButton::clicked, &owner, [this]() {
            emit owner.closeRequested();
        });
        headerLayout->addWidget(closeBtn);

        mainLayout->addLayout(headerLayout);

        // --- 2. Center 3D Viewport ---
        viewport = new ViewportWidget(&owner);
        viewport->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        viewport->setMinimumSize(320, 240);
        mainLayout->addWidget(viewport, 1);

        // --- 3. Bottom Playback Toolbar ---
        playbackBar = new PlaybackBar(&owner);
        playbackBar->setFixedHeight(32);
        mainLayout->addWidget(playbackBar);

        // Connect Viewport <-> PlaybackBar
        QObject::connect(viewport, &ViewportWidget::playbackTimeChanged, playbackBar, &PlaybackBar::setTimeAndDuration);
        QObject::connect(viewport, &ViewportWidget::playbackStateChanged, playbackBar, &PlaybackBar::setPlaying);

        QObject::connect(playbackBar, &PlaybackBar::playToggled, viewport, &ViewportWidget::setPlaying);
        QObject::connect(playbackBar, &PlaybackBar::rewindClicked, &owner, [this]() {
            viewport->setTime(0.0f);
        });
        QObject::connect(playbackBar, &PlaybackBar::loopToggled, viewport, &ViewportWidget::setLooping);
        QObject::connect(playbackBar, &PlaybackBar::timeSeeked, viewport, &ViewportWidget::setTime);
        QObject::connect(playbackBar, &PlaybackBar::speedChanged, viewport, &ViewportWidget::setPlaybackSpeed);

        QObject::connect(viewport, &ViewportWidget::modelLoaded, &owner, [this](size_t meshes, size_t verts, size_t bones) {
            if (meshes > 0 || verts > 0) {
                statsLabel->setText(QString("(%1 meshes, %2 verts, %3 bones)")
                    .arg(meshes)
                    .arg(verts)
                    .arg(bones));
            } else {
                statsLabel->clear();
            }
        });
    }
};

ModelViewerPanel::ModelViewerPanel(QWidget* parent)
    : QWidget(parent)
    , _impl(std::make_unique<Impl>(*this)) {}

ModelViewerPanel::~ModelViewerPanel() = default;

void ModelViewerPanel::loadModel(const GrnModel* model, const QString& title) {
    _impl->currentModelTitle = title;
    _impl->titleLabel->setText(title.isEmpty() ? tr("3D Preview") : title);
    _impl->viewport->loadModel(model);
}

void ModelViewerPanel::playAnimation(const GrnAnimation* anim, const QString& animTitle) {
    if (anim) {
        QString disp = _impl->currentModelTitle;
        if (!animTitle.isEmpty()) {
            disp += QString(" — %1").arg(animTitle);
        }
        _impl->titleLabel->setText(disp);
        _impl->viewport->playAnimation(anim);
    } else {
        _impl->titleLabel->setText(_impl->currentModelTitle.isEmpty() ? tr("3D Preview") : _impl->currentModelTitle);
        _impl->viewport->stopAnimation();
    }
}

void ModelViewerPanel::stopAnimation() {
    _impl->viewport->stopAnimation();
}

ViewportWidget* ModelViewerPanel::viewport() const {
    return _impl->viewport;
}

PlaybackBar* ModelViewerPanel::playbackBar() const {
    return _impl->playbackBar;
}

} // namespace grn
