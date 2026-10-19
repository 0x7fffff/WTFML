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

namespace ToolboxInputState {
inline QString key() { return QStringLiteral("Toolbox/RecentInputsV1"); }
inline bool input(QWidget *widget) {
    if (widget->objectName().isEmpty() || widget->objectName().startsWith(QStringLiteral("qt_"))
        || widget->objectName() == QStringLiteral("toolboxRememberInputs")) return false;
    for (auto *parent = widget->parentWidget(); parent; parent = parent->parentWidget())
        if (qobject_cast<QTableWidget *>(parent)) return false; // Table rows have a separate stable representation.
    if (auto *edit = qobject_cast<QLineEdit *>(widget)) return !edit->isReadOnly() && edit->echoMode() == QLineEdit::Normal;
    if (auto *edit = qobject_cast<QPlainTextEdit *>(widget)) return !edit->isReadOnly();
    return qobject_cast<QComboBox *>(widget) || qobject_cast<QSpinBox *>(widget)
        || qobject_cast<QDoubleSpinBox *>(widget) || qobject_cast<QCheckBox *>(widget);
}
inline QByteArray capture(ToolboxPage *page) {
    QJsonObject fields;
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
    QJsonArray beam, anvil;
    auto *list = page->findChild<QListWidget *>(QStringLiteral("beamGlassList"));
    for (int i = 0; i < qMin(list->count(), 64); ++i) beam.append(list->item(i)->data(Qt::UserRole).toInt());
    for (const auto &row : page->anvilRows().mid(0, 64))
        anvil.append(QJsonObject{{"id", row.id}, {"level", row.level}, {"book", row.isBook}, {"uses", row.uses}, {"same", row.sameAsPrevious}});
    return QJsonDocument(QJsonObject{{"schemaVersion", 1}, {"fields", fields}, {"beam", beam}, {"anvil", anvil},
        {"banner", BannerDesign::encode({page->bannerBaseColor(), page->bannerVersion(), page->bannerLayers()})}}).toJson(QJsonDocument::Compact);
}
inline bool restore(ToolboxPage *page, const QByteArray &bytes) {
    if (bytes.size() > 256 * 1024) return false;
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(bytes, &parse);
    const auto object = doc.object();
    if (parse.error != QJsonParseError::NoError || !doc.isObject() || object.value("schemaVersion") != QJsonValue(1)
        || !object.value("fields").isObject()) return false;
    const auto fields = object.value("fields").toObject();
    for (auto *widget : page->findChildren<QWidget *>()) {
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
    page->restoreTrimEquipment(fields.contains(QStringLiteral("trimArmorSlot0")));
    if (BannerDesign::decode(QJsonDocument(object.value("banner").toObject()).toJson(), &design, &error))
        page->setBannerDesign(design.base, design.layers, design.version);
    QList<int> colors;
    for (const auto &value : object.value("beam").toArray())
        if (value.isDouble() && value.toInt(-1) >= 0 && value.toInt() < 16 && colors.size() < 64) colors.append(value.toInt());
    page->setBeamLayers(colors);
    QList<ToolboxPage::AnvilRow> rows;
    for (const auto &value : object.value("anvil").toArray()) {
        const auto row = value.toObject();
        if (!row.value("id").isString() || rows.size() >= 64) continue;
        rows.append({row.value("id").toString(), qBound(1, row.value("level").toInt(1), 9), row.value("book").toBool(true),
            qBound(0, row.value("uses").toInt(), 9), row.value("same").toBool()});
    }
    if (object.value("anvil").isArray()) page->setAnvilRows(rows);
    return true;
}
inline void write(QSettings &settings, ToolboxPage *page) {
    const bool enabled = page->findChild<QCheckBox *>(QStringLiteral("toolboxRememberInputs"))->isChecked();
    settings.setValue(QStringLiteral("Toolbox/RememberInputs"), enabled);
    if (enabled) settings.setValue(key(), capture(page)); else settings.remove(key());
}
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
    QObject::connect(timer, &QTimer::timeout, page, save);
    const auto changed = [timer] { timer->start(); };
    for (auto *widget : page->findChildren<QWidget *>()) {
        if (!input(widget)) continue;
        if (auto *edit = qobject_cast<QLineEdit *>(widget)) QObject::connect(edit, &QLineEdit::textEdited, timer, changed);
        else if (auto *text = qobject_cast<QPlainTextEdit *>(widget)) QObject::connect(text, &QPlainTextEdit::textChanged, timer, changed);
        else if (auto *combo = qobject_cast<QComboBox *>(widget)) QObject::connect(combo, &QComboBox::currentIndexChanged, timer, changed);
        else if (auto *spin = qobject_cast<QSpinBox *>(widget)) QObject::connect(spin, &QSpinBox::valueChanged, timer, changed);
        else if (auto *number = qobject_cast<QDoubleSpinBox *>(widget)) QObject::connect(number, &QDoubleSpinBox::valueChanged, timer, changed);
        else if (auto *check = qobject_cast<QCheckBox *>(widget)) QObject::connect(check, &QCheckBox::toggled, timer, changed);
    }
    // Dynamic table controls and list edits are saved too; no need to hook their short-lived cells.
    QObject::connect(page, &ToolboxPage::bannerRequested, timer, changed);
    QObject::connect(page, &ToolboxPage::beamRequested, timer, changed);
    QObject::connect(page, &ToolboxPage::anvilPlanRequested, timer, changed);
    QObject::connect(remember, &QCheckBox::toggled, page, [save](bool enabled) {
        QSettings().setValue(QStringLiteral("Toolbox/RememberInputs"), enabled); save();
    });
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, page, save);
}
}
