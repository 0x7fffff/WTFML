/**
 * @file ServerConsolePage.cpp
 * @brief 服务端控制台那一页的实现（形状与取舍见头文件）。
 * @author 0x7fffff
 * @date 2026-10-13
 */

#include "ServerConsolePage.hpp"

#include <QFont>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextCursor>  // setLog() 末尾那一下"落到最底下"
#include <QVBoxLayout>

#include "MainWindowUI.hpp"
#include "Theme.hpp"

namespace {

/// 一行小字（状态那句）。和 `NewServerPage.cpp` 里那个 `makeMutedLabel()` 同一套做法
/// —— 全仓"比正文浅一档"的取法只有这一种（从自己的调色板算，不写死颜色）。
QLabel *makeMutedLabel(QWidget *parent, const QString &objectName) {
    auto *label = new QLabel(parent);
    label->setObjectName(objectName);
    QPalette palette = label->palette();
    QColor muted = palette.color(QPalette::WindowText);
    muted.setAlpha(150);
    palette.setColor(QPalette::WindowText, muted);
    label->setPalette(palette);
    return label;
}

}  // namespace

ServerConsolePage::ServerConsolePage(QWidget *parent) : QDialog(parent) {
    setObjectName(MainWindowUI::serverConsolePageName());
    setWindowTitle(windowTitle());
    // 只影响"单独拿出来用"（验证工程那几处）：嵌进覆盖层时这一页拿的是内容区的整幅宽。
    resize(880, 620);

    auto *box = new QVBoxLayout(this);
    box->setContentsMargins(12, 12, 12, 12);
    box->setSpacing(10);

    // ---------- 顶上：哪一个服务端 + 它现在什么状态 ----------
    {
        auto *row = new QWidget(this);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        m_titleLabel = new QLabel(row);
        m_titleLabel->setObjectName(MainWindowUI::serverConsoleTitleName());
        QFont titleFont = m_titleLabel->font();
        titleFont.setBold(true);
        m_titleLabel->setFont(titleFont);
        // 名字可能很长（用户起的）：不许它把状态那半截挤出去。**这一句要在 setFont 之后**
        // （尺寸策略跟字体无关，但下面取调色板那一步跟字体有关，这里保持一致）。
        m_titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        layout->addWidget(m_titleLabel, 1);
        m_statusLabel = makeMutedLabel(row, MainWindowUI::serverConsoleStatusName());
        layout->addWidget(m_statusLabel, 0);
        box->addWidget(row);
    }

    // ---------- 中间那一大片：它的输出 ----------
    {
        m_logView = new QPlainTextEdit(this);
        m_logView->setObjectName(MainWindowUI::serverConsoleLogName());
        m_logView->setReadOnly(true);
        // 等宽字体：服务端那些日志是对着列的（时间戳 / 级别 / 正文），比例字体一读就歪。
        // 问 `QFontDatabase` 要系统那一个，不写死字体名 —— 写死的话换台机器就退回去了。
        QFont logFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        m_logView->setFont(logFont);
        // 不许折行：折了之后一行日志占两行、时间戳和正文错开，反而更难读（横向滚就是了）。
        m_logView->setLineWrapMode(QPlainTextEdit::NoWrap);
        // NOTE: 这里**不做**选中复制那一套特别设置 —— QPlainTextEdit 默认就能选中复制，
        //       而这一页正是"用户要拿一行去搜"的地方。
        box->addWidget(m_logView, 1);
    }

    // ---------- 底下：往里敲一行 ----------
    {
        auto *row = new QWidget(this);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        m_commandEdit = new QLineEdit(row);
        m_commandEdit->setObjectName(MainWindowUI::serverConsoleCommandEditName());
        m_commandEdit->setPlaceholderText(
            QStringLiteral("往这个服务端敲一条命令（比如 say 大家好），回车发送"));
        layout->addWidget(m_commandEdit, 1);
        m_sendButton = new QPushButton(MainWindowUI::serverConsoleSendText(), row);
        m_sendButton->setObjectName(MainWindowUI::serverConsoleSendButtonName());
        // 送一行命令 = 这一页的主操作，走主题色那一档（和「创建」「新建服务端…」同一档）。
        Theme::markButton(m_sendButton, Theme::ButtonKind::Accent);
        layout->addWidget(m_sendButton, 0);
        box->addWidget(row);

        // 回车 = 发送（和按那颗按钮一回事）。
        connect(m_commandEdit, &QLineEdit::returnPressed, m_sendButton, &QPushButton::click);
        connect(m_sendButton, &QPushButton::clicked, this, [this] {
            const QString text = m_commandEdit->text().trimmed();
            if (text.isEmpty()) {
                return;  // 空行不发（服务端那边收到一个空命令只会回一句没用的报错）
            }
            emit commandSubmitted(text);
            m_commandEdit->clear();
        });
    }

    // ---------- 最下面：停它 ----------
    {
        auto *row = new QWidget(this);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        layout->addStretch(1);
        m_forceKillButton = new QPushButton(MainWindowUI::serverConsoleForceKillText(), row);
        m_forceKillButton->setObjectName(MainWindowUI::serverConsoleForceKillButtonName());
        // 强杀 = 可能丢存档 ⇒ 危险那一档的红（和「删除这个服务端」同一档）。
        Theme::markButton(m_forceKillButton, Theme::ButtonKind::Danger);
        m_forceKillButton->setToolTip(
            QStringLiteral("不等它存盘了，直接把进程打死 —— 可能会丢存档。"));
        layout->addWidget(m_forceKillButton, 0);
        m_stopButton = new QPushButton(MainWindowUI::serverConsoleStopText(), row);
        m_stopButton->setObjectName(MainWindowUI::serverConsoleStopButtonName());
        Theme::markButton(m_stopButton, Theme::ButtonKind::Neutral);
        m_stopButton->setToolTip(QStringLiteral("让它自己存盘、收尾，然后退出（推荐）。"));
        layout->addWidget(m_stopButton, 0);
        box->addWidget(row);

        connect(m_stopButton, &QPushButton::clicked, this, &ServerConsolePage::stopRequested);
        connect(m_forceKillButton, &QPushButton::clicked, this,
                &ServerConsolePage::forceKillRequested);
    }

    // 起手是"什么都没在跑"那一档（`MainWindow` 开页时会立刻喂一遍真状态）。
    setStopAvailable(false);
    setForceKillAvailable(false);
    setCommandAvailable(false);
}

void ServerConsolePage::setServerName(const QString &name) {
    if (m_titleLabel != nullptr) {
        m_titleLabel->setText(name);
    }
    setWindowTitle(name.isEmpty() ? windowTitle() : name);
}

void ServerConsolePage::setStatusText(const QString &text) {
    if (m_statusLabel != nullptr) {
        m_statusLabel->setText(text);
    }
}

void ServerConsolePage::setStopAvailable(bool available) {
    if (m_stopButton != nullptr) {
        m_stopButton->setEnabled(available);
    }
}

void ServerConsolePage::setForceKillAvailable(bool available) {
    if (m_forceKillButton != nullptr) {
        m_forceKillButton->setEnabled(available);
    }
}

void ServerConsolePage::setCommandAvailable(bool available) {
    if (m_commandEdit != nullptr) {
        m_commandEdit->setEnabled(available);
    }
    if (m_sendButton != nullptr) {
        m_sendButton->setEnabled(available);
    }
}

void ServerConsolePage::setMaxLines(int lines) {
    if (m_logView != nullptr) {
        // `setMaximumBlockCount()` 自己会把超出去的老行丢掉 —— 不用在这儿另写一段"留下
        // 后 N 行"的逻辑（写两遍的话两边迟早对不上）。
        m_logView->setMaximumBlockCount(qMax(1, lines));
    }
}

void ServerConsolePage::setLog(const QStringList &lines) {
    if (m_logView == nullptr) {
        return;
    }
    m_logView->setPlainText(lines.join(QLatin1Char('\n')));
    // 落到最底下：用户开这一页十有八九是来看"刚才那几行"的。
    m_logView->moveCursor(QTextCursor::End);
    m_logView->ensureCursorVisible();
}

void ServerConsolePage::appendLine(const QString &line) {
    if (m_logView == nullptr) {
        return;
    }
    // ★ `appendPlainText()` 会自己补一个换行，而且**会顺带把滚动条钉到底**（它是
    //   "一条新消息来了"的语义）。这一页要的正是这个，别改成 insertPlainText + 自己挪光标。
    m_logView->appendPlainText(line);
}

QStringList ServerConsolePage::logLines() const {
    if (m_logView == nullptr) {
        return QStringList();
    }
    const QString text = m_logView->toPlainText();
    if (text.isEmpty()) {
        return QStringList();
    }
    return text.split(QLatin1Char('\n'));
}

QString ServerConsolePage::commandText() const {
    return m_commandEdit != nullptr ? m_commandEdit->text() : QString();
}
