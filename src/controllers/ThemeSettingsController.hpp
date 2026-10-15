#pragma once

#include <QObject>

#include "Theme.hpp"

class MainWindowUI;
class LauncherSettings;
class QWidget;

class ThemeSettingsController final : public QObject {
    Q_OBJECT

public:
    ThemeSettingsController(MainWindowUI *ui, LauncherSettings *settings, QWidget *dialogParent,
                            QObject *parent = nullptr);

    void chooseImage(int index);
    void clearImage(int index);
    void changeImageFit(int index);
    void changeImageOpacity(int index);
    void changePreset();
    void chooseAccent();
    void resetAccent();
    void chooseSemantic(int index);
    void resetSemantic();
    void resetAll();
    void exportTheme();
    void importTheme();
    void changeTextOpacity(int value);
    void setAnimationsEnabled(bool enabled);
    void updateTitleText();
    void refresh();
    void applySpec(const Theme::Spec &spec);
    void setPartImage(const QString &partKey, const QString &imagePath,
                      const QString &imageName, int fit, int opacity);

private:
    void refreshImages();

    MainWindowUI *m_ui = nullptr;
    LauncherSettings *m_settings = nullptr;
    QWidget *m_dialogParent = nullptr;
};
