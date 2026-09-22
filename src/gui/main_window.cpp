#include "main_window.h"
#include "conversion_worker.h"
#include "grn_options_widget.h"
#include "glb_options_widget.h"
#include "log_drawer.h"
#include "section_card.h"
#include "gui_utils.h"
#include "source_info_widget.h"
#include "target_info_widget.h"
#include "viewer/model_viewer_panel.h"
#include "viewer/viewport_widget.h"
#include "../core/grn_parser.h"
#include "../gltf/glb_reader.h"

#include <oclero/qlementine/style/QlementineStyle.hpp>
#include <oclero/qlementine/style/ThemeManager.hpp>
#include <oclero/qlementine/utils/IconUtils.hpp>
#include <oclero/qlementine/utils/WidgetUtils.hpp>

#include <oclero/qlementine/widgets/LineEdit.hpp>
#include <oclero/qlementine/widgets/NavigationBar.hpp>
#include <oclero/qlementine/widgets/AboutDialog.hpp>
#include <oclero/qlementine/widgets/LoadingSpinner.hpp>
#include <oclero/qlementine/icons/Icons16.hpp>

#include <QPointer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QStackedWidget>
#include <QMenuBar>
#include <QToolButton>
#include <QStatusBar>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QFileDialog>
#include <QFileInfo>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QClipboard>
#include <QApplication>
#include <QGroupBox>
#include <QFrame>
#include <QDialog>
#include <QAction>
#include <QSplitter>

namespace grn {

using Icons16 = oclero::qlementine::icons::Icons16;

struct MainWindow::Impl {
    MainWindow& owner;
    QPointer<oclero::qlementine::ThemeManager> themeManager;
    ConversionWorker* worker{ nullptr };

    // Layout & 3D Viewer
    QSplitter* mainSplitter{ nullptr };
    QWidget* converterPanel{ nullptr };
    ModelViewerPanel* modelViewer{ nullptr };
    SourceInfoWidget* sourceInfoWidget{ nullptr };
    TargetInfoWidget* targetInfoWidget{ nullptr };
    QAction* previewAction{ nullptr };
    QToolButton* previewToggleBtn{ nullptr };
    QPointer<QDialog> detachedDialog;
    std::optional<GrnModel> loadedModel;
    std::optional<GrnModel> convertedModel;
    std::optional<GrnModel> cachedExternalAnimModel;
    QString lastOutputPath;
    bool isPreviewVisible{ true };

    int previousTabIndex{ 0 };
    QString cachedGrnInputPath;
    QString cachedGrnOutputPath;
    QString cachedGlbInputPath;
    QString cachedGlbOutputPath;

    QVBoxLayout* rootLayout{ nullptr };
    QMenuBar* menuBar{ nullptr };
    QStatusBar* statusBar{ nullptr };
    QLabel* statusLabel{ nullptr };
    oclero::qlementine::LoadingSpinner* spinner{ nullptr };
    QProgressBar* progressBar{ nullptr };
    QToolButton* logDrawerBtn{ nullptr };

    // Top Navigation Tabs
    oclero::qlementine::NavigationBar* navBar{ nullptr };

    // Source & Destination
    oclero::qlementine::LineEdit* inputEdit{ nullptr };
    oclero::qlementine::LineEdit* outputEdit{ nullptr };
    QPushButton* browseFileBtn{ nullptr };
    QPushButton* browseOutBtn{ nullptr };

    // Stacked Options
    QStackedWidget* optionsStack{ nullptr };
    GrnOptionsWidget* grnOptions{ nullptr };
    GlbOptionsWidget* glbOptions{ nullptr };
    QPushButton* convertBtn{ nullptr };

    // Bottom Tray: Log Drawer
    LogDrawer* logDrawer{ nullptr };
    QWidget* fileBoxWidget{ nullptr };

    Impl(MainWindow& o, oclero::qlementine::ThemeManager* tm)
        : owner(o)
        , themeManager(tm) {}

    void setupUI() {
        worker = new ConversionWorker(&owner);
        QObject::connect(worker, &ConversionWorker::progressUpdated, &owner, [this](float p) {
            onProgress(p);
        });
        QObject::connect(worker, &ConversionWorker::logMessage, &owner, [this](const QString& msg, int lvl) {
            onLog(msg, lvl);
        });
        QObject::connect(worker, &ConversionWorker::conversionFinished, &owner, [this](bool ok, const QString& summary) {
            onFinished(ok, summary);
        });

        setupMenuBar();
        setupNavBar();
        setupSourceAndDestination();
        setupOptionsStack();
        setupConvertButton();
        setupStatusBar();
        setupLogDrawer();
        setupLayout();

        updateActionState();
        updateModelInOptions();
    }

    void setupMenuBar() {
        menuBar = new QMenuBar(nullptr);

        auto* fileMenu = menuBar->addMenu(owner.tr("&File"));
        fileMenu->addAction(makeThemedIcon(Icons16::Document_Open), owner.tr("Open &File..."), QKeySequence::Open, [this]() {
            browseInput();
        });
        fileMenu->addSeparator();
        fileMenu->addAction(makeThemedIcon(Icons16::Action_Close), owner.tr("E&xit"),
                            QKeySequence(Qt::CTRL | Qt::Key_Q), []() {
            qApp->quit();
        });

        auto* editMenu = menuBar->addMenu(owner.tr("&Edit"));
        editMenu->addAction(makeThemedIcon(Icons16::Action_Reset), owner.tr("&Reset Settings"), [this]() {
            resetOptions();
        });
        editMenu->addAction(makeThemedIcon(Icons16::Action_Copy), owner.tr("&Copy Log"), QKeySequence::Copy, [this]() {
            QApplication::clipboard()->setText(logDrawer->allText());
        });
        editMenu->addAction(makeThemedIcon(Icons16::Action_Trash), owner.tr("&Clear Log"), [this]() {
            logDrawer->clearLog();
        });

        auto* viewMenu = menuBar->addMenu(owner.tr("&View"));
        previewAction = viewMenu->addAction(makeThemedIcon(Icons16::Shape_Cube), owner.tr("3D &Preview"), QKeySequence(Qt::CTRL | Qt::Key_P), [this]() {
            togglePreview(!isPreviewVisible);
        });
        previewAction->setCheckable(true);
        previewAction->setChecked(true);

        auto* helpMenu = menuBar->addMenu(owner.tr("&Help"));
        helpMenu->addAction(makeThemedIcon(Icons16::Misc_Help), owner.tr("&About GRN Converter..."), [this]() {
            oclero::qlementine::AboutDialog dlg(&owner);
            dlg.setApplicationName(owner.tr("GRN <-> GLB Converter"));
            dlg.setDescription(owner.tr("Production C++20 Bidirectional Granny 1.2b and glTF 2.0 Converter.\n"
                                        "Engineered with Qt6 and Qlementine Modern Theme."));
            dlg.setApplicationVersion("1.0.0");
            dlg.exec();
        });
    }

    void setupNavBar() {
        navBar = new oclero::qlementine::NavigationBar(&owner);
        navBar->setFocusPolicy(Qt::NoFocus);
        navBar->setItemsShouldExpand(true);
        navBar->addItem(owner.tr("GRN → GLB"), QIcon());
        navBar->addItem(owner.tr("GLB → GRN"), QIcon());
        navBar->setCurrentIndex(0);

        QObject::connect(navBar, &oclero::qlementine::NavigationBar::currentIndexChanged, &owner, [this]() {
            onTabChanged(navBar->currentIndex());
        });
    }

    void onTabChanged(int newIdx) {
        if (newIdx == previousTabIndex) return;

        // 1. Cache current paths for previous tab
        if (previousTabIndex == 0) {
            cachedGrnInputPath = inputEdit->text();
            cachedGrnOutputPath = outputEdit->text();
        } else {
            cachedGlbInputPath = inputEdit->text();
            cachedGlbOutputPath = outputEdit->text();
        }
        previousTabIndex = newIdx;

        optionsStack->setCurrentIndex(newIdx);

        // 2. Restore cached paths for new tab
        inputEdit->blockSignals(true);
        outputEdit->blockSignals(true);
        if (newIdx == 0) {
            inputEdit->setText(cachedGrnInputPath);
            outputEdit->setText(cachedGrnOutputPath);
        } else {
            inputEdit->setText(cachedGlbInputPath);
            outputEdit->setText(cachedGlbOutputPath);
        }
        inputEdit->blockSignals(false);
        outputEdit->blockSignals(false);

        // 3. Clear target model / preview / target info widget
        convertedModel.reset();
        lastOutputPath.clear();
        if (targetInfoWidget) {
            targetInfoWidget->clearTarget();
        }
        if (sourceInfoWidget) {
            sourceInfoWidget->clearSource();
        }
        if (modelViewer) {
            modelViewer->loadTargetModel(nullptr, QString());
        }

        // 4. Update source model & preview for new tab
        updateActionState();
        updateModelInOptions();
    }

    void setupSourceAndDestination() {
        auto* srcDestContainer = new QWidget(&owner);
        fileBoxWidget = srcDestContainer;

        auto* fileLayout = new QVBoxLayout(srcDestContainer);
        fileLayout->setContentsMargins(0, 0, 0, 0);
        fileLayout->setSpacing(4);

        auto* srcDestLabel = new QLabel(owner.tr("Source & Destination"), srcDestContainer);
        QFont hf = srcDestLabel->font();
        hf.setBold(true);
        srcDestLabel->setFont(hf);
        fileLayout->addWidget(srcDestLabel);

        auto* card = new SectionCard(srcDestContainer);
        auto* formLayout = new QFormLayout(card);
        formLayout->setContentsMargins(10, 8, 10, 8);
        formLayout->setVerticalSpacing(4);
        formLayout->setHorizontalSpacing(6);

        // Source row: single file only
        auto* inRow = new QHBoxLayout();
        inRow->setContentsMargins(0, 0, 0, 0);
        inRow->setSpacing(2);
        inputEdit = new oclero::qlementine::LineEdit(card);
        inputEdit->setPlaceholderText(owner.tr("Select or drop .grn / .glb model..."));
        inputEdit->setClearButtonEnabled(true);
        inputEdit->setFixedHeight(24);
        QObject::connect(inputEdit, &oclero::qlementine::LineEdit::textChanged, &owner, [this]() {
            onInputPathChanged();
        });
        inRow->addWidget(inputEdit, 1);

        browseFileBtn = new QPushButton(makeThemedIcon(Icons16::Document_Open), QString(), card);
        browseFileBtn->setToolTip(owner.tr("Browse 3D model file (.grn, .glb)..."));
        browseFileBtn->setFixedSize(24, 24);
        QObject::connect(browseFileBtn, &QPushButton::clicked, &owner, [this]() { browseInput(); });
        inRow->addWidget(browseFileBtn);
        formLayout->addRow(owner.tr("Source:"), inRow);

        // Output row: always folder
        auto* outRow = new QHBoxLayout();
        outRow->setContentsMargins(0, 0, 0, 0);
        outRow->setSpacing(2);
        outputEdit = new oclero::qlementine::LineEdit(card);
        outputEdit->setPlaceholderText(owner.tr("Select target output folder..."));
        outputEdit->setClearButtonEnabled(true);
        outputEdit->setFixedHeight(24);
        outRow->addWidget(outputEdit, 1);

        browseOutBtn = new QPushButton(makeThemedIcon(Icons16::File_FolderOpen), QString(), card);
        browseOutBtn->setToolTip(owner.tr("Browse target destination folder..."));
        browseOutBtn->setFixedSize(24, 24);
        QObject::connect(browseOutBtn, &QPushButton::clicked, &owner, [this]() { browseOutput(); });
        outRow->addWidget(browseOutBtn);
        formLayout->addRow(owner.tr("Output:"), outRow);

        fileLayout->addWidget(card);
    }

    void setupOptionsStack() {
        optionsStack = new QStackedWidget(&owner);

        grnOptions = new GrnOptionsWidget(optionsStack);
        QObject::connect(grnOptions, &GrnOptionsWidget::optionsChanged, &owner, [this]() {
            updateViewerScale();
        });
        QObject::connect(grnOptions, &GrnOptionsWidget::animFilesChanged, &owner, [this]() {
            updateBadges();
        });
        QObject::connect(grnOptions, &GrnOptionsWidget::animationSelected, &owner, [this](const QString& animPath, int internalAnimIndex) {
            onGrnAnimationSelected(animPath, internalAnimIndex);
        });
        optionsStack->addWidget(grnOptions);

        glbOptions = new GlbOptionsWidget(optionsStack);
        QObject::connect(glbOptions, &GlbOptionsWidget::optionsChanged, &owner, [this]() {
            updateViewerScale();
        });
        QObject::connect(glbOptions, &GlbOptionsWidget::animationSelected, &owner, [this](int animIndex) {
            onGlbAnimationSelected(animIndex);
        });
        optionsStack->addWidget(glbOptions);
    }

    void setupConvertButton() {
        convertBtn = new QPushButton(makeThemedIcon(Icons16::Action_Run), owner.tr("Convert Model"), &owner);
        convertBtn->setFixedHeight(36);
        convertBtn->setCursor(Qt::PointingHandCursor);
        QFont f = convertBtn->font();
        f.setBold(true);
        f.setPointSize(10);
        convertBtn->setFont(f);
        convertBtn->setStyleSheet(
            "QPushButton {"
            "  background-color: #0078d7;"
            "  color: #ffffff;"
            "  border: 1px solid #005a9e;"
            "  border-radius: 5px;"
            "  padding: 4px 16px;"
            "  font-weight: 700;"
            "}"
            "QPushButton:hover:!disabled {"
            "  background-color: #1a88e1;"
            "  border-color: #0078d7;"
            "}"
            "QPushButton:pressed:!disabled {"
            "  background-color: #005a9e;"
            "}"
            "QPushButton:disabled {"
            "  background-color: rgba(0, 120, 215, 0.25);"
            "  color: rgba(255, 255, 255, 0.6);"
            "  border-color: transparent;"
            "}"
        );
        QObject::connect(convertBtn, &QPushButton::clicked, &owner, [this]() { executeConversion(); });
    }

    void setupStatusBar() {
        statusBar = new QStatusBar(&owner);
        statusBar->setSizeGripEnabled(false);

        statusLabel = new QLabel(owner.tr("Ready"), statusBar);
        statusLabel->setStyleSheet("color: #888888;");
        statusBar->addWidget(statusLabel, 1);

        spinner = new oclero::qlementine::LoadingSpinner(statusBar);
        spinner->setSpinning(false);
        statusBar->addPermanentWidget(spinner);

        progressBar = new QProgressBar(statusBar);
        progressBar->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        progressBar->setFixedWidth(50);
        progressBar->setFixedHeight(8);
        progressBar->setRange(0, 100);
        progressBar->setValue(0);
        progressBar->setTextVisible(false);
        progressBar->setVisible(false);
        statusBar->addPermanentWidget(progressBar);

        logDrawerBtn = new QToolButton(statusBar);
        logDrawerBtn->setText(owner.tr("Log (0)"));
        logDrawerBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
        logDrawerBtn->setCheckable(true);
        logDrawerBtn->setChecked(false);
        QObject::connect(logDrawerBtn, &QToolButton::toggled, &owner, [this](bool chk) {
            logDrawer->setVisible(chk);
        });
        statusBar->addPermanentWidget(logDrawerBtn);

        previewToggleBtn = new QToolButton(statusBar);
        previewToggleBtn->setIcon(makeThemedIcon(Icons16::Shape_Cube));
        previewToggleBtn->setText(owner.tr("3D Preview"));
        previewToggleBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        previewToggleBtn->setCheckable(true);
        previewToggleBtn->setChecked(true);
        QObject::connect(previewToggleBtn, &QToolButton::toggled, &owner, [this](bool chk) {
            togglePreview(chk);
        });
        statusBar->addPermanentWidget(previewToggleBtn);
    }

    void setupLogDrawer() {
        logDrawer = new LogDrawer(&owner);
        logDrawer->setVisible(false);
        QObject::connect(logDrawer, &LogDrawer::entryCountChanged, &owner, [this](size_t count) {
            if (logDrawerBtn) {
                logDrawerBtn->setText(QString("Log (%1)").arg(count));
            }
        });
        logDrawer->appendLog(owner.tr("GRN Converter ready."));
    }

    void setupLayout() {
        converterPanel = new QWidget(&owner);
        converterPanel->setMinimumWidth(320);
        converterPanel->setMaximumWidth(420);

        sourceInfoWidget = new SourceInfoWidget(&owner);

        auto* convLayout = new QVBoxLayout(converterPanel);
        convLayout->setContentsMargins(8, 4, 8, 4);
        convLayout->setSpacing(6);
        convLayout->addWidget(navBar);
        convLayout->addWidget(oclero::qlementine::makeHorizontalLine(&owner));
        convLayout->addWidget(fileBoxWidget);
        convLayout->addWidget(sourceInfoWidget);
        convLayout->addWidget(optionsStack, 1);
        convLayout->addWidget(convertBtn);
        convLayout->addWidget(logDrawer);
        convLayout->addWidget(statusBar);

        modelViewer = new ModelViewerPanel(&owner);
        modelViewer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        modelViewer->setMinimumWidth(360);
        QObject::connect(modelViewer, &ModelViewerPanel::closeRequested, &owner, [this]() {
            togglePreview(false);
        });
        QObject::connect(modelViewer, &ModelViewerPanel::detachRequested, &owner, [this]() {
            detachPreview();
        });

        targetInfoWidget = new TargetInfoWidget(&owner);
        targetInfoWidget->setMinimumWidth(240);
        targetInfoWidget->setMaximumWidth(360);
        QObject::connect(targetInfoWidget, &TargetInfoWidget::animationSelected, &owner, [this](int animIndex, const QString& clipPath) {
            onTargetAnimationSelected(animIndex, clipPath);
        });

        mainSplitter = new QSplitter(Qt::Horizontal, &owner);
        mainSplitter->setHandleWidth(4);
        mainSplitter->addWidget(converterPanel);
        mainSplitter->addWidget(modelViewer);
        mainSplitter->addWidget(targetInfoWidget);
        mainSplitter->setCollapsible(0, false);
        mainSplitter->setCollapsible(1, false);
        mainSplitter->setCollapsible(2, true);
        mainSplitter->setStretchFactor(0, 0);
        mainSplitter->setStretchFactor(1, 1);
        mainSplitter->setStretchFactor(2, 0);
        mainSplitter->setSizes({ 360, 600, 320 });

        auto* outerLayout = new QVBoxLayout(&owner);
        outerLayout->setContentsMargins(0, 0, 0, 0);
        outerLayout->setSpacing(0);
        outerLayout->setMenuBar(menuBar);
        outerLayout->addWidget(mainSplitter, 1);
    }

    void onInputPathChanged() {
        QString path = inputEdit->text().trimmed();
        if (!path.isEmpty()) {
            QFileInfo fi(path);
            if (fi.isFile()) {
                if (outputEdit->text().isEmpty()) {
                    outputEdit->setText(fi.dir().absolutePath());
                }

                QString ext = fi.suffix().toLower();
                if (ext == "grn" && navBar->currentIndex() != 0) {
                    cachedGrnInputPath = path;
                    previousTabIndex = 0;
                    navBar->blockSignals(true);
                    navBar->setCurrentIndex(0);
                    optionsStack->setCurrentIndex(0);
                    navBar->blockSignals(false);
                } else if ((ext == "glb" || ext == "gltf") && navBar->currentIndex() != 1) {
                    cachedGlbInputPath = path;
                    previousTabIndex = 1;
                    navBar->blockSignals(true);
                    navBar->setCurrentIndex(1);
                    optionsStack->setCurrentIndex(1);
                    navBar->blockSignals(false);
                }
            }
        }
        if (navBar->currentIndex() == 0) {
            cachedGrnInputPath = path;
            cachedGrnOutputPath = outputEdit->text();
        } else {
            cachedGlbInputPath = path;
            cachedGlbOutputPath = outputEdit->text();
        }
        updateActionState();
        updateModelInOptions();
    }

    void updateModelInOptions() {
        QString path = inputEdit->text().trimmed();
        grnOptions->setModel(path);
        glbOptions->setModel(path);
        updateBadges();

        loadedModel.reset();
        cachedExternalAnimModel.reset();
        convertedModel.reset();
        if (targetInfoWidget) {
            targetInfoWidget->clearTarget();
        }
        if (modelViewer) {
            modelViewer->loadTargetModel(nullptr, QString());
        }

        if (!path.isEmpty()) {
            QFileInfo fi(path);
            if (fi.exists() && fi.isFile()) {
                QString ext = fi.suffix().toLower();
                if (ext == "grn") {
                    loadedModel = parse_grn_file(fi.filesystemFilePath());
                } else if (ext == "glb" || ext == "gltf") {
                    GlbImportOptions opt;
                    opt.y_up = true;
                    opt.texture_dir = fi.dir().filesystemAbsolutePath();
                    loadedModel = load_glb_file(fi.filesystemFilePath(), opt);
                }
            }
        }

        if (loadedModel) {
            QFileInfo fi(path);
            modelViewer->loadSourceModel(&*loadedModel, fi.fileName());
            if (sourceInfoWidget) {
                bool isGrn = (navBar->currentIndex() == 0);
                sourceInfoWidget->setSourceModel(&*loadedModel, path, isGrn);
            }
        } else {
            modelViewer->loadSourceModel(nullptr, QString());
            if (sourceInfoWidget) {
                sourceInfoWidget->clearSource();
            }
        }
        updateViewerScale();
    }

    void updateViewerScale() {
        float s = 1.0f;
        if (navBar->currentIndex() == 0) {
            s = grnOptions->scaleMultiplier();
        } else {
            if (glbOptions->isTargetHeightMode()) {
                float th = glbOptions->targetHeight();
                if (th > 0.0f && loadedModel) {
                    float min_z = 1e30f, max_z = -1e30f;
                    for (const auto& m : loadedModel->meshes) {
                        for (const auto& v : m.vertices) {
                            min_z = std::min(min_z, v.z);
                            max_z = std::max(max_z, v.z);
                        }
                    }
                    float h = max_z - min_z;
                    if (h > 1e-4f) {
                        s = th / h;
                    }
                }
            } else {
                s = glbOptions->scaleFactor();
            }
        }
        if (modelViewer) {
            modelViewer->setModelScale(s);
        }
    }

    void togglePreview(bool show) {
        if (isPreviewVisible == show) return;
        isPreviewVisible = show;

        if (previewAction) previewAction->setChecked(show);
        if (previewToggleBtn) {
            previewToggleBtn->blockSignals(true);
            previewToggleBtn->setChecked(show);
            previewToggleBtn->blockSignals(false);
        }

        if (show) {
            owner.setMinimumSize(1000, 600);
            owner.setMaximumWidth(QWIDGETSIZE_MAX);
            converterPanel->setMaximumWidth(420);
            modelViewer->setVisible(true);
            targetInfoWidget->setVisible(true);
            mainSplitter->setSizes({ 360, 600, 320 });
            owner.resize(1280, owner.height());
            if (loadedModel) {
                QFileInfo fi(inputEdit->text());
                modelViewer->loadSourceModel(&*loadedModel, fi.fileName());
            }
            if (convertedModel) {
                QFileInfo fi(lastOutputPath);
                modelViewer->loadTargetModel(&*convertedModel, fi.fileName());
            }
        } else {
            modelViewer->setVisible(false);
            targetInfoWidget->setVisible(false);
            converterPanel->setMaximumWidth(QWIDGETSIZE_MAX);
            owner.setMinimumSize(360, 600);
            owner.resize(400, owner.height());
        }
    }

    void detachPreview() {
        if (!detachedDialog) {
            detachedDialog = new QDialog(&owner);
            detachedDialog->setWindowTitle(owner.tr("3D Model Preview"));
            detachedDialog->resize(800, 680);

            auto* dlgLayout = new QVBoxLayout(detachedDialog);
            dlgLayout->setContentsMargins(0, 0, 0, 0);
            dlgLayout->addWidget(modelViewer);
            modelViewer->setVisible(true);

            QObject::connect(detachedDialog, &QDialog::finished, &owner, [this](int) {
                mainSplitter->insertWidget(1, modelViewer);
                modelViewer->setVisible(isPreviewVisible);
            });
            detachedDialog->show();
        } else {
            detachedDialog->show();
            detachedDialog->raise();
            detachedDialog->activateWindow();
        }
    }

    void onGrnAnimationSelected(const QString& animPath, int internalAnimIndex) {
        if (internalAnimIndex >= 0 && loadedModel && static_cast<size_t>(internalAnimIndex) < loadedModel->animations.size()) {
            const auto& a = loadedModel->animations[internalAnimIndex];
            modelViewer->playSourceAnimation(&a, QString::fromStdString(a.name));

            if (convertedModel) {
                bool found = false;
                for (const auto& ta : convertedModel->animations) {
                    if (ta.name == a.name) {
                        modelViewer->playTargetAnimation(&ta, QString::fromStdString(ta.name));
                        found = true;
                        break;
                    }
                }
                if (!found && static_cast<size_t>(internalAnimIndex) < convertedModel->animations.size()) {
                    modelViewer->playTargetAnimation(&convertedModel->animations[internalAnimIndex], QString());
                }
            }
            return;
        }

        if (!animPath.isEmpty()) {
            cachedExternalAnimModel = parse_grn_file(std::filesystem::path(animPath.toStdWString()));
            if (cachedExternalAnimModel && !cachedExternalAnimModel->animations.empty()) {
                const auto& a = cachedExternalAnimModel->animations[0];
                modelViewer->playSourceAnimation(&a, QFileInfo(animPath).fileName());

                if (convertedModel) {
                    for (const auto& ta : convertedModel->animations) {
                        if (ta.name == a.name) {
                            modelViewer->playTargetAnimation(&ta, QString::fromStdString(ta.name));
                            break;
                        }
                    }
                }
                return;
            }
        }

        modelViewer->stopAnimation();
    }

    void onGlbAnimationSelected(int animIndex) {
        if (animIndex >= 0 && loadedModel && static_cast<size_t>(animIndex) < loadedModel->animations.size()) {
            const auto& a = loadedModel->animations[animIndex];
            modelViewer->playSourceAnimation(&a, QString::fromStdString(a.name));

            if (convertedModel) {
                bool found = false;
                for (const auto& ta : convertedModel->animations) {
                    if (ta.name == a.name) {
                        modelViewer->playTargetAnimation(&ta, QString::fromStdString(ta.name));
                        found = true;
                        break;
                    }
                }
                if (!found && static_cast<size_t>(animIndex) < convertedModel->animations.size()) {
                    modelViewer->playTargetAnimation(&convertedModel->animations[animIndex], QString());
                }
            }
        } else {
            modelViewer->stopAnimation();
        }
    }

    void onTargetAnimationSelected(int animIndex, const QString& clipPath) {
        if (animIndex >= 0 && convertedModel && static_cast<size_t>(animIndex) < convertedModel->animations.size()) {
            const auto& a = convertedModel->animations[animIndex];
            modelViewer->playTargetAnimation(&a, QString::fromStdString(a.name));

            if (loadedModel) {
                bool found = false;
                for (const auto& sa : loadedModel->animations) {
                    if (sa.name == a.name) {
                        modelViewer->playSourceAnimation(&sa, QString::fromStdString(sa.name));
                        found = true;
                        break;
                    }
                }
                if (!found && static_cast<size_t>(animIndex) < loadedModel->animations.size()) {
                    modelViewer->playSourceAnimation(&loadedModel->animations[animIndex], QString());
                }
            }
        } else if (!clipPath.isEmpty()) {
            auto clipModel = parse_grn_file(std::filesystem::path(clipPath.toStdWString()));
            if (clipModel && !clipModel->animations.empty()) {
                const auto& a = clipModel->animations[0];
                modelViewer->playTargetAnimation(&a, QFileInfo(clipPath).fileName());

                if (loadedModel) {
                    for (const auto& sa : loadedModel->animations) {
                        if (sa.name == a.name) {
                            modelViewer->playSourceAnimation(&sa, QString::fromStdString(sa.name));
                            break;
                        }
                    }
                }
            }
        } else {
            modelViewer->stopAnimation();
        }
    }

    void updateBadges() {
        if (navBar->currentIndex() == 0) {
            auto anims = grnOptions->externalAnimFiles();
            navBar->setItemBadge(0, anims.empty() ? QString() : QString::number(anims.size()));
            navBar->setItemBadge(1, QString());
        } else {
            navBar->setItemBadge(0, QString());
        }
    }

    void updateActionState() {
        QString inPath = inputEdit->text().trimmed();
        bool hasInput = !inPath.isEmpty();
        bool running = worker && worker->isRunning();

        convertBtn->setEnabled(hasInput && !running);
        inputEdit->setEnabled(!running);
        outputEdit->setEnabled(!running);
        navBar->setEnabled(!running);
        grnOptions->setEnabled(!running);
        glbOptions->setEnabled(!running);

        if (running) {
            convertBtn->setText(owner.tr("Converting..."));
            spinner->setSpinning(true);
        } else {
            convertBtn->setText(owner.tr("Convert Model"));
            spinner->setSpinning(false);
        }
    }

    void browseInput() {
        bool toGlb = (navBar->currentIndex() == 0);
        QString filter = toGlb
            ? owner.tr("Granny 1.2b (*.grn);;glTF Binary (*.glb *.gltf);;All Files (*.*)")
            : owner.tr("glTF Binary (*.glb *.gltf);;Granny 1.2b (*.grn);;All Files (*.*)");
        QString file = QFileDialog::getOpenFileName(&owner, owner.tr("Select 3D Model File"), inputEdit->text(), filter);
        if (!file.isEmpty()) {
            inputEdit->setText(file);
        }
    }

    void browseOutput() {
        QString dir = QFileDialog::getExistingDirectory(&owner, owner.tr("Select Target Output Folder"), outputEdit->text());
        if (!dir.isEmpty()) {
            outputEdit->setText(dir);
        }
    }

    void resetOptions() {
        grnOptions->reset();
        glbOptions->reset();
        updateModelInOptions();
    }

    void executeConversion() {
        QString inPath = inputEdit->text().trimmed();
        if (inPath.isEmpty()) return;

        QFileInfo fi(inPath);
        QString outDir = outputEdit->text().trimmed();
        if (outDir.isEmpty()) {
            outDir = fi.dir().absolutePath();
            outputEdit->setText(outDir);
        }

        bool isGrnToGlb = (navBar->currentIndex() == 0);
        QString outFileName = fi.completeBaseName() + (isGrnToGlb ? ".glb" : ".grn");
        QString outPath = QDir(outDir).filePath(outFileName);
        lastOutputPath = outPath;

        if (targetInfoWidget) {
            targetInfoWidget->setConverting(true);
        }

        ConversionOptions opts;
        if (isGrnToGlb) {
            opts.y_up = grnOptions->convertCoordinates();
            opts.embed_textures = grnOptions->embedTextures();
            opts.texture_format = grnOptions->looseTextureFormat();
            opts.vtex_enabled = grnOptions->decompressVTex();
            opts.scale = grnOptions->scaleMultiplier();
            if (grnOptions->embedAnimations()) {
                opts.anim_files = grnOptions->externalAnimFiles();
            }
        } else {
            opts.y_up = glbOptions->convertCoordinates();
            if (glbOptions->isTargetHeightMode()) {
                opts.target_height = glbOptions->targetHeight();
            } else {
                opts.scale = glbOptions->scaleFactor();
            }
            opts.vtex_enabled = glbOptions->compressVTex();
            opts.split_animations = glbOptions->splitAnimations();
            opts.auto_split_16bit = glbOptions->autoSplit16Bit();
        }

        progressBar->setValue(0);
        progressBar->setVisible(true);
        statusLabel->setText(owner.tr("Converting..."));
        updateActionState();

        worker->setJob(false, inPath.toStdString(), outPath.toStdString(), opts);
        worker->start();
    }

    void onProgress(float p) {
        progressBar->setValue(static_cast<int>(p * 100.0f));
    }

    void onLog(const QString& msg, int lvl) {
        logDrawer->appendLog(msg, lvl);
    }

    void onFinished(bool ok, const QString& summary) {
        updateActionState();
        statusLabel->setText(summary);
        progressBar->setValue(ok ? 100 : 0);
        progressBar->setVisible(false);

        if (ok && !lastOutputPath.isEmpty()) {
            QFileInfo fi(lastOutputPath);
            if (fi.exists()) {
                bool isGrnToGlb = (navBar->currentIndex() == 0);
                if (isGrnToGlb) {
                    GlbImportOptions imp_opt;
                    imp_opt.y_up = true;
                    imp_opt.texture_dir = fi.dir().filesystemAbsolutePath();
                    convertedModel = load_glb_file(fi.filesystemFilePath(), imp_opt);
                } else {
                    convertedModel = parse_grn_file(fi.filesystemFilePath());
                }

                if (convertedModel) {
                    modelViewer->loadTargetModel(&*convertedModel, fi.fileName());
                    targetInfoWidget->setTargetModel(&*convertedModel, lastOutputPath, isGrnToGlb);
                } else {
                    targetInfoWidget->setTargetModel(nullptr, lastOutputPath, isGrnToGlb);
                }
            }
        } else if (!ok) {
            targetInfoWidget->clearTarget();
        }
    }
};

MainWindow::MainWindow(oclero::qlementine::ThemeManager* themeManager, QWidget* parent)
    : QWidget(parent)
    , _impl(std::make_unique<Impl>(*this, themeManager)) {
    setWindowTitle(tr("GRN <-> GLB Converter"));
    setMinimumSize(1000, 600);
    resize(1280, 800);
    setAcceptDrops(true);
    _impl->setupUI();
}

MainWindow::~MainWindow() = default;

void MainWindow::openPath(const QString& path) {
    _impl->inputEdit->setText(path);
}

void MainWindow::executeConversion() {
    _impl->executeConversion();
}

bool MainWindow::isConverting() const {
    return _impl->worker && _impl->worker->isRunning();
}

void MainWindow::setSplitAnims(bool enabled) {
    _impl->glbOptions->setSplitAnimations(enabled);
}

void MainWindow::setEmbedAnims(bool enabled) {
    _impl->grnOptions->setEmbedAnimations(enabled);
}

void MainWindow::setEmbedTextures(bool enabled) {
    _impl->grnOptions->setEmbedTextures(enabled);
}

void MainWindow::setActiveTab(int index) {
    _impl->navBar->setCurrentIndex(index);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dragMoveEvent(QDragMoveEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    const auto urls = event->mimeData()->urls();
    if (!urls.isEmpty()) {
        QString localPath = urls.first().toLocalFile();
        if (!localPath.isEmpty()) {
            openPath(localPath);
            event->acceptProposedAction();
        }
    }
}

void MainWindow::setPreviewVisible(bool visible) {
    _impl->togglePreview(visible);
}

bool MainWindow::isPreviewVisible() const {
    return _impl->isPreviewVisible;
}

void MainWindow::addExternalAnimation(const QString& path) {
    _impl->grnOptions->addExternalAnimFile(path);
    _impl->updateBadges();
}

void MainWindow::selectAnimationItem(int index) {
    _impl->grnOptions->selectAnimationItem(index);
}

} // namespace grn
