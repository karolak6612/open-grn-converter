#pragma once

#include <QWidget>
#include <QString>
#include <memory>
#include <string>
#include <vector>
#include <filesystem>

namespace grn {

class GrnOptionsWidget : public QWidget {
    Q_OBJECT
public:
    explicit GrnOptionsWidget(QWidget* parent = nullptr);
    ~GrnOptionsWidget() override;

    void setModel(const QString& modelPath);

    bool convertCoordinates() const;
    bool embedTextures() const;
    std::string looseTextureFormat() const;
    bool decompressVTex() const;
    float scaleMultiplier() const;
    bool embedAnimations() const;

    std::vector<std::filesystem::path> externalAnimFiles() const;
    void addExternalAnimFile(const QString& path);
    void clearExternalAnims();
    void selectAnimationItem(int index);

    void setConvertCoordinates(bool convert);
    void setEmbedTextures(bool embed);
    void setEmbedAnimations(bool enabled);
    void reset();

signals:
    void optionsChanged();
    void animFilesChanged();
    void animationSelected(const QString& animPath, int internalAnimIndex);

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace grn
