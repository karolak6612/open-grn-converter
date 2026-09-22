#include "conversion_worker.h"

namespace grn {

ConversionWorker::ConversionWorker(QObject* parent)
    : QThread(parent) {}

void ConversionWorker::setJob(bool is_batch,
                              const std::filesystem::path& input,
                              const std::filesystem::path& output,
                              const ConversionOptions& opts) {
    is_batch_ = is_batch;
    input_path_ = input;
    output_path_ = output;
    options_ = opts;
}

void ConversionWorker::run() {
    emit progressUpdated(0.0f);
    emit logMessage("--- Starting Conversion Task ---", 0);

    auto cb = [this](const std::string& item, float prog, bool ok, const std::string& msg) {
        (void)item;
        emit progressUpdated(prog);
        int lvl = ok ? (prog >= 1.0f ? 1 : 0) : 3; // 0=Info, 1=Success, 2=Warning, 3=Error
        emit logMessage(QString::fromStdString(msg), lvl);
    };

    if (is_batch_ || std::filesystem::is_directory(input_path_)) {
        auto res = convert_directory(input_path_, output_path_, options_, cb);
        bool all_ok = (res.files_failed == 0 && res.files_succeeded > 0);
        QString summary = QString("Batch complete. Succeeded: %1, Failed: %2")
                              .arg(res.files_succeeded)
                              .arg(res.files_failed);
        emit conversionFinished(all_ok, summary);
    } else {
        bool ok = convert_file(input_path_, output_path_, options_, cb);
        emit progressUpdated(ok ? 1.0f : 0.0f);
        emit conversionFinished(ok, ok ? "Conversion completed successfully." : "Conversion failed.");
    }
}

} // namespace grn
