#pragma once
#include "ChromeDialog.hpp"
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QStandardPaths>
#include <QSpinBox>
#include <functional>
#include "Theme.hpp"

// 只收集输入和显示状态；下载与任务生命周期由 controller 持有，关闭窗口不取消任务。
class DownloadToolDialog final : public ChromeDialog {
public:
    explicit DownloadToolDialog(QWidget *parent = nullptr) : ChromeDialog(parent) {
        setObjectName(QStringLiteral("downloadToolDialog"));
        setWindowTitle(QStringLiteral("高速下载"));
        resize(600, 440);
        auto *layout = new QVBoxLayout(body());
        layout->setContentsMargins(20, 18, 20, 18);
        layout->setSpacing(12);
        auto *hint = new QLabel(QStringLiteral("下载 HTTP/HTTPS 直链，进度显示在任务中心。关闭窗口后任务继续。"), this);
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
        // 进度：一根细圆角条，百分比摆在右端（不在条里面贴左）。
        auto *progressRow = new QHBoxLayout;
        progressRow->setSpacing(10);
        progress = new QProgressBar(this); progress->setRange(0, 100); progress->setValue(0);
        progress->setObjectName(QStringLiteral("downloadToolProgress"));
        progress->setTextVisible(false);
        progress->setFixedHeight(7);
        const auto paintBar = [this] {
            const Theme::Tokens tokens = Theme::tokens();
            QColor track = tokens.text;  // 轨道：正文色压淡（token 里那份 track 在深色磨砂上几乎看不见）
            track.setAlphaF(0.14);
            progress->setStyleSheet(QStringLiteral("QProgressBar { background-color: %1; border: none; border-radius: 3px; }"
                                                   "QProgressBar::chunk { background-color: %2; border-radius: 3px; }")
                                        .arg(track.name(QColor::HexArgb), tokens.primary.name(QColor::HexArgb)));
        };
        paintBar();
        Theme::follow(progress, paintBar);
        percent = new QLabel(QStringLiteral("0%"), this);
        percent->setObjectName(QStringLiteral("downloadToolPercent"));
        percent->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        percent->setMinimumWidth(percent->fontMetrics().horizontalAdvance(QStringLiteral("100%")));
        progressRow->addWidget(progress, 1, Qt::AlignVCenter);
        progressRow->addWidget(percent);
        layout->addLayout(progressRow);
        status = new QLabel(QStringLiteral("源站支持时自动分段，可跨重启续传。"), this);
        status->setTextFormat(Qt::PlainText); status->setWordWrap(true);
        status->setObjectName(QStringLiteral("downloadToolStatus"));
        layout->addWidget(status);
        layout->addSpacing(8);

        // 缓存：四个次要操作收进「…」菜单，和小标题、上限放一行成一组，跟底下的主操作分开层次。
        const QByteArray role = Theme::textRolePropertyName().toUtf8();
        auto *cacheHeader = new QHBoxLayout;
        cacheHeader->setSpacing(8);
        auto *cacheTitle = new QLabel(QStringLiteral("缓存"), this);
        QFont titleFont = Theme::textFont(cacheTitle->font(), Theme::TextRole::Body);
        if (const QString kai = Theme::handFamily(Theme::HandFont::Kai); !kai.isEmpty()) titleFont.setFamily(kai);
        titleFont.setPixelSize(16);
        cacheTitle->setFont(titleFont);
        cacheHeader->addWidget(cacheTitle);
        cacheHeader->addStretch(1);
        auto *limitLabel = new QLabel(QStringLiteral("闲置上限"), this);
        limitLabel->setProperty(role.constData(), Theme::secondaryTextRoleValue());
        cacheHeader->addWidget(limitLabel);
        cacheLimit = new QSpinBox(this); cacheLimit->setObjectName(QStringLiteral("downloadCacheLimit"));
        cacheLimit->setRange(128, 65536); cacheLimit->setValue(2048); cacheLimit->setSuffix(QStringLiteral(" MiB"));
        cacheLimit->setToolTip(QStringLiteral("按最旧优先整理闲置缓存；正在进行的下载不受影响。过期指 30 天未更新。"));
        cacheHeader->addWidget(cacheLimit);
        cacheMenu = new QPushButton(QStringLiteral("…"), this);
        cacheMenu->setObjectName(QStringLiteral("downloadCacheMenu"));
        cacheMenu->setToolTip(QStringLiteral("缓存操作"));
        cacheMenu->setFixedWidth(36);
        Theme::markButton(cacheMenu, Theme::ButtonKind::Neutral);
        auto *menu = new QMenu(cacheMenu);
        const QStringList actions{QStringLiteral("统计占用"), QStringLiteral("按上限整理"), QStringLiteral("清理过期"),
                                  QStringLiteral("清空闲置缓存…")};
        for (int action = 0; action < actions.size(); ++action) {
            if (action == 3) menu->addSeparator();
            QAction *item = menu->addAction(actions[action]);
            item->setObjectName(QStringLiteral("downloadCacheAction%1").arg(action));
            connect(item, &QAction::triggered, this, [this, action] { if (onCache) onCache(action); });
        }
        connect(cacheMenu, &QPushButton::clicked, this, [this, menu] {
            menu->popup(cacheMenu->mapToGlobal(QPoint(cacheMenu->width() - menu->sizeHint().width(), cacheMenu->height() + 4)));
        });
        cacheHeader->addWidget(cacheMenu);
        layout->addLayout(cacheHeader);
        cacheStatus = new QLabel(QStringLiteral("尚未统计（仅分段续传缓存）"), this);
        cacheStatus->setObjectName(QStringLiteral("downloadCacheStatus"));
        cacheStatus->setTextFormat(Qt::PlainText); cacheStatus->setWordWrap(true);
        cacheStatus->setProperty(role.constData(), Theme::secondaryTextRoleValue());
        layout->addWidget(cacheStatus);
        layout->addStretch(1);

        auto *buttons = new QHBoxLayout;
        buttons->addStretch();
        stop = new QPushButton(QStringLiteral("取消下载"), this);
        start = new QPushButton(QStringLiteral("开始下载"), this);
        start->setObjectName(QStringLiteral("downloadToolStart"));
        stop->setObjectName(QStringLiteral("downloadToolCancel"));
        Theme::markButton(start, Theme::ButtonKind::Primary);
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
        if (total <= 0) { progress->setRange(0, 0); percent->setText(QStringLiteral("…")); }
        else {
            progress->setRange(0, 1000); progress->setValue(int(qBound(0.0, double(done) * 1000 / double(total), 1000.0)));
            percent->setText(QStringLiteral("%1%").arg(progress->value() / 10));
        }
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
    QLabel *percent = nullptr;
    QPushButton *cacheMenu = nullptr;
};
