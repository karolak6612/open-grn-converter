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
#include <QSplitter>
#include <QFrame>

namespace grn {

using Icons16 = oclero::qlementine::icons::Icons16;

struct ModelViewerPanel::Impl {
    ModelViewerPanel& owner;

    QComboBox* layoutCombo{ nullptr };
    QComboBox* shadingCombo{ nullptr };
    QPushButton* wireBtn{ nullptr };
    QPushButton* gridBtn{ nullptr };
    QPushButton* skeletonBtn{ nullptr };
    QPushButton* labelsBtn{ nullptr };
    QPushButton* syncCamBtn{ nullptr };
    QPushButton* syncAnimBtn{ nullptr };
    QPushButton* closeBtn{ nullptr };

    QSplitter* splitter{ nullptr };
    QWidget* sourceContainer{ nullptr };
    QLabel* sourceBadge{ nullptr };
    ViewportWidget* sourceViewport{ nullptr };

    QWidget* targetContainer{ nullptr };
    QLabel* targetBadge{ nullptr };
    ViewportWidget* targetViewport{ nullptr };

    PlaybackBar* playbackBar{ nullptr };

    QString currentSourceTitle;
    QString currentTargetTitle;
    bool isSyncingCam{ false };

    explicit Impl(ModelViewerPanel& o) : owner(o) {
        setupUI();
    }

    void setupUI() {
        auto* mainLayout = new QVBoxLayout(&owner);
        mainLayout->setContentsMargins(4, 4, 4, 4);
        mainLayout->setSpacing(4);

        // --- 1. Top Header Toolbar ---
        auto* headerLayout = new QHBoxLayout();
        headerLayout->setContentsMargins(2, 2, 2, 2);
        headerLayout->setSpacing(4);

        // Layout mode combo
        layoutCombo = new QComboBox(&owner);
        layoutCombo->addItems({
            owner.tr("Side-by-Side"),
            owner.tr("Stacked (Up/Down)"),
            owner.tr("Source Only"),
            owner.tr("Target Only")
        });
        layoutCombo->setFixedHeight(24);
        QObject::connect(layoutCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &owner, [this](int idx) {
            updateLayoutMode(static_cast<ComparisonLayout>(idx));
        });
        headerLayout->addWidget(layoutCombo);

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
            sourceViewport->setShadingMode(mode);
            targetViewport->setShadingMode(mode);
        });
        headerLayout->addWidget(shadingCombo);

        // Wireframe toggle
        wireBtn = new QPushButton(makeThemedIcon(Icons16::Shape_Cube), owner.tr("Wire"), &owner);
        wireBtn->setCheckable(true);
        wireBtn->setChecked(false);
        wireBtn->setFixedHeight(24);
        wireBtn->setToolTip(owner.tr("Toggle wireframe overlay"));
        QObject::connect(wireBtn, &QPushButton::toggled, &owner, [this](bool chk) {
            sourceViewport->setWireframe(chk);
            targetViewport->setWireframe(chk);
        });
        headerLayout->addWidget(wireBtn);

        // Grid toggle
        gridBtn = new QPushButton(makeThemedIcon(Icons16::Misc_Grid), owner.tr("Grid"), &owner);
        gridBtn->setCheckable(true);
        gridBtn->setChecked(true);
        gridBtn->setFixedHeight(24);
        gridBtn->setToolTip(owner.tr("Toggle ground plane grid"));
        QObject::connect(gridBtn, &QPushButton::toggled, &owner, [this](bool chk) {
            sourceViewport->setShowGrid(chk);
            targetViewport->setShowGrid(chk);
        });
        headerLayout->addWidget(gridBtn);

        // Skeleton overlay toggle
        skeletonBtn = new QPushButton(makeThemedIcon(Icons16::Shape_Cube), owner.tr("Skeleton"), &owner);
        skeletonBtn->setCheckable(true);
        skeletonBtn->setChecked(false);
        skeletonBtn->setFixedHeight(24);
        skeletonBtn->setToolTip(owner.tr("Toggle 3D skeleton bone hierarchy wireframe"));
        QObject::connect(skeletonBtn, &QPushButton::toggled, &owner, [this](bool chk) {
            sourceViewport->setShowSkeleton(chk);
            targetViewport->setShowSkeleton(chk);
        });
        headerLayout->addWidget(skeletonBtn);

        // Bone labels toggle
        labelsBtn = new QPushButton(makeThemedIcon(Icons16::Misc_Tag), owner.tr("Labels"), &owner);
        labelsBtn->setCheckable(true);
        labelsBtn->setChecked(false);
        labelsBtn->setFixedHeight(24);
        labelsBtn->setToolTip(owner.tr("Toggle projected 2D bone name labels"));
        QObject::connect(labelsBtn, &QPushButton::toggled, &owner, [this](bool chk) {
            sourceViewport->setShowBoneLabels(chk);
            targetViewport->setShowBoneLabels(chk);
        });
        headerLayout->addWidget(labelsBtn);

        headerLayout->addStretch(1);

        // Sync camera toggle
        syncCamBtn = new QPushButton(makeThemedIcon(Icons16::Action_Refresh), owner.tr("Sync Cam"), &owner);
        syncCamBtn->setCheckable(true);
        syncCamBtn->setChecked(true);
        syncCamBtn->setFixedHeight(24);
        syncCamBtn->setToolTip(owner.tr("Synchronize camera orbit, pan, and zoom between viewports"));
        headerLayout->addWidget(syncCamBtn);

        // Sync anim toggle
        syncAnimBtn = new QPushButton(makeThemedIcon(Icons16::Action_Refresh), owner.tr("Sync Anim"), &owner);
        syncAnimBtn->setCheckable(true);
        syncAnimBtn->setChecked(true);
        syncAnimBtn->setFixedHeight(24);
        syncAnimBtn->setToolTip(owner.tr("Synchronize animation playback and timeline between viewports"));
        headerLayout->addWidget(syncAnimBtn);

        mainLayout->addLayout(headerLayout);

        // --- 2. Center Dual Viewport Splitter ---
        splitter = new QSplitter(Qt::Horizontal, &owner);
        splitter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

        // 2A. Source Container
        sourceContainer = new QWidget(splitter);
        auto* srcLayout = new QVBoxLayout(sourceContainer);
        srcLayout->setContentsMargins(0, 0, 0, 0);
        srcLayout->setSpacing(2);

        sourceBadge = new QLabel(owner.tr("Source: No model loaded"), sourceContainer);
        sourceBadge->setStyleSheet(
            "QLabel {"
            "  background: rgba(0, 0, 0, 0.45);"
            "  color: #ffffff;"
            "  border-radius: 4px;"
            "  padding: 3px 8px;"
            "  font-size: 11px;"
            "  font-weight: bold;"
            "}"
        );
        sourceBadge->setFixedHeight(22);
        srcLayout->addWidget(sourceBadge);

        sourceViewport = new ViewportWidget(sourceContainer);
        sourceViewport->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        sourceViewport->setMinimumSize(180, 160);
        srcLayout->addWidget(sourceViewport, 1);
        splitter->addWidget(sourceContainer);

        // 2B. Target Container
        targetContainer = new QWidget(splitter);
        auto* tgtLayout = new QVBoxLayout(targetContainer);
        tgtLayout->setContentsMargins(0, 0, 0, 0);
        tgtLayout->setSpacing(2);

        targetBadge = new QLabel(owner.tr("Target: Waiting for conversion"), targetContainer);
        targetBadge->setStyleSheet(
            "QLabel {"
            "  background: rgba(0, 0, 0, 0.45);"
            "  color: #ffffff;"
            "  border-radius: 4px;"
            "  padding: 3px 8px;"
            "  font-size: 11px;"
            "  font-weight: bold;"
            "}"
        );
        targetBadge->setFixedHeight(22);
        tgtLayout->addWidget(targetBadge);

        targetViewport = new ViewportWidget(targetContainer);
        targetViewport->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        targetViewport->setMinimumSize(180, 160);
        tgtLayout->addWidget(targetViewport, 1);
        splitter->addWidget(targetContainer);

        // Set 50/50 initial split
        splitter->setSizes({ 500, 500 });
        mainLayout->addWidget(splitter, 1);

        // --- 3. Camera Synchronization ---
        QObject::connect(sourceViewport, &ViewportWidget::cameraChanged, &owner, [this](const OrbitCamera& cam) {
            if (syncCamBtn->isChecked() && !isSyncingCam) {
                isSyncingCam = true;
                targetViewport->syncCamera(cam);
                isSyncingCam = false;
            }
        });
        QObject::connect(targetViewport, &ViewportWidget::cameraChanged, &owner, [this](const OrbitCamera& cam) {
            if (syncCamBtn->isChecked() && !isSyncingCam) {
                isSyncingCam = true;
                sourceViewport->syncCamera(cam);
                isSyncingCam = false;
            }
        });

        // --- 4. Bottom Playback Toolbar ---
        playbackBar = new PlaybackBar(&owner);
        playbackBar->setFixedHeight(32);
        mainLayout->addWidget(playbackBar);

        // Connect Viewports <-> PlaybackBar
        auto updatePlaybackTime = [this]() {
            float srcDur = sourceViewport->duration();
            float tgtDur = targetViewport->duration();
            if (srcDur > 0.0f) {
                playbackBar->setTimeAndDuration(sourceViewport->currentTime(), srcDur);
            } else if (tgtDur > 0.0f) {
                playbackBar->setTimeAndDuration(targetViewport->currentTime(), tgtDur);
            } else {
                playbackBar->setTimeAndDuration(0.0f, 0.0f);
            }
        };
        auto updatePlaybackState = [this]() {
            bool playing = sourceViewport->isPlaying() || targetViewport->isPlaying();
            playbackBar->setPlaying(playing);
        };

        QObject::connect(sourceViewport, &ViewportWidget::playbackTimeChanged, &owner, [updatePlaybackTime](float, float) {
            updatePlaybackTime();
        });
        QObject::connect(targetViewport, &ViewportWidget::playbackTimeChanged, &owner, [updatePlaybackTime](float, float) {
            updatePlaybackTime();
        });
        QObject::connect(sourceViewport, &ViewportWidget::playbackStateChanged, &owner, [updatePlaybackState](bool) {
            updatePlaybackState();
        });
        QObject::connect(targetViewport, &ViewportWidget::playbackStateChanged, &owner, [updatePlaybackState](bool) {
            updatePlaybackState();
        });

        QObject::connect(playbackBar, &PlaybackBar::playToggled, &owner, [this](bool p) {
            sourceViewport->setPlaying(p);
            targetViewport->setPlaying(p);
        });
        QObject::connect(playbackBar, &PlaybackBar::rewindClicked, &owner, [this]() {
            sourceViewport->setTime(0.0f);
            targetViewport->setTime(0.0f);
        });
        QObject::connect(playbackBar, &PlaybackBar::loopToggled, &owner, [this](bool l) {
            sourceViewport->setLooping(l);
            targetViewport->setLooping(l);
        });
        QObject::connect(playbackBar, &PlaybackBar::timeSeeked, &owner, [this](float t) {
            sourceViewport->setTime(t);
            targetViewport->setTime(t);
        });
        QObject::connect(playbackBar, &PlaybackBar::speedChanged, &owner, [this](float spd) {
            sourceViewport->setPlaybackSpeed(spd);
            targetViewport->setPlaybackSpeed(spd);
        });

        // Model loading badge update
        QObject::connect(sourceViewport, &ViewportWidget::modelLoaded, &owner, [this](size_t meshes, size_t verts, size_t bones) {
            if (meshes > 0 || verts > 0) {
                sourceBadge->setText(QString("Source: %1 (%2 meshes, %3 verts, %4 bones)")
                    .arg(currentSourceTitle.isEmpty() ? owner.tr("Model") : currentSourceTitle)
                    .arg(meshes).arg(verts).arg(bones));
            } else {
                sourceBadge->setText(owner.tr("Source: No model loaded"));
            }
        });

        QObject::connect(targetViewport, &ViewportWidget::modelLoaded, &owner, [this](size_t meshes, size_t verts, size_t bones) {
            if (meshes > 0 || verts > 0) {
                targetBadge->setText(QString("Target: %1 (%2 meshes, %3 verts, %4 bones)")
                    .arg(currentTargetTitle.isEmpty() ? owner.tr("Model") : currentTargetTitle)
                    .arg(meshes).arg(verts).arg(bones));
            } else {
                targetBadge->setText(owner.tr("Target: Waiting for conversion"));
            }
        });
    }

    void updateLayoutMode(ComparisonLayout layout) {
        switch (layout) {
            case ComparisonLayout::SideBySide:
                splitter->setOrientation(Qt::Horizontal);
                sourceContainer->setVisible(true);
                targetContainer->setVisible(true);
                splitter->setSizes({ 500, 500 });
                break;
            case ComparisonLayout::Stacked:
                splitter->setOrientation(Qt::Vertical);
                sourceContainer->setVisible(true);
                targetContainer->setVisible(true);
                splitter->setSizes({ 300, 300 });
                break;
            case ComparisonLayout::SourceOnly:
                sourceContainer->setVisible(true);
                targetContainer->setVisible(false);
                break;
            case ComparisonLayout::TargetOnly:
                sourceContainer->setVisible(false);
                targetContainer->setVisible(true);
                break;
        }
    }
};

ModelViewerPanel::ModelViewerPanel(QWidget* parent)
    : QWidget(parent)
    , _impl(std::make_unique<Impl>(*this)) {}

ModelViewerPanel::~ModelViewerPanel() = default;

void ModelViewerPanel::loadSourceModel(const GrnModel* model, const QString& title, bool isZUp) {
    _impl->currentSourceTitle = title;
    _impl->sourceViewport->setZUpMode(isZUp);
    _impl->sourceViewport->loadModel(model);
    if (!model) {
        _impl->sourceBadge->setText(tr("Source: No model loaded"));
    }
}

void ModelViewerPanel::loadTargetModel(const GrnModel* model, const QString& title, bool isZUp) {
    _impl->currentTargetTitle = title;
    _impl->targetViewport->setZUpMode(isZUp);
    _impl->targetViewport->loadModel(model);
    if (!model) {
        _impl->targetBadge->setText(tr("Target: Waiting for conversion"));
    }
}

void ModelViewerPanel::playSourceAnimation(const GrnAnimation* anim, const QString& /*animTitle*/, const std::vector<GrnBone>* animBones) {
    _impl->sourceViewport->playAnimation(anim, animBones);
}

void ModelViewerPanel::playTargetAnimation(const GrnAnimation* anim, const QString& /*animTitle*/, const std::vector<GrnBone>* animBones) {
    _impl->targetViewport->playAnimation(anim, animBones);
}

void ModelViewerPanel::playAnimation(const GrnAnimation* anim, const QString& animTitle, const std::vector<GrnBone>* animBones) {
    _impl->sourceViewport->playAnimation(anim, animBones);
    _impl->targetViewport->playAnimation(anim, animBones);
    (void)animTitle;
}

void ModelViewerPanel::stopSourceAnimation() {
    _impl->sourceViewport->stopAnimation();
}

void ModelViewerPanel::stopTargetAnimation() {
    _impl->targetViewport->stopAnimation();
}

void ModelViewerPanel::stopAnimation() {
    _impl->sourceViewport->stopAnimation();
    _impl->targetViewport->stopAnimation();
}

void ModelViewerPanel::setModelScale(float s) {
    _impl->sourceViewport->setModelScale(s);
}

void ModelViewerPanel::setComparisonLayout(ComparisonLayout layout) {
    _impl->layoutCombo->setCurrentIndex(static_cast<int>(layout));
    _impl->updateLayoutMode(layout);
}

bool ModelViewerPanel::isSyncAnim() const {
    return _impl->syncAnimBtn && _impl->syncAnimBtn->isChecked();
}

void ModelViewerPanel::setSyncAnim(bool enabled) {
    if (_impl->syncAnimBtn) {
        _impl->syncAnimBtn->setChecked(enabled);
    }
}

void ModelViewerPanel::setSelectedBone(int boneIndex) {
    if (_impl->sourceViewport) _impl->sourceViewport->setSelectedBone(boneIndex);
    if (_impl->targetViewport) _impl->targetViewport->setSelectedBone(boneIndex);
}

void ModelViewerPanel::setShowSkeleton(bool enabled) {
    if (_impl->skeletonBtn) _impl->skeletonBtn->setChecked(enabled);
    if (_impl->sourceViewport) _impl->sourceViewport->setShowSkeleton(enabled);
    if (_impl->targetViewport) _impl->targetViewport->setShowSkeleton(enabled);
}

void ModelViewerPanel::setShowBoneLabels(bool enabled) {
    if (_impl->labelsBtn) _impl->labelsBtn->setChecked(enabled);
    if (_impl->sourceViewport) _impl->sourceViewport->setShowBoneLabels(enabled);
    if (_impl->targetViewport) _impl->targetViewport->setShowBoneLabels(enabled);
}

void ModelViewerPanel::setBoneLabelsLOD(bool enabled) {
    if (_impl->sourceViewport) _impl->sourceViewport->setBoneLabelsLOD(enabled);
    if (_impl->targetViewport) _impl->targetViewport->setBoneLabelsLOD(enabled);
}

bool ModelViewerPanel::boneLabelsLOD() const {
    if (_impl->sourceViewport) return _impl->sourceViewport->boneLabelsLOD();
    return false;
}

ViewportWidget* ModelViewerPanel::sourceViewport() const {
    return _impl->sourceViewport;
}

ViewportWidget* ModelViewerPanel::targetViewport() const {
    return _impl->targetViewport;
}

PlaybackBar* ModelViewerPanel::playbackBar() const {
    return _impl->playbackBar;
}

} // namespace grn
