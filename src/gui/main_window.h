#pragma once

#include <QWidget>
#include <memory>

namespace oclero::qlementine {
class ThemeManager;
}

namespace grn {

class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(oclero::qlementine::ThemeManager* themeManager = nullptr, QWidget* parent = nullptr);
    ~MainWindow() override;

    void openPath(const QString& path);
    void executeConversion();
    bool isConverting() const;

    // Test & CLI automation setters
    void setSplitAnims(bool enabled);
    void setEmbedAnims(bool enabled);
    void setEmbedTextures(bool enabled);
    void setActiveTab(int index);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace grn
