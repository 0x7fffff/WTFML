#pragma once
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QWidget>
#include <functional>
#include "Theme.hpp"

/// 工具箱里「高速下载」那一页（原来的独立窗口并进了工具箱）。
/// 只收集输入和显示状态；下载与任务生命周期由 `DirectDownloadController` 持有，切走页面不取消任务。
/// 内容宽 < 640 时「保存目录」与「文件名」上下叠放。
class DownloadToolPanel final : public QWidget {
public:
    explicit DownloadToolPanel(QWidget *parent = nullptr) : QWidget(parent) {
        setObjectName(QStringLiteral("downloadToolPanel"));
        const QByteArray role = Theme::textRolePropertyName().toUtf8();
        const auto caption = [this, &role](const QString &text) {
            auto *label = new QLabel(text, this);
            label->setProperty(role.constData(), Theme::secondaryTextRoleValue());
            return label;
        };
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);

        layout->addWidget(caption(QStringLiteral("下载地址")));
        url = new QLineEdit(this);
        url->setObjectName(QStringLiteral("downloadToolUrl"));
        url->setPlaceholderText(QStringLiteral("https://"));
        layout->addWidget(url);
        layout->addSpacing(8);

        // 保存目录（含浏览）与文件名：宽时并排，窄时上下叠放。
        auto *directoryRow = new QWidget(this);
        auto *row = new QHBoxLayout(directoryRow);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(8);
        directory = new QLineEdit(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation), directoryRow);
        directory->setObjectName(QStringLiteral("downloadToolDirectory"));
        browse = new QPushButton(QStringLiteral("浏览"), directoryRow);
        browse->setObjectName(QStringLiteral("downloadToolBrowse"));
        Theme::markButton(browse, Theme::ButtonKind::Neutral);
        row->addWidget(directory, 1);
        row->addWidget(browse);
        connect(browse, &QPushButton::clicked, this, [this] {
            const QString picked = QFileDialog::getExistingDirectory(this, QStringLiteral("保存目录"), directory->text());
            if (!picked.isEmpty()) directory->setText(picked);
        });
        name = new QLineEdit(this);
        name->setObjectName(QStringLiteral("downloadToolFileName"));
        name->setPlaceholderText(QStringLiteral("留空使用链接中的文件名"));
        m_pathGrid = new QGridLayout;
        m_pathGrid->setContentsMargins(0, 0, 0, 0);
        m_pathGrid->setHorizontalSpacing(8);
        m_pathGrid->setVerticalSpacing(6);
        m_directoryCaption = caption(QStringLiteral("保存目录"));
        m_nameCaption = caption(QStringLiteral("文件名"));
        m_directoryRow = directoryRow;
        layout->addLayout(m_pathGrid);
        arrangePathRow(false);
        layout->addSpacing(8);

        auto *buttons = new QHBoxLayout;
        buttons->setSpacing(8);
        buttons->addStretch();
        stop = new QPushButton(QStringLiteral("取消下载"), this);
        start = new QPushButton(QStringLiteral("开始下载"), this);
        start->setObjectName(QStringLiteral("downloadToolStart"));
        stop->setObjectName(QStringLiteral("downloadToolCancel"));
        Theme::markButton(start, Theme::ButtonKind::Primary);
        Theme::markButton(stop, Theme::ButtonKind::Neutral);
        buttons->addWidget(stop);
        buttons->addWidget(start);
        layout->addLayout(buttons);
        connect(start, &QPushButton::clicked, this, [this] { if (onStart) onStart(); });
        connect(stop, &QPushButton::clicked, this, [this] { if (onCancel) onCancel(); });

        // 进度：一根细圆角条，百分比摆在右端；没有任务时整行和状态句都不画。
        progressBox = new QWidget(this);
        auto *progressColumn = new QVBoxLayout(progressBox);
        progressColumn->setContentsMargins(0, 8, 0, 0);
        progressColumn->setSpacing(6);
        auto *progressRow = new QHBoxLayout;
        progressRow->setSpacing(10);
        progress = new QProgressBar(progressBox);
        progress->setRange(0, 100);
        progress->setValue(0);
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
        percent = new QLabel(QStringLiteral("0%"), progressBox);
        percent->setObjectName(QStringLiteral("downloadToolPercent"));
        percent->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        percent->setMinimumWidth(percent->fontMetrics().horizontalAdvance(QStringLiteral("100%")));
        progressRow->addWidget(progress, 1, Qt::AlignVCenter);
        progressRow->addWidget(percent);
        progressColumn->addLayout(progressRow);
        status = new QLabel(progressBox);
        status->setTextFormat(Qt::PlainText);
        status->setWordWrap(true);
        status->setObjectName(QStringLiteral("downloadToolStatus"));
        status->setProperty(role.constData(), Theme::secondaryTextRoleValue());
        progressColumn->addWidget(status);
        layout->addWidget(progressBox);
        progressBox->hide();
        layout->addSpacing(18);

        // 缓存：小标题 + 闲置用量；一行上限与三个整理动作；一句说明。
        auto *cacheHeader = new QHBoxLayout;
        cacheHeader->setSpacing(12);
        auto *cacheTitle = new QLabel(QStringLiteral("缓存"), this);
        QFont titleFont = QApplication::font();
        titleFont.setPixelSize(15);
        titleFont.setWeight(QFont::DemiBold);
        cacheTitle->setFont(titleFont);
        cacheHeader->addWidget(cacheTitle);
        cacheStatus = new QLabel(this);
        cacheStatus->setObjectName(QStringLiteral("downloadCacheStatus"));
        cacheStatus->setTextFormat(Qt::PlainText);
        cacheStatus->setProperty(role.constData(), Theme::secondaryTextRoleValue());
        cacheHeader->addWidget(cacheStatus, 1);
        layout->addLayout(cacheHeader);

        auto *cacheRow = new QHBoxLayout;
        cacheRow->setSpacing(8);
        cacheLimit = new QSpinBox(this);
        cacheLimit->setObjectName(QStringLiteral("downloadCacheLimit"));
        cacheLimit->setRange(128, 65536);
        cacheLimit->setValue(2048);
        cacheLimit->setSuffix(QStringLiteral(" MiB 上限"));
        cacheLimit->setButtonSymbols(QAbstractSpinBox::NoButtons);
        // 数字 + 单位一起居中：右对齐时那 33px 余量全落在左边，看起来像"左空 48、右空 15"
        // （走查 j07）。QSS 里四边对称的 3px 边 + 8px padding 保证居中之后两边一样宽。
        cacheLimit->setAlignment(Qt::AlignCenter);
        cacheLimit->setFixedWidth(150);
        cacheRow->addWidget(cacheLimit);
        const struct { const char *text; const char *tip; bool danger; } kActions[] = {
            {"按上限整理", "按最旧优先整理闲置缓存。", false},
            {"清理过期", "清理 30 天未更新的缓存。", false},
            {"清空闲置缓存…", "只删未被任务使用的分段和续传记录。", true},
        };
        for (int index = 0; index < 3; ++index) {
            auto *button = new QPushButton(QString::fromUtf8(kActions[index].text), this);
            const int action = index + 1;
            button->setObjectName(QStringLiteral("downloadCacheAction%1").arg(action));
            button->setToolTip(QString::fromUtf8(kActions[index].tip));
            Theme::markButton(button, kActions[index].danger ? Theme::ButtonKind::Ghost : Theme::ButtonKind::Neutral);
            if (kActions[index].danger) {
                button->setStyleSheet(QStringLiteral("QPushButton { color: %1; }").arg(Theme::tokens().danger.name()));
                Theme::follow(button, [button] {
                    button->setStyleSheet(QStringLiteral("QPushButton { color: %1; }").arg(Theme::tokens().danger.name()));
                });
            }
            connect(button, &QPushButton::clicked, this, [this, action] { if (onCache) onCache(action); });
            cacheRow->addWidget(button);
            cacheButtons.append(button);
        }
        cacheRow->addStretch(1);
        layout->addLayout(cacheRow);
        auto *cacheHint = caption(QStringLiteral("过期 = 30 天未更新；不影响进行中的下载。"));
        layout->addWidget(cacheHint);
        layout->addStretch(1);
        setBusy(false);
    }
    void setBusy(bool busy) {
        start->setEnabled(true);
        start->setText(busy ? QStringLiteral("加入队列") : QStringLiteral("开始下载"));
        stop->setEnabled(busy);
        browse->setEnabled(true);
        for (auto *field : {url, directory, name}) field->setEnabled(true);
    }
    void showProgress(qint64 done, qint64 total, const QString &text) {
        progressBox->show();
        if (total <= 0) { progress->setRange(0, 0); percent->setText(QStringLiteral("…")); }
        else {
            progress->setRange(0, 1000); progress->setValue(int(qBound(0.0, double(done) * 1000 / double(total), 1000.0)));
            percent->setText(QStringLiteral("%1%").arg(progress->value() / 10));
        }
        showStatus(text);
    }
    /// 状态句（连接中 / 完成 / 失败）。空串 = 收起整块进度区里的那一句。
    void showStatus(const QString &text) {
        status->setText(text);
        status->setVisible(!text.isEmpty());
        if (!text.isEmpty()) progressBox->show();
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
    QWidget *progressBox = nullptr;
    QList<QPushButton *> cacheButtons;

protected:
    void resizeEvent(QResizeEvent *event) override {
        QWidget::resizeEvent(event);
        arrangePathRow(event->size().width() < 640);
    }

private:
    /// 并排：保存目录（1fr）+ 文件名（1fr）；叠放：各占一行。
    void arrangePathRow(bool stacked) {
        if (m_stacked == int(stacked) && m_pathGrid->count() > 0) return;
        m_stacked = int(stacked);
        while (m_pathGrid->count() > 0) delete m_pathGrid->takeAt(0);
        if (stacked) {
            m_pathGrid->addWidget(m_directoryCaption, 0, 0);
            m_pathGrid->addWidget(m_directoryRow, 1, 0);
            m_pathGrid->addWidget(m_nameCaption, 2, 0);
            m_pathGrid->addWidget(name, 3, 0);
            m_pathGrid->setColumnStretch(0, 1);
            m_pathGrid->setColumnStretch(1, 0);
        } else {
            m_pathGrid->addWidget(m_directoryCaption, 0, 0);
            m_pathGrid->addWidget(m_nameCaption, 0, 1);
            m_pathGrid->addWidget(m_directoryRow, 1, 0);
            m_pathGrid->addWidget(name, 1, 1);
            m_pathGrid->setColumnStretch(0, 3);
            m_pathGrid->setColumnStretch(1, 2);
        }
    }
    QGridLayout *m_pathGrid = nullptr;
    QWidget *m_directoryRow = nullptr;
    QLabel *m_directoryCaption = nullptr, *m_nameCaption = nullptr;
    int m_stacked = -1;
};
