/**
 * @file TaskDetailPage.cpp
 * @brief 任务详情页的实现（形状与取舍见头文件）。
 */

#include "TaskDetailPage.hpp"

#include <QDateTime>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "ElidedLabel.hpp"
#include "MainWindowUI.hpp"
#include "TaskProgressBar.hpp"
#include "Theme.hpp"

namespace {

/// 状态那一行左边那个记号的颜色：失败和中断染红，别的按正文走。
bool stateIsDanger(TaskCenter::State state) {
    return state == TaskCenter::State::Failed || state == TaskCenter::State::Interrupted;
}

/// 一行小字（次要信息）。挂上 objectName 是为了验证工程找得到它（找不到的按"没画"算）。
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

QFrame *makeSeparator(QWidget *parent) {
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::NoFrame);
    line->setFixedHeight(8);
    return line;
}

}  // namespace

TaskDetailPage::TaskDetailPage(QWidget *parent) : QDialog(parent) {
    setObjectName(MainWindowUI::taskDetailPageName());
    setWindowTitle(windowTitle());
    // 只影响"单独拿出来用"（验证工程那几处）：嵌进覆盖层的时候，这一页拿到的是内容区的
    // 整幅宽（见 OverlayHost::execEmbedded 里那句 area->layout()->addWidget(dialog)）。
    resize(560, 460);

    m_box = new QVBoxLayout(this);
    // 边上那一圈和资源详情页一样是 12（覆盖层自己还有 9，见 OverlayHost::kOverlayPageMargin）。
    m_box->setContentsMargins(12, 12, 12, 12);
    m_box->setSpacing(10);

    // ---------- 状态那一行：记号 + 状态词 + 右边"什么时候" ----------
    auto *stateRow = new QHBoxLayout;
    stateRow->setContentsMargins(0, 0, 0, 0);
    stateRow->setSpacing(6);
    m_stateLabel = new QLabel(this);
    m_stateLabel->setObjectName(MainWindowUI::taskDetailStateName());
    stateRow->addWidget(m_stateLabel, 0, Qt::AlignVCenter);
    m_timeLabel = makeMutedLabel(this, MainWindowUI::taskDetailTimeName());
    stateRow->addWidget(m_timeLabel, 0, Qt::AlignVCenter);
    stateRow->addStretch(1);
    m_box->addLayout(stateRow);

    // ---------- 标题（大一号）----------
    m_titleLabel = new ElidedLabel(this);
    m_titleLabel->setObjectName(MainWindowUI::taskDetailTitleName());
    {
        QFont titleFont = m_titleLabel->font();
        titleFont.setBold(true);
        titleFont = Theme::textFont(titleFont, Theme::TextRole::Heading);
        m_titleLabel->setFont(titleFont);
    }
    m_box->addWidget(m_titleLabel);

    // ---------- 一行小字（"正在下载资源对象 · 2.8 MB/s"）----------
    m_detailLabel = makeMutedLabel(this, MainWindowUI::taskDetailDetailName());
    m_detailLabel->setWordWrap(true);
    m_box->addWidget(m_detailLabel);

    // ---------- 进度：条 + 右边那个数 ----------
    // 测不出总量（percent < 0）的时候**整行收起来**，不画一根不确定态的条，也不编一个
    // 百分比出来 —— 和面板那一行同一条规矩。
    auto *barRow = new QHBoxLayout;
    barRow->setContentsMargins(0, 0, 0, 0);
    barRow->setSpacing(8);
    m_bar = new TaskProgressBar(this);
    m_bar->setObjectName(MainWindowUI::taskDetailBarName());
    m_bar->setRange(0, 100);
    m_bar->setFixedHeight(MainWindowUI::taskCenterPanelBarHeight());
    barRow->addWidget(m_bar, 1);
    auto *percent = makeMutedLabel(this, MainWindowUI::taskDetailPercentName());
    barRow->addWidget(percent, 0, Qt::AlignVCenter);
    // 那一格是这条布局里第三样（0=条、1=数）—— rebuild() 里按名字找回来重写它。
    m_box->addLayout(barRow);

    // ---------- 字节那一句（按字节记账的任务才有）----------
    m_bytesLabel = makeMutedLabel(this, MainWindowUI::taskDetailBytesName());
    m_box->addWidget(m_bytesLabel);

    m_box->addWidget(makeSeparator(this));

    // ---------- 「到了哪一步」：步骤清单 ----------
    auto *stepsTitle = new QLabel(MainWindowUI::taskDetailStepsTitleText(), this);
    {
        QFont font = stepsTitle->font();
        font.setBold(true);
        stepsTitle->setFont(font);
    }
    m_box->addWidget(stepsTitle);
    m_body = new QWidget(this);
    m_body->setObjectName(MainWindowUI::taskDetailStepsName());
    auto *bodyLayout = new QVBoxLayout(m_body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(4);
    m_box->addWidget(m_body);

    // ---------- 出错那一块（只在真出过错的时候露）----------
    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName(MainWindowUI::taskDetailErrorName());
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_box->addWidget(m_errorLabel);

    m_box->addWidget(makeSeparator(this));

    // ---------- 那一排按钮 ----------
    // 露与不露**只读能力位**（见头文件那段）；这一排里"关闭"永远在，它是这一页的出路之一
    // （另一条出路是顶上那颗 `← 返回`）。
    auto *actions = new QHBoxLayout;
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(8);
    m_cancelButton = makeActionButton(MainWindowUI::taskDetailCancelText(), Action::Cancel,
                                      MainWindowUI::taskDetailCancelName());
    actions->addWidget(m_cancelButton);
    m_retryButton = makeActionButton(MainWindowUI::taskDetailRetryText(), Action::Retry,
                                     MainWindowUI::taskDetailRetryName());
    actions->addWidget(m_retryButton);
    actions->addStretch(1);
    m_logButton = makeActionButton(MainWindowUI::taskDetailLogText(), Action::OpenLog,
                                   MainWindowUI::taskDetailLogName());
    actions->addWidget(m_logButton);
    m_targetButton = makeActionButton(MainWindowUI::taskDetailTargetText(), Action::OpenTarget,
                                      MainWindowUI::taskDetailTargetName());
    actions->addWidget(m_targetButton);
    m_repairButton = makeActionButton(tr("补齐文件"), Action::RepairImport,
                                      QStringLiteral("taskDetailRepairImport"));
    actions->addWidget(m_repairButton);
    m_recoverButton = makeActionButton(tr("继续未完成操作"), Action::Recover,
                                       QStringLiteral("taskDetailRecover"));
    actions->addWidget(m_recoverButton);
    m_pauseButton = makeActionButton(tr("暂停下载"), Action::Pause, QStringLiteral("taskDetailPause"));
    actions->addWidget(m_pauseButton);
    m_reportButton = makeActionButton(MainWindowUI::taskDetailReportText(), Action::ExportReport,
                                      MainWindowUI::taskDetailReportName());
    actions->addWidget(m_reportButton);
    m_crashDirButton = makeActionButton(MainWindowUI::taskDetailCrashDirText(),
                                        Action::OpenCrashDir,
                                        MainWindowUI::taskDetailCrashDirName());
    actions->addWidget(m_crashDirButton);
    m_copyButton = makeActionButton(MainWindowUI::taskDetailCopyText(), Action::CopySummary,
                                    MainWindowUI::taskDetailCopyName());
    actions->addWidget(m_copyButton);
    auto *close = new QPushButton(MainWindowUI::taskDetailCloseText(), this);
    close->setObjectName(MainWindowUI::taskDetailCloseName());
    Theme::markButton(close, Theme::ButtonKind::Neutral);
    close->setCursor(Qt::PointingHandCursor);
    // 关页 = 走覆盖层那条返回路：`reject()` 发 `QDialog::finished`，`showPage()` 接的就是
    // 这一发（返回箭头也只是转成 `reject()`；`execPage()` 那一档同样是它）。
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    actions->addWidget(close);
    m_box->addLayout(actions);

    m_box->addStretch(1);

    setTask(TaskCenter::Item());
}

QPushButton *TaskDetailPage::makeActionButton(const QString &text, Action action,
                                              const QString &objectName) {
    auto *button = new QPushButton(text, this);
    button->setObjectName(objectName);
    Theme::markButton(button, Theme::ButtonKind::Neutral);
    button->setCursor(Qt::PointingHandCursor);
    connect(button, &QPushButton::clicked, this, [this, action] { requestAction(action); });
    return button;
}

void TaskDetailPage::setTask(const TaskCenter::Item &item) {
    m_item = item;
    m_key = item.key;
    rebuild();
}

void TaskDetailPage::rebuild() {
    const TaskCenter::State state = m_item.state;
    const bool active = TaskCenter::stateIsActive(state);

    // ---------- 状态那一行 ----------
    const QString mark = TaskCenter::terminalMark(state);
    m_stateLabel->setText(mark.isEmpty() ? MainWindowUI::taskDetailStateText(state)
                                         : mark + QLatin1Char(' ')
                                               + MainWindowUI::taskDetailStateText(state));
    {
        QPalette palette = m_stateLabel->palette();
        QColor ink = palette.color(QPalette::WindowText);
        if (stateIsDanger(state)) {
            ink = MainWindowUI::folderDangerColor(palette);
        }
        palette.setColor(QPalette::WindowText, ink);
        m_stateLabel->setPalette(palette);
    }
    m_timeLabel->setText(MainWindowUI::taskDetailTimeLineText(m_item.startedAt, m_item.finishedAt));
    m_timeLabel->setVisible(!m_timeLabel->text().isEmpty());

    // ---------- 标题 / 小字 ----------
    m_titleLabel->setText(m_item.title.isEmpty() ? MainWindowUI::taskDetailNoTitleText()
                                                 : m_item.title);
    m_detailLabel->setText(m_item.detail);
    m_detailLabel->setVisible(!m_item.detail.isEmpty());

    // ---------- 进度那两样 ----------
    const bool showPercent = m_item.percent >= 0;
    m_bar->setVisible(showPercent);
    m_bar->setValue(m_item.percent);
    // 数那一格是进度行里第二件（第一条是那条 bar）。按 objectName 找回来而不是留个指针：
    // 那一条布局里就这一件是 QLabel，加一件新东西时这里不会悄悄跟着错。
    if (auto *percent = findChild<QLabel *>(MainWindowUI::taskDetailPercentName())) {
        percent->setText(showPercent ? MainWindowUI::taskCenterPercentText(m_item.percent)
                                     : QString());
        percent->setVisible(showPercent);
    }
    m_bytesLabel->setText(m_item.bytesText);
    m_bytesLabel->setVisible(!m_item.bytesText.isEmpty());

    // ---------- 步骤那一栏 ----------
    auto *body = qobject_cast<QVBoxLayout *>(m_body->layout());
    if (body != nullptr) {
        while (QLayoutItem *child = body->takeAt(0)) {
            delete child->widget();
            delete child;
        }
        for (int i = 0; i < m_item.steps.size(); ++i) {
            const TaskCenter::Step step =
                TaskCenter::stepForTask(m_item.state, m_item.steps.at(i));
            auto *label = new QLabel(m_body);
            label->setObjectName(MainWindowUI::taskDetailStepName(i));
            const QString note = step.note.isEmpty() ? QString()
                                                     : QStringLiteral(" / ") + step.note;
            label->setText(TaskCenter::stepMark(step.state) + QLatin1Char(' ') + step.name + note);
            if (step.state == TaskCenter::StepState::Failed) {
                QPalette palette = label->palette();
                palette.setColor(QPalette::WindowText,
                                 MainWindowUI::folderDangerColor(palette));
                label->setPalette(palette);
            }
            body->addWidget(label);
        }
        if (m_item.steps.isEmpty()) {
            // 没有步骤的任务多的是（一次纯文件下载就没有）—— 写一句"没有分步"比留一块空白
            // 强，也让验证工程分得清"这一项本来就没有步骤"和"步骤没画出来"。
            auto *none = makeMutedLabel(m_body, MainWindowUI::taskDetailNoStepsName());
            none->setText(MainWindowUI::taskDetailNoStepsText());
            body->addWidget(none);
        }
    }

    // ---------- 出错那一块 ----------
    const bool hasError = !m_item.errorSummary.isEmpty() && !active;
    m_errorLabel->setVisible(hasError);
    if (hasError) {
        QPalette palette = m_errorLabel->palette();
        m_errorLabel->setText(MainWindowUI::taskDetailErrorTitleText() + QStringLiteral("\n")
                              + m_item.errorSummary);
        palette.setColor(QPalette::WindowText, MainWindowUI::folderDangerColor(palette));
        m_errorLabel->setPalette(palette);
    }

    // ---------- 按钮那一排 ----------
    m_cancelButton->setVisible(m_item.capabilities.cancel);
    m_retryButton->setVisible(m_item.capabilities.retry);
    m_logButton->setVisible(m_item.capabilities.openLog);
    m_targetButton->setVisible(m_item.capabilities.openTarget);
    m_repairButton->setVisible(m_item.capabilities.repairImport);
    m_recoverButton->setVisible(m_item.capabilities.recover);
    m_pauseButton->setVisible(m_item.capabilities.pause);
    m_reportButton->setVisible(m_item.capabilities.exportReport);
    m_crashDirButton->setVisible(m_item.capabilities.openCrashDir);
    // 「复制摘要」永远在（它不需要任何能力位 —— 摘要就是这一页上那几行字，任何时候都抄得走）。
    m_copyButton->setVisible(!m_key.isEmpty());
}
