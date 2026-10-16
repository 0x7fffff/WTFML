#include "controllers/ThemeSettingsController.hpp"

#include "MainWindowUI.hpp"
#include "Animations.hpp"
#include "InlineMessage.hpp"
#include "TitleBar.hpp"
#include "settings/LauncherSettings.hpp"
#include "util/ThemeImage.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QStandardPaths>

namespace {

QColor semanticColor(const Theme::Spec &spec, int index) {
    const QPalette palette = QApplication::palette();
    switch (index) {
    case 0: return spec.danger.isValid() ? spec.danger : Theme::dangerColor(palette);
    case 1: return spec.warning.isValid() ? spec.warning : Theme::warningColor(palette);
    case 2: return spec.ok.isValid() ? spec.ok : Theme::okColor(palette);
    case 3: return spec.errorText.isValid() ? spec.errorText : Theme::errorTextColor(palette);
    default: return {};
    }
}

void setSemanticColor(Theme::Spec *spec, int index, const QColor &color) {
    if (spec == nullptr) return;
    switch (index) {
    case 0: spec->danger = color; break;
    case 1: spec->warning = color; break;
    case 2: spec->ok = color; break;
    case 3: spec->errorText = color; break;
    default: break;
    }
}

}  // namespace

ThemeSettingsController::ThemeSettingsController(MainWindowUI *ui, LauncherSettings *settings,
                                                 QWidget *dialogParent, QObject *parent)
    : QObject(parent), m_ui(ui), m_settings(settings), m_dialogParent(dialogParent) {}

void ThemeSettingsController::chooseImage(int index) {
    const MainWindowUI::ThemeImageRow &row = m_ui->getThemeImageRow(index);
    Theme::Part part;
    if (row.choose == nullptr || !Theme::partFromKey(row.partKey, &part)) return;
    const QString picked = QFileDialog::getOpenFileName(
        m_dialogParent, tr("挑一张背景图"),
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
        tr("图片 (*.png *.jpg *.jpeg *.bmp *.webp *.gif);;所有文件 (*)"));
    if (picked.isEmpty()) return;
    QString error;
    const QString stored = ThemeImage::store(row.partKey, picked, &error);
    if (stored.isEmpty()) {
        InlineMessage::warning(m_ui->overlay(), tr("这张图用不了"), error);
        return;
    }
    const Theme::PartImage before = Theme::partImage(part);
    setPartImage(row.partKey, stored, QFileInfo(picked).fileName(), int(before.fit),
                 before.opacity);
}

void ThemeSettingsController::clearImage(int index) {
    const MainWindowUI::ThemeImageRow &row = m_ui->getThemeImageRow(index);
    if (row.clear == nullptr || row.partKey.isEmpty()) return;
    ThemeImage::remove(row.partKey);
    setPartImage(row.partKey, {}, {}, int(Theme::ImageFit::Cover), 100);
}

void ThemeSettingsController::changeImageFit(int index) {
    const MainWindowUI::ThemeImageRow &row = m_ui->getThemeImageRow(index);
    Theme::Part part;
    if (row.fit == nullptr || !Theme::partFromKey(row.partKey, &part)) return;
    const Theme::PartImage before = Theme::partImage(part);
    if (!before.isSet()) return;
    Theme::ImageFit fit = before.fit;
    Theme::imageFitFromKey(row.fit->currentData().toString(), &fit);
    setPartImage(row.partKey, before.path, before.name, int(fit), before.opacity);
}

void ThemeSettingsController::changeImageOpacity(int index) {
    const MainWindowUI::ThemeImageRow &row = m_ui->getThemeImageRow(index);
    Theme::Part part;
    if (row.opacity == nullptr || !Theme::partFromKey(row.partKey, &part)) return;
    const Theme::PartImage before = Theme::partImage(part);
    if (before.isSet())
        setPartImage(row.partKey, before.path, before.name, int(before.fit), row.opacity->value());
}

void ThemeSettingsController::setPartImage(const QString &partKey, const QString &imagePath,
                                           const QString &imageName, int fit, int opacity) {
    Theme::Part part;
    if (!Theme::partFromKey(partKey, &part)) return;
    Theme::Spec spec = Theme::current();
    Theme::PartStyle style = spec.parts.value(int(part));
    style.imagePath = imagePath;
    style.hasImage = !imagePath.isEmpty();
    style.imageName = imagePath.isEmpty() ? QString() : imageName;
    style.fit = Theme::ImageFit(fit);
    style.imageOpacity = opacity;
    if (!style.hasImage && !style.hasBackground && !style.hasForeground && style.textOpacity < 0)
        spec.parts.remove(int(part));
    else
        spec.parts.insert(int(part), style);
    applySpec(spec);
}

void ThemeSettingsController::applySpec(const Theme::Spec &spec) {
    Theme::setSpec(spec);
    Theme::save(Theme::current());
    if (qApp != nullptr) Theme::apply(*qApp);
    refresh();
}

void ThemeSettingsController::refreshImages() {
    for (int i = 0; i < MainWindowUI::themeImageRowCount(); ++i) {
        const MainWindowUI::ThemeImageRow &row = m_ui->getThemeImageRow(i);
        Theme::Part part;
        if (row.state == nullptr || !Theme::partFromKey(row.partKey, &part)) continue;
        const Theme::PartImage image = Theme::partImage(part);
        const bool has = image.isSet();
        const QSignalBlocker blockFit(row.fit);
        const QSignalBlocker blockOpacity(row.opacity);
        if (has) {
            const int fitIndex = row.fit->findData(Theme::imageFitKey(image.fit));
            if (fitIndex >= 0) row.fit->setCurrentIndex(fitIndex);
        }
        row.opacity->setValue(image.opacity);
        row.percent->setText(MainWindowUI::themeImagePercentText(image.opacity));
        row.state->setText(has ? tr("现在铺的是：%1").arg(
                                     image.name.isEmpty() ? QFileInfo(image.path).fileName()
                                                          : image.name)
                               : MainWindowUI::themeImageNoneText());
        row.fit->setEnabled(has);
        row.opacity->setEnabled(has);
        row.percent->setEnabled(has);
        row.clear->setEnabled(has);
    }
}

void ThemeSettingsController::refresh() {
    const Theme::Spec &spec = Theme::current();
    if (QComboBox *combo = m_ui->getThemePresetCombo()) {
        const QSignalBlocker block(combo);
        const int index = combo->findData(spec.presetId);
        if (index >= 0) combo->setCurrentIndex(index);
    }
    if (QPushButton *accent = m_ui->getThemeAccentButton())
        Theme::markColorSwatch(accent, spec.accent);
    if (QSlider *slider = m_ui->getThemeTextOpacitySlider()) {
        const QSignalBlocker block(slider);
        slider->setValue(spec.textOpacity);
        if (QLabel *percent = m_ui->getThemeTextOpacityPercent())
            percent->setText(MainWindowUI::themePercentText(spec.textOpacity));
    }
    for (int i = 0; i < MainWindowUI::themeSemanticCount(); ++i)
        if (QPushButton *swatch = m_ui->getThemeSemanticButton(i))
            Theme::markColorSwatch(swatch, semanticColor(spec, i));
    if (QCheckBox *check = m_ui->getAnimationsCheck()) {
        const QSignalBlocker block(check);
        check->setChecked(Anim::userEnabled());
    }
    refreshImages();
}

void ThemeSettingsController::changePreset() {
    QComboBox *combo = m_ui->getThemePresetCombo();
    if (combo == nullptr || !Theme::isPresetId(combo->currentData().toString())) return;
    Theme::Spec spec = Theme::current();
    spec.presetId = combo->currentData().toString();
    spec.accent = Theme::tokens(spec.presetId).primary;
    applySpec(spec);
}

void ThemeSettingsController::chooseAccent() {
    const QColor picked = InlineMessage::pickColor(
        m_ui->overlay(), tr("主题色"),
        tr("顶栏、侧栏选中项那根竖条、确认性按钮（启动 / 下载 / 搜索那些）都用它。"),
        Theme::current().accent);
    if (!picked.isValid()) return;
    Theme::Spec spec = Theme::current();
    spec.accent = picked;
    applySpec(spec);
}

void ThemeSettingsController::resetAccent() {
    Theme::Spec spec = Theme::current();
    spec.accent = Theme::tokens(spec.presetId).primary;
    applySpec(spec);
}

void ThemeSettingsController::chooseSemantic(int index) {
    const Theme::Spec before = Theme::current();
    const QColor picked = InlineMessage::pickColor(
        m_ui->overlay(), MainWindowUI::themeSemanticText(index),
        tr("这一处红 / 黄 / 绿在界面上是哪儿（见下面那行说明）。"),
        semanticColor(before, index));
    if (!picked.isValid()) return;
    Theme::Spec spec = before;
    setSemanticColor(&spec, index, picked);
    applySpec(spec);
}

void ThemeSettingsController::resetSemantic() {
    Theme::Spec spec = Theme::current();
    spec.danger = spec.warning = spec.ok = spec.errorText = QColor();
    applySpec(spec);
}

void ThemeSettingsController::resetAll() {
    if (!InlineMessage::question(
            m_ui->overlay(), tr("恢复全部默认"),
            tr("配色、主题色、文字不透明度、四个语义色、以及这两块背景图，都会回到出厂那一份。\n"
               "你自己改的那些不会留着。"),
            tr("恢复"), tr("取消"), false, true)) return;
    for (int i = 0; i < MainWindowUI::themeImageRowCount(); ++i) {
        const QString key = m_ui->getThemeImageRow(i).partKey;
        if (!key.isEmpty()) ThemeImage::remove(key);
    }
    applySpec(Theme::defaultSpec());
}

void ThemeSettingsController::exportTheme() {
    const QString suggested = QDir(QStandardPaths::writableLocation(
                                       QStandardPaths::DocumentsLocation))
                                  .filePath(QStringLiteral("wtfml-theme.json"));
    const QString target = QFileDialog::getSaveFileName(
        m_dialogParent, tr("导出主题"), suggested, tr("主题文件 (*.json);;所有文件 (*)"));
    if (target.isEmpty()) return;
    QFile file(target);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        InlineMessage::warning(m_ui->overlay(), tr("导出失败"),
                               tr("写不进这个文件：\n%1\n\n%2")
                                   .arg(QDir::toNativeSeparators(target), file.errorString()));
        return;
    }
    const QByteArray json = QJsonDocument(Theme::specToJson(Theme::current()))
                                .toJson(QJsonDocument::Indented);
    if (file.write(json) != json.size()) {
        InlineMessage::warning(m_ui->overlay(), tr("导出失败"),
                               tr("写到一半写不进去了：\n%1").arg(file.errorString()));
        return;
    }
    file.close();
    InlineMessage::information(m_ui->overlay(), tr("导出完成"),
                               tr("主题已导出到：\n%1").arg(QDir::toNativeSeparators(target)));
}

void ThemeSettingsController::importTheme() {
    const QString source = QFileDialog::getOpenFileName(
        m_dialogParent, tr("导入主题"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        tr("主题文件 (*.json);;所有文件 (*)"));
    if (source.isEmpty()) return;
    QFile file(source);
    if (!file.open(QIODevice::ReadOnly)) {
        InlineMessage::warning(m_ui->overlay(), tr("导入失败"),
                               tr("读不了这个文件：\n%1\n\n%2")
                                   .arg(QDir::toNativeSeparators(source), file.errorString()));
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    Theme::Spec spec;
    QString error;
    if (document.isNull() || !document.isObject()
        || !Theme::specFromJson(document.object(), &spec, &error)) {
        const QString why = document.isNull() || !document.isObject()
                                ? parseError.errorString() : error;
        InlineMessage::warning(m_ui->overlay(), tr("导入失败"),
                               tr("这不是一份主题文件：\n%1\n\n%2")
                                   .arg(QDir::toNativeSeparators(source), why));
        return;
    }
    applySpec(spec);
    QStringList missing;
    for (int i = 0; i < MainWindowUI::themeImageRowCount(); ++i) {
        Theme::Part part;
        if (!Theme::partFromKey(m_ui->getThemeImageRow(i).partKey, &part)) continue;
        const Theme::PartImage image = Theme::partImage(part);
        if (image.isSet() && !QFileInfo::exists(image.path)) missing.append(Theme::partName(part));
    }
    InlineMessage::information(
        m_ui->overlay(), tr("导入完成"),
        missing.isEmpty() ? tr("这份主题已经铺上了。")
                          : tr("颜色都铺上了，但这些背景图在这台机器上找不到（主题文件里只有路径，不含图）：\n%1")
                                .arg(missing.join(QLatin1Char('\n'))));
}

void ThemeSettingsController::changeTextOpacity(int value) {
    Theme::Spec spec = Theme::current();
    spec.textOpacity = value;
    applySpec(spec);
}

void ThemeSettingsController::setAnimationsEnabled(bool enabled) {
    Anim::setEnabled(enabled);
    Anim::save();
}

void ThemeSettingsController::updateTitleText() {
    if (m_settings == nullptr) return;
    m_settings->setTitleText(m_ui->getTitleTextEdit()->text());
    m_ui->titleBar()->setBrandText(m_settings->titleText());
}
