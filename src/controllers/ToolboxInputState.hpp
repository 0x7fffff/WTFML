#pragma once
#include "ToolboxPage.hpp"
#include "tools/BannerDesign.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QCoreApplication>
#include <cmath>
#include <optional>

namespace ToolboxInputState {
inline QString key() { return QStringLiteral("Toolbox/RecentInputsV1"); }
inline bool input(QWidget *widget) {
    if (widget->objectName().isEmpty() || widget->objectName().startsWith(QStringLiteral("qt_"))
        || widget->objectName() == QStringLiteral("toolboxRememberInputs")) return false;
    for (auto *parent = widget->parentWidget(); parent; parent = parent->parentWidget()) {
        if (qobject_cast<QTableWidget *>(parent)) return false; // Table rows have a separate stable representation.
        // 高速下载页的下载地址可能带签名凭据，保存目录 / 缓存上限由 DirectDownloadController 自己存：都不进「记住输入」。
        if (parent->objectName() == QStringLiteral("downloadToolPanel")) return false;
    }
    if (auto *edit = qobject_cast<QLineEdit *>(widget)) return !edit->isReadOnly() && edit->echoMode() == QLineEdit::Normal;
    if (auto *edit = qobject_cast<QPlainTextEdit *>(widget)) return !edit->isReadOnly();
    return qobject_cast<QComboBox *>(widget) || qobject_cast<QSpinBox *>(widget)
        || qobject_cast<QDoubleSpinBox *>(widget) || qobject_cast<QCheckBox *>(widget);
}
/// @p previous 是上一份存下来的（`write()` 从设置里取）：没搭过的那几片这一趟没人动过，沿用它的值。
inline QByteArray capture(ToolboxPage *page, const QByteArray &previous = {}) {
    using Tool = ToolboxPage::Tool;
    QJsonObject old = previous.size() <= 256 * 1024 ? QJsonDocument::fromJson(previous).object() : QJsonObject();
    if (old.value("schemaVersion") != QJsonValue(1)) old = {};
    QJsonObject fields = old.value("fields").toObject();
    for (auto *widget : page->findChildren<QWidget *>()) {
        if (!input(widget)) continue;
        QJsonValue value;
        if (auto *edit = qobject_cast<QLineEdit *>(widget)) value = edit->text().left(4096);
        else if (auto *text = qobject_cast<QPlainTextEdit *>(widget)) value = text->toPlainText().left(32768);
        else if (auto *combo = qobject_cast<QComboBox *>(widget)) value = combo->currentIndex();
        else if (auto *spin = qobject_cast<QSpinBox *>(widget)) value = spin->value();
        else if (auto *number = qobject_cast<QDoubleSpinBox *>(widget)) value = number->value();
        else if (auto *check = qobject_cast<QCheckBox *>(widget)) value = check->isChecked();
        fields.insert(widget->objectName(), value);
    }
    QJsonObject object{{"schemaVersion", 1}, {"fields", fields}};
    for (const char *name : {"beam", "anvil", "banner"})
        if (old.contains(QLatin1String(name))) object.insert(QLatin1String(name), old.value(QLatin1String(name)));
    if (page->hasTool(Tool::Beacon)) {
        QJsonArray beam;
        auto *list = page->findChild<QListWidget *>(QStringLiteral("beamGlassList"));
        for (int i = 0; i < qMin(list->count(), 64); ++i) beam.append(list->item(i)->data(Qt::UserRole).toInt());
        object.insert("beam", beam);
    }
    if (page->hasTool(Tool::Anvil)) {
        QJsonArray anvil;
        for (const auto &row : page->anvilRows().mid(0, 64))
            anvil.append(QJsonObject{{"id", row.id}, {"level", row.level}, {"book", row.isBook}, {"uses", row.uses}, {"same", row.sameAsPrevious}});
        object.insert("anvil", anvil);
    }
    if (page->hasTool(Tool::Banner))
        object.insert("banner", BannerDesign::encode({page->bannerBaseColor(), page->bannerVersion(), page->bannerLayers()}));
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}
/// 把 @p bytes 摆回去：只摆已搭出来的那几片；给了 @p only 就只摆那一片（它刚搭出来时）。
inline bool restore(ToolboxPage *page, const QByteArray &bytes, std::optional<ToolboxPage::Tool> only = {}) {
    using Tool = ToolboxPage::Tool;
    if (bytes.size() > 256 * 1024) return false;
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(bytes, &parse);
    const auto object = doc.object();
    if (parse.error != QJsonParseError::NoError || !doc.isObject() || object.value("schemaVersion") != QJsonValue(1)
        || !object.value("fields").isObject()) return false;
    const auto wants = [page, only](Tool tool) { return only ? *only == tool : page->hasTool(tool); };
    QWidget *scope = only ? page->findChild<QWidget *>(ToolboxPage::toolPageName(*only)) : page;
    if (scope == nullptr) return false;
    const auto fields = object.value("fields").toObject();
    for (auto *widget : scope->findChildren<QWidget *>()) {
        if (!input(widget) || !fields.contains(widget->objectName())) continue;
        const auto value = fields.value(widget->objectName());
        const QSignalBlocker blocker(widget);
        if (auto *edit = qobject_cast<QLineEdit *>(widget)) { if (value.isString()) edit->setText(value.toString().left(4096)); }
        else if (auto *text = qobject_cast<QPlainTextEdit *>(widget)) { if (value.isString()) text->setPlainText(value.toString().left(32768)); }
        else if (auto *combo = qobject_cast<QComboBox *>(widget)) {
            if (value.isDouble() && value.toInt(-1) >= 0 && value.toInt() < combo->count()) combo->setCurrentIndex(value.toInt());
        } else if (auto *spin = qobject_cast<QSpinBox *>(widget)) { if (value.isDouble()) spin->setValue(value.toInt()); }
        else if (auto *number = qobject_cast<QDoubleSpinBox *>(widget)) { if (value.isDouble() && std::isfinite(value.toDouble())) number->setValue(value.toDouble()); }
        else if (auto *check = qobject_cast<QCheckBox *>(widget)) { if (value.isBool()) check->setChecked(value.toBool()); }
    }
    BannerDesign::Design design; QString error;
    // Re-run the existing presentation rule after blocked field restoration; no network/file action.
    if (wants(Tool::Trim)) page->restoreTrimEquipment(fields.contains(QStringLiteral("trimArmorSlot0")));
    if (wants(Tool::Banner) && BannerDesign::decode(QJsonDocument(object.value("banner").toObject()).toJson(), &design, &error))
        page->setBannerDesign(design.base, design.layers, design.version);
    if (wants(Tool::Beacon)) {
        QList<int> colors;
        for (const auto &value : object.value("beam").toArray())
            if (value.isDouble() && value.toInt(-1) >= 0 && value.toInt() < 16 && colors.size() < 64) colors.append(value.toInt());
        page->setBeamLayers(colors);
    }
    if (wants(Tool::Anvil) && object.value("anvil").isArray()) {
        QList<ToolboxPage::AnvilRow> rows;
        for (const auto &value : object.value("anvil").toArray()) {
            const auto row = value.toObject();
            if (!row.value("id").isString() || rows.size() >= 64) continue;
            rows.append({row.value("id").toString(), qBound(1, row.value("level").toInt(1), 9), row.value("book").toBool(true),
                qBound(0, row.value("uses").toInt(), 9), row.value("same").toBool()});
        }
        page->setAnvilRows(rows);
    }
    return true;
}
inline void write(QSettings &settings, ToolboxPage *page) {
    const bool enabled = page->findChild<QCheckBox *>(QStringLiteral("toolboxRememberInputs"))->isChecked();
    settings.setValue(QStringLiteral("Toolbox/RememberInputs"), enabled);
    if (enabled) settings.setValue(key(), capture(page, settings.value(key()).toByteArray())); else settings.remove(key());
}
/// 把 @p root 底下那些输入框接到存盘计时器上（改了 700 ms 后写一次）。
inline void hookInputs(QWidget *root, QTimer *timer) {
    const auto changed = [timer] { timer->start(); };
    for (auto *widget : root->findChildren<QWidget *>()) {
        if (!input(widget)) continue;
        if (auto *edit = qobject_cast<QLineEdit *>(widget)) QObject::connect(edit, &QLineEdit::textEdited, timer, changed);
        else if (auto *text = qobject_cast<QPlainTextEdit *>(widget)) QObject::connect(text, &QPlainTextEdit::textChanged, timer, changed);
        else if (auto *combo = qobject_cast<QComboBox *>(widget)) QObject::connect(combo, &QComboBox::currentIndexChanged, timer, changed);
        else if (auto *spin = qobject_cast<QSpinBox *>(widget)) QObject::connect(spin, &QSpinBox::valueChanged, timer, changed);
        else if (auto *number = qobject_cast<QDoubleSpinBox *>(widget)) QObject::connect(number, &QDoubleSpinBox::valueChanged, timer, changed);
        else if (auto *check = qobject_cast<QCheckBox *>(widget)) QObject::connect(check, &QCheckBox::toggled, timer, changed);
    }
}
inline QTimer *saveTimer(ToolboxPage *page) { return page->findChild<QTimer *>(QStringLiteral("toolboxInputSaveTimer"), Qt::FindDirectChildrenOnly); }
/// 页面级的那一半：「记住输入」开关、存盘计时器。各件工具的输入等它搭出来再由 `restoreTool()` 接。
inline void attach(ToolboxPage *page) {
    auto *remember = page->findChild<QCheckBox *>(QStringLiteral("toolboxRememberInputs"));
    QSettings settings;
    remember->setChecked(settings.value(QStringLiteral("Toolbox/RememberInputs"), true).toBool());
    if (remember->isChecked()) restore(page, settings.value(key()).toByteArray());
    const auto save = [page] {
        QSettings current;
        write(current, page);
    };
    auto *timer = new QTimer(page); timer->setSingleShot(true); timer->setInterval(700);
    timer->setObjectName(QStringLiteral("toolboxInputSaveTimer"));
    QObject::connect(timer, &QTimer::timeout, page, save);
    const auto changed = [timer] { timer->start(); };
    hookInputs(page, timer);
    // Dynamic table controls and list edits are saved too; no need to hook their short-lived cells.
    QObject::connect(page, &ToolboxPage::bannerRequested, timer, changed);
    QObject::connect(page, &ToolboxPage::beamRequested, timer, changed);
    QObject::connect(page, &ToolboxPage::anvilPlanRequested, timer, changed);
    QObject::connect(remember, &QCheckBox::toggled, page, [save](bool enabled) {
        QSettings().setValue(QStringLiteral("Toolbox/RememberInputs"), enabled); save();
    });
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, page, save);
}
/// @p tool 那一片刚搭出来（controller 灌完下拉表之后调）：摆回记住的输入，并接上存盘。
inline void restoreTool(ToolboxPage *page, ToolboxPage::Tool tool) {
    QTimer *timer = saveTimer(page);
    QWidget *root = page->findChild<QWidget *>(ToolboxPage::toolPageName(tool));
    if (timer == nullptr || root == nullptr) return;
    if (page->findChild<QCheckBox *>(QStringLiteral("toolboxRememberInputs"))->isChecked())
        restore(page, QSettings().value(key()).toByteArray(), tool);
    hookInputs(root, timer);
}
}
