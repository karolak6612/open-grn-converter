#include "log_drawer.h"

#include <oclero/qlementine/utils/WidgetUtils.hpp>

#include <QVBoxLayout>
#include <QPlainTextEdit>
#include <QFontDatabase>
#include <QDateTime>
#include <QScrollBar>

namespace grn {

struct LogDrawer::Impl {
    LogDrawer& owner;
    QPlainTextEdit* logConsole{ nullptr };
    size_t logEntryCount{ 0 };

    explicit Impl(LogDrawer& o) : owner(o) {
        setupUI();
    }

    void setupUI() {
        owner.setFixedHeight(90);

        auto* layout = new QVBoxLayout(&owner);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        layout->addWidget(oclero::qlementine::makeHorizontalLine(&owner));

        logConsole = new QPlainTextEdit(&owner);
        logConsole->setReadOnly(true);
        logConsole->setLineWrapMode(QPlainTextEdit::NoWrap);
        logConsole->setMaximumBlockCount(1000);

        QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        mono.setPointSize(9);
        logConsole->setFont(mono);
        logConsole->setFrameShape(QFrame::NoFrame);

        layout->addWidget(logConsole, 1);
    }

    void appendLog(const QString& text, int level) {
        QString time = QDateTime::currentDateTime().toString("hh:mm:ss");
        QString prefix = "[INFO]";
        if (level == 1) prefix = "[WARN]";
        else if (level >= 2) prefix = "[ERR ]";

        QString formatted = QString("%1 %2 %3").arg(time, prefix, text);
        logConsole->appendPlainText(formatted);
        logEntryCount++;
        emit owner.entryCountChanged(logEntryCount);

        auto* sb = logConsole->verticalScrollBar();
        if (sb) sb->setValue(sb->maximum());
    }

    void clearLog() {
        logConsole->clear();
        logEntryCount = 0;
        emit owner.entryCountChanged(0);
    }
};

LogDrawer::LogDrawer(QWidget* parent)
    : QWidget(parent)
    , _impl(std::make_unique<Impl>(*this)) {}

LogDrawer::~LogDrawer() = default;

void LogDrawer::appendLog(const QString& text, int level) {
    _impl->appendLog(text, level);
}

void LogDrawer::clearLog() {
    _impl->clearLog();
}

QString LogDrawer::allText() const {
    return _impl->logConsole->toPlainText();
}

size_t LogDrawer::entryCount() const {
    return _impl->logEntryCount;
}

} // namespace grn
