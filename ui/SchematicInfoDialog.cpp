#include "SchematicInfoDialog.hpp"

#include "schematic/SchematicInfo.hpp"
#include "Theme.hpp"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

SchematicInfoDialog::SchematicInfoDialog(const QString &path, QWidget *parent)
    : ChromeDialog(parent) {
    setObjectName(QStringLiteral("schematicInfoDialog"));
    setWindowTitle(QStringLiteral("原理图信息"));
    resize(620, 420);
    auto *layout = new QVBoxLayout(body());
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(10);

    const QFileInfo file(path);
    auto *title = new QLabel(QStringLiteral("<b>%1</b>").arg(file.fileName().toHtmlEscaped()), this);
    title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(title);
    auto *size = new QLabel(QStringLiteral("大小：%1 MB").arg(file.size() / 1024.0 / 1024.0, 0, 'f', 2), this);
    layout->addWidget(size);

    const SchematicInfo::Info info = SchematicInfo::read(path);
    auto *summary = new QLabel(SchematicInfo::summaryText(info, QString()), this);
    summary->setWordWrap(true);
    summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(summary, 1);

    // 不用标准钮（QDialogButtonBox::Save/Cancel/…）：它的字在 LanguageChange 时会被 Qt 刷回自带的英文，
    // 装没装 Qt 中文翻译都一样不稳。自己 addButton(字, 角色)，accepted/rejected 照常发。
    auto *buttons = new QDialogButtonBox(this);
    Theme::markButton(buttons->addButton(QStringLiteral("关闭"), QDialogButtonBox::RejectRole),
                      Theme::ButtonKind::Neutral);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

