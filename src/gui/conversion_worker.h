#pragma once

#include <QThread>
#include <QString>
#include <filesystem>
#include "../converter/options.h"
#include "../converter/converter.h"

namespace grn {

class ConversionWorker : public QThread {
    Q_OBJECT
public:
    explicit ConversionWorker(QObject* parent = nullptr);
    ~ConversionWorker() override = default;

    void setJob(bool is_batch,
                const std::filesystem::path& input,
                const std::filesystem::path& output,
                const ConversionOptions& opts);

signals:
    void progressUpdated(float progress);
    void logMessage(const QString& text, int level);
    void conversionFinished(bool success, const QString& summary);

protected:
    void run() override;

private:
    bool is_batch_ = false;
    std::filesystem::path input_path_;
    std::filesystem::path output_path_;
    ConversionOptions options_;
};

} // namespace grn
