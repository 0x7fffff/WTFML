#pragma once

#include <QObject>

#include "Theme.hpp"

class MainWindowUI;
class LauncherSettings;
class QTimer;
class QWidget;

class ThemeSettingsController final : public QObject {
    Q_OBJECT

public:
    ThemeSettingsController(MainWindowUI *ui, LauncherSettings *settings, QWidget *dialogParent,
                            QObject *parent = nullptr);
    /// 节流窗口里还攒着一次没落盘的话，关掉之前补上（见 `applySpecThrottled()`）。
    ~ThemeSettingsController() override;

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
    void refresh();
    void applySpec(const Theme::Spec &spec);
    void setPartImage(const QString &partKey, const QString &imagePath,
                      const QString &imageName, int fit, int opacity);

private:
    void refreshImages();
    void setPartImageImpl(const QString &partKey, const QString &imagePath,
                          const QString &imageName, int fit, int opacity, bool throttled);
    /// 拖滑块时用的一档：内存里立刻跟上，落盘 + 重铺整个应用最多每
    /// `kApplyThrottleMs` 一次（见 .cpp 里那段）。
    void applySpecThrottled(const Theme::Spec &spec);
    /// 把节流窗口里攒着的那一发补上。
    void flushPendingSpec();

    MainWindowUI *m_ui = nullptr;
    LauncherSettings *m_settings = nullptr;
    QWidget *m_dialogParent = nullptr;
    QTimer *m_applyThrottle = nullptr;
    bool m_applyPending = false;
};
