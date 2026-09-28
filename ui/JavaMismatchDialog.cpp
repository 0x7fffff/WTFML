/**
 * @file JavaMismatchDialog.cpp
 * @brief Implements the "instance Java does not match this version" three-way dialog.
 * @author 0x7fffff
 * @date 2026-09-21
 * @note 布局从上到下：正文 → 「以后不再提醒」→ 按钮那一排。正文里必须同时出现
 *       **实例名**和**两个版本号** —— 用户看到这个框时的第一反应是"我哪个实例又出事了"，
 *       少写一个他就得自己去猜。
 */

#include "JavaMismatchDialog.hpp"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

// 正文那个宽度常量（`promptContentWidth()`）是那一层给的 —— 这个框现在开在它里面。
#include "OverlayHost.hpp"
#include "Theme.hpp"   // 「取消」那一颗的身份（一般性 = 黑字黑边）

namespace {

/// 实际那个 Java 的可读写法。版本串可能是空的（release 快路径不回填 versionOutput），
/// 所以这里必须兜住"只有主版本号"这一种，否则正文里会出现「Java （）」。
QString describeActual(int actualMajor, const QString &actualVersionText) {
    const QString text = actualVersionText.trimmed();
    if (text.isEmpty()) {
        return QStringLiteral("Java %1").arg(actualMajor);
    }
    return QStringLiteral("Java %1（%2）").arg(actualMajor).arg(text);
}

}  // namespace

QString JavaMismatchDialog::messageText(const QString &instanceName, int expectedMajor,
                                        int actualMajor, const QString &actualVersionText,
                                        const QString &javaPath) {
    return QStringLiteral(
               "实例「%1」在「实例设置…」里指定了 %2，而这个版本要求 Java %3。\n\n"
               "仍然用它启动多半起不来（报 UnsupportedClassVersionError）；"
               "改用匹配的 Java 会把「实例设置…」里那一项换成自动挑中的匹配版本，并记下来。\n\n"
               "%4")
        .arg(instanceName, describeActual(actualMajor, actualVersionText),
             QString::number(expectedMajor), javaPath);
}

JavaMismatchDialog::JavaMismatchDialog(const QString &instanceName, int expectedMajor,
                                       int actualMajor, const QString &actualVersionText,
                                       const QString &javaPath, QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("Java 版本对不上"));
    // 2026-09-25：原来这里有一句 `setMinimumWidth(560)`。它当时的意思是"这句话很长，别挤
    // 成一条竖线"。现在这个框开在**内容区中间那一块**里（见 OverlayHost::execPrompt），
    // 而那一块的宽度是**全项目一个常数**（`promptWidth()`，用户原话「不会让界面出现
    // 尺寸不一样的弹窗」）—— 560 会把这一档撑成跟别的提示不一样宽，所以那句话去掉了。
    // 宽度改由下面那个正文标签**钉成 `promptContentWidth()`**：和 `InlineMessageBox`
    // 同一套做法，钉了之后"这段字换几行"是个确定的数，卡片的 `sizeHint().height()` 才准。

    auto *layout = new QVBoxLayout(this);
    // 留白由卡片自己给（`promptPadding()`），这里再给一层就成了双层留白。
    layout->setContentsMargins(0, 0, 0, 0);

    m_textLabel = new QLabel(
        messageText(instanceName, expectedMajor, actualMajor, actualVersionText, javaPath), this);
    m_textLabel->setObjectName(textLabelName());
    m_textLabel->setWordWrap(true);
    // 那条路径会很长（`C:/.../.minecraft/runtime/.../bin/java.exe`），允许选中复制：
    // 用户要按正文里那句话去改配置时，手抄一条路径是不现实的。
    m_textLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_textLabel->setFixedWidth(OverlayHost::promptContentWidth());
    layout->addWidget(m_textLabel);

    m_dontRemindCheck = new QCheckBox(tr("以后不再提醒这个实例"), this);
    m_dontRemindCheck->setObjectName(dontRemindCheckName());
    m_dontRemindCheck->setToolTip(
        tr("记在这个实例自己的 wtfml.meta 里。想再打开：实例设置… → 高级选项 → 版本提醒。"));
    layout->addWidget(m_dontRemindCheck);

    layout->addStretch(1);

    auto *buttons = new QHBoxLayout;
    m_continueButton = new QPushButton(tr("仍然用这个 Java 启动"), this);
    m_continueButton->setObjectName(continueButtonName());
    // 默认按钮 = "什么都不改，照我钉的那个启动"。理由见 .hpp：一次误按回车不该改配置。
    m_continueButton->setDefault(true);
    m_useMatchingButton = new QPushButton(tr("改用匹配的 Java 启动"), this);
    m_useMatchingButton->setObjectName(useMatchingButtonName());
    m_cancelButton = new QPushButton(tr("取消"), this);
    m_cancelButton->setObjectName(cancelButtonName());
    // 两颗「…启动」是确认性（绿），「取消」是一般性（黑）。
    Theme::markButton(m_cancelButton, Theme::ButtonKind::Neutral);
    m_cancelButton->setToolTip(tr("不启动。想先去把这个 Java 装好、或者改完实例设置再回来。"));
    buttons->addWidget(m_continueButton);
    buttons->addWidget(m_useMatchingButton);
    buttons->addStretch(1);
    buttons->addWidget(m_cancelButton);
    layout->addLayout(buttons);

    connect(m_continueButton, &QPushButton::clicked, this, [this] {
        m_choice = Choice::UseInstanceJava;
        accept();
    });
    connect(m_useMatchingButton, &QPushButton::clicked, this, [this] {
        m_choice = Choice::UseMatchingJava;
        accept();
    });
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    // 关窗 / Esc 走 reject()，**不动 m_choice** —— 它初值就是 Abort，那正是想要的。
}

bool JavaMismatchDialog::dontRemindAgain() const {
    // 构造失败（理论上到不了）时按"不勾"算：宁可下次再问一句，也不要静默地
    // 替用户把一个"以后别问了"的表态记下来。
    return m_dontRemindCheck != nullptr && m_dontRemindCheck->isChecked();
}
