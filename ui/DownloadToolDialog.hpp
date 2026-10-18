#pragma once
#include <QDialog>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QStandardPaths>
#include <QSpinBox>
#include <functional>
#include "Theme.hpp"

// 只收集输入和显示状态；下载与任务生命周期由 controller 持有，关闭窗口不取消任务。
class DownloadToolDialog final : public QDialog {
public:
    explicit DownloadToolDialog(QWidget *parent = nullptr) : QDialog(parent) {
        setObjectName(QStringLiteral("downloadToolDialog"));
        setWindowTitle(QStringLiteral("高速下载"));
        resize(600, 330);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(20, 18, 20, 18);
        layout->setSpacing(12);
        auto *hint = new QLabel(QStringLiteral("下载 HTTP/HTTPS 文件直链，进度同步到任务中心。支持失败重试；速度取决于网络和源站。关闭此窗口后任务继续。"), this);
        hint->setWordWrap(true);
        layout->addWidget(hint);
        auto *form = new QFormLayout;
        url = new QLineEdit(this); url->setObjectName(QStringLiteral("downloadToolUrl"));
        url->setPlaceholderText(QStringLiteral("https://example.com/file.zip"));
        form->addRow(QStringLiteral("下载地址"), url);
        auto *directoryRow = new QWidget(this);
        auto *row = new QHBoxLayout(directoryRow); row->setContentsMargins(0, 0, 0, 0);
        directory = new QLineEdit(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation), directoryRow);
        directory->setObjectName(QStringLiteral("downloadToolDirectory"));
        browse = new QPushButton(QStringLiteral("浏览"), directoryRow);
        Theme::markButton(browse, Theme::ButtonKind::Neutral);
        row->addWidget(directory, 1); row->addWidget(browse);
        connect(browse, &QPushButton::clicked, this, [this] {
            const QString picked = QFileDialog::getExistingDirectory(this, QStringLiteral("保存目录"), directory->text());
            if (!picked.isEmpty()) directory->setText(picked);
        });
        form->addRow(QStringLiteral("保存目录"), directoryRow);
        name = new QLineEdit(this); name->setObjectName(QStringLiteral("downloadToolFileName"));
        name->setPlaceholderText(QStringLiteral("可留空，使用链接中的文件名"));
        form->addRow(QStringLiteral("文件名"), name);
        layout->addLayout(form);
        progress = new QProgressBar(this); progress->setRange(0, 100); progress->setValue(0);
        progress->setObjectName(QStringLiteral("downloadToolProgress"));
        layout->addWidget(progress);
        status = new QLabel(QStringLiteral("大文件在源站支持时自动分段；完整分段可跨重启续传，不支持时退回普通下载。"), this);
        status->setTextFormat(Qt::PlainText); status->setWordWrap(true);
        status->setObjectName(QStringLiteral("downloadToolStatus"));
        layout->addWidget(status);
        auto *cacheRow = new QHBoxLayout;
        cacheStatus = new QLabel(QStringLiteral("缓存：尚未统计（仅分段续传缓存）"), this);
        cacheStatus->setObjectName(QStringLiteral("downloadCacheStatus"));
        cacheStatus->setTextFormat(Qt::PlainText); cacheStatus->setWordWrap(true);
        layout->addWidget(cacheStatus);
        cacheLimit = new QSpinBox(this); cacheLimit->setObjectName(QStringLiteral("downloadCacheLimit"));
        cacheLimit->setRange(128, 65536); cacheLimit->setValue(2048); cacheLimit->setSuffix(QStringLiteral(" MiB"));
        cacheRow->addWidget(new QLabel(QStringLiteral("闲置缓存上限"), this)); cacheRow->addWidget(cacheLimit);
        cacheRow->addStretch(); layout->addLayout(cacheRow);
        auto *cacheButtons = new QHBoxLayout;
        for (int action = 0; action < 4; ++action) {
            auto *button = new QPushButton(QStringList{QStringLiteral("统计"), QStringLiteral("按上限整理"), QStringLiteral("清理过期"), QStringLiteral("清空闲置")}[action], this);
            button->setObjectName(QStringLiteral("downloadCacheAction%1").arg(action));
            Theme::markButton(button, Theme::ButtonKind::Ghost);
            cacheButtons->addWidget(button);
            connect(button, &QPushButton::clicked, this, [this, action] { if (onCache) onCache(action); });
        }
        layout->addLayout(cacheButtons);
        cacheLimit->setToolTip(QStringLiteral("新下载开始时按最旧优先整理闲置缓存；活动下载不会被清理，使用中可暂时超过上限。过期指 30 天未更新。"));
        auto *buttons = new QHBoxLayout;
        buttons->addStretch();
        stop = new QPushButton(QStringLiteral("取消下载"), this);
        start = new QPushButton(QStringLiteral("开始下载"), this);
        start->setObjectName(QStringLiteral("downloadToolStart"));
        stop->setObjectName(QStringLiteral("downloadToolCancel"));
        Theme::markButton(start, Theme::ButtonKind::Accent);
        Theme::markButton(stop, Theme::ButtonKind::Neutral);
        buttons->addWidget(stop); buttons->addWidget(start);
        layout->addLayout(buttons);
        connect(start, &QPushButton::clicked, this, [this] { if (onStart) onStart(); });
        connect(stop, &QPushButton::clicked, this, [this] { if (onCancel) onCancel(); });
        setBusy(false);
    }
    void setBusy(bool busy) {
        start->setEnabled(true); start->setText(busy ? QStringLiteral("加入队列") : QStringLiteral("开始下载"));
        stop->setEnabled(busy);
        browse->setEnabled(true);
        for (auto *field : {url, directory, name}) field->setEnabled(true);
    }
    void showProgress(qint64 done, qint64 total, const QString &text) {
        if (total <= 0) progress->setRange(0, 0);
        else { progress->setRange(0, 1000); progress->setValue(int(qBound(0.0, double(done) * 1000 / double(total), 1000.0))); }
        status->setText(text);
    }
    std::function<void()> onStart, onCancel;
    std::function<void(int)> onCache;
    QSpinBox *cacheLimit = nullptr;
    QLabel *cacheStatus = nullptr;
    QLineEdit *url = nullptr, *directory = nullptr, *name = nullptr;
    QPushButton *start = nullptr, *stop = nullptr, *browse = nullptr;
    QLabel *status = nullptr;
    QProgressBar *progress = nullptr;
};
