#pragma once

#include <QWidget>
#include <QString>
#include <memory>

namespace grn {

class LogDrawer : public QWidget {
    Q_OBJECT
public:
    explicit LogDrawer(QWidget* parent = nullptr);
    ~LogDrawer() override;

    void appendLog(const QString& text, int level = 0);
    void clearLog();
    QString allText() const;
    size_t entryCount() const;

signals:
    void entryCountChanged(size_t count);

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace grn
