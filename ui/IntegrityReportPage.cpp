/**
 */

#include "IntegrityReportPage.hpp"

#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "MainWindowUI.hpp"
#include "Theme.hpp"

namespace {

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

IntegrityReportPage::IntegrityReportPage(QWidget *parent) : QDialog(parent) {
    setObjectName(MainWindowUI::integrityReportPageName());
    setWindowTitle(windowTitle());
    // 只影响"单独拿出来用"（验证工程那几处）：嵌进覆盖层时这一页拿的是内容区的整幅宽。
    resize(620, 520);

    m_box = new QVBoxLayout(this);
    // 边上那一圈和资源详情页 / 任务详情页一样是 12（覆盖层自己还有 9）。
    m_box->setContentsMargins(12, 12, 12, 12);
    m_box->setSpacing(10);

    // ---------- 总述那一句（大一号、加粗）----------
    // 实例名写在最前面那句里（`「1.20.1」3120 个文件里 2 个要修：…`），所以这里只留一行。
    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setObjectName(MainWindowUI::integrityReportSummaryName());
    m_summaryLabel->setWordWrap(true);
    {
        QFont font = m_summaryLabel->font();
        font.setBold(true);
        font = Theme::textFont(font, Theme::TextRole::Heading);
        m_summaryLabel->setFont(font);
    }
    m_box->addWidget(m_summaryLabel);

    // ---------- 出错那一块（只在"体检没跑起来"时露）----------
    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName(MainWindowUI::integrityReportErrorName());
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_box->addWidget(m_errorLabel);

    m_box->addWidget(makeSeparator(this));

    // ---------- 四组的内容都长在这一块里（每次 setReport() 整块重建）----------
    m_body = new QWidget(this);
    auto *bodyLayout = new QVBoxLayout(m_body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(12);
    m_box->addWidget(m_body);

    m_box->addStretch(1);
    m_box->addWidget(makeSeparator(this));

    // ---------- 那一排按钮 ----------
    // 「只查看」是这一页的一条出路（另一条是顶上那颗 `← 返回`）—— 它不是动作，直接 reject()。
    auto *actions = new QHBoxLayout;
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(8);
    auto *close = new QPushButton(MainWindowUI::integrityReportCloseText(), this);
    close->setObjectName(MainWindowUI::integrityReportCloseName());
    Theme::markButton(close, Theme::ButtonKind::Neutral);
    close->setCursor(Qt::PointingHandCursor);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    actions->addWidget(close);
    actions->addStretch(1);
    m_copyButton = new QPushButton(MainWindowUI::integrityReportCopyText(), this);
    m_copyButton->setObjectName(MainWindowUI::integrityReportCopyName());
    Theme::markButton(m_copyButton, Theme::ButtonKind::Ghost);
    m_copyButton->setCursor(Qt::PointingHandCursor);
    connect(m_copyButton, &QPushButton::clicked, this,
            [this] { requestAction(Action::CopyReport); });
    actions->addWidget(m_copyButton);
    m_repairButton = new QPushButton(MainWindowUI::integrityReportRepairText(), this);
    m_repairButton->setObjectName(MainWindowUI::integrityReportRepairName());
    // 这一页唯一的主动作就是"补文件"（Primary = 主题色实底 + 白字）。
    Theme::markButton(m_repairButton, Theme::ButtonKind::Primary);
    m_repairButton->setCursor(Qt::PointingHandCursor);
    connect(m_repairButton, &QPushButton::clicked, this, [this] { requestAction(Action::Repair); });
    actions->addWidget(m_repairButton);
    m_box->addLayout(actions);

    setReport(Report());
}

void IntegrityReportPage::setReport(const Report &report) {
    m_report = report;
    rebuild();
}

void IntegrityReportPage::rebuild() {
    // ---------- 总述那一句 ----------
    // error 非空时总述就是那句 error 本身（`summary` 那一栏由翻译点填成同一句），所以这里
    // 只把实例名拼在前面，让用户一眼看清说的是哪个实例。
    m_summaryLabel->setText(QStringLiteral("「%1」%2").arg(m_report.instanceName, m_report.summary));

    // ---------- 出错那一块 ----------
    const bool hasError = !m_report.error.isEmpty();
    m_errorLabel->setVisible(hasError);
    if (hasError) {
        QPalette palette = m_errorLabel->palette();
        m_errorLabel->setText(MainWindowUI::integrityReportErrorTitleText() + QStringLiteral("\n")
                              + m_report.error);
        palette.setColor(QPalette::WindowText, MainWindowUI::folderDangerColor(palette));
        m_errorLabel->setPalette(palette);
    }

    // ---------- 四组 ----------
    auto *body = qobject_cast<QVBoxLayout *>(m_body->layout());
    if (body != nullptr) {
        while (QLayoutItem *child = body->takeAt(0)) {
            delete child->widget();
            delete child;
        }

        // error 那一趟压根没有分组（清单都没列出来）—— 别再画四个空组出来。
        if (!hasError) {
            int rowIndex = 0;
            for (int g = 0; g < m_report.groups.size(); ++g) {
                const Group &group = m_report.groups.at(g);

                // 组标题：`客户端（2 个要修）`
                auto *title = new QLabel(m_body);
                title->setObjectName(MainWindowUI::integrityReportGroupName(g));
                {
                    QFont font = title->font();
                    font.setBold(true);
                    title->setFont(font);
                }
                title->setText(MainWindowUI::integrityReportGroupTitleText(group.name,
                                                                           group.problems()));
                body->addWidget(title);

                // 组内那一块（逐条问题，或者一句"没有要修的"）
                auto *groupBody = new QWidget(m_body);
                groupBody->setObjectName(MainWindowUI::integrityReportGroupBodyName(g));
                auto *rows = new QVBoxLayout(groupBody);
                rows->setContentsMargins(12, 0, 0, 0);  // 缩进，看得出是这一组里的
                rows->setSpacing(6);

                if (group.rows.isEmpty()) {
                    auto *none = makeMutedLabel(groupBody, QString());
                    none->setText(MainWindowUI::integrityReportAllGoodText());
                    rows->addWidget(none);
                }
                for (const Row &row : group.rows) {
                    auto *label = new QLabel(groupBody);
                    label->setObjectName(MainWindowUI::integrityReportRowName(rowIndex++));
                    label->setWordWrap(true);
                    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
                    label->setText(
                        MainWindowUI::integrityReportRowText(row.label, row.detail, row.path));
                    rows->addWidget(label);
                }
                body->addWidget(groupBody);
            }
        }
    }

    m_copyButton->setVisible(true);
    // 「补齐缺失或损坏文件」只在**真有要修的文件**时露：没得修的时候给一颗点了什么都不做的
    // 按钮，比不给还糟（和任务详情页那句"按钮不靠状态猜"是同一条）。
    m_repairButton->setVisible(!hasError && m_report.problems() > 0);
}
