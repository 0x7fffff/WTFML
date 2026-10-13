/**
 * @file NewServerPage.hpp
 * @brief 「新建服务端」那一页（coding plan §6.2 / 里程碑 M4）：挑服务端类型、游戏版本、
 *        加载器/构建版本、名称、内存，确认之后建这个服务端。
 * @author 0x7fffff
 * @date 2026-10-13
 *
 * 【这一页只摆控件，判据一条都不在这儿】
 *
 * `ui/` **不 include `src/`**（见 `GroupDialog.hpp` 文件头那段老规矩）。所以：
 *
 *   * **哪八档、每一档叫什么** —— 由 `MainWindow` 从 `ServerInstall::allFlavors()` 那几张表
 *     翻好，当作 `FlavorOption` 喂进来（`setFlavors()`）；
 *   * **每一档要填哪几格**（Forge/NeoForge 要加载器版本、Fabric 还要安装器版本…）—— 同样
 *     由那一层填在 `FlavorOption` 的四个 `needs*` 上，这一页只负责照着露/收那几行；
 *   * **"这一档要下什么、装完是什么样"那句说明** —— 由那一层现算（`ServerInstall::planFor()`）
 *     之后用 `setSummary()` 摆进来。
 *
 * 这样这一页做到最后只有一个"输入表单"的职责：用户填了什么，原样报出去。
 *
 * 【它和一个普通确认框的区别】
 *
 * 用户填的每一格都**真的会变成盘上那个服务端**（名字变目录名、档和版本变成启动命令）——
 * 所以它在覆盖层里**整页**出现（`OverlayHost::execPage()`），而不是开一个小框。填到哪一格
 * 才知道下一步要填什么（类型一变，下面那几行跟着露/收），挤在小框里做不了这件事。
 *
 * NOTE: 这一页**不联网、不碰盘**：它 `accept()` 之后才轮到 `MainWindow` 去建目录、去下载。
 *       所以验证工程能离线把它整页驱动一遍（填字段 → 读回来）。
 */

#pragma once

#include <QDialog>
#include <QList>
#include <QString>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

class NewServerPage : public QDialog {
    Q_OBJECT

public:
    /// 下拉框里的一档。**全是显示用的字符串 + 四堵"这一档要填哪几格"**（都由翻译点算好）。
    struct FlavorOption {
        QString token;  ///< 身份（`ServerInstall::flavorToken()`）—— **认人用它，不认文字**
        QString name;   ///< 显示名（`ServerInstall::flavorName()`）
        QString note;   ///< 悬停说明（`ServerInstall::flavorNote()`）
        bool needsLoader = false;     ///< 要「加载器版本」那一格（Forge / NeoForge / Fabric / Quilt）
        bool needsInstaller = false;  ///< 要「安装器版本」那一格（Fabric / Quilt）
        bool needsBuild = false;      ///< 要「构建号」那一格（Paper / Purpur）
        bool needsLoaderGame = false; ///< 要「游戏版本」那一格（八档都要，留着这个口子）
    };

    explicit NewServerPage(QWidget *parent = nullptr);

    /// 覆盖层页眉上那行字（`OverlayHost::execPage` 的标题）。
    static QString windowTitle() { return QStringLiteral("新建服务端"); }

    /// 摆那八档（**每次开页都重新喂**：同一页对象会被复用）。@p defaultToken 是起手停在哪一档。
    void setFlavors(const QList<FlavorOption> &flavors, const QString &defaultToken);
    /// 起手填什么（名称默认 `Server<版本>`、游戏版本默认当前选中的那一个）。
    void setDefaults(const QString &name, const QString &gameVersion);

    /// 那一句计划说明 + 底下「创建」那颗能不能点。**由 `MainWindow` 现算现喂**（用户每改一格
    /// 就叫一次，见 `fieldsChanged`）。
    /// @param problem 非空 = 现在这一份填法还不成立，把那句话（而不是计划说明）摆出来。
    void setSummary(const QString &text, const QString &problem);

    // ---------- 读回用户填的（`accept()` 之后）----------
    QString serverName() const;
    QString flavorToken() const;
    QString gameVersion() const;
    QString loaderVersion() const;
    QString installerVersion() const;
    QString buildId() const;
    int memoryMb() const;

signals:
    /// 用户改了任何一格（换档 / 打字 / 拨内存）。`MainWindow` 接它现算计划说明。
    void fieldsChanged();

private:
    /// 按当前选中那一档，露/收那几行，并把 `setSummary()` 的旧话清掉（等翻译点重新喂）。
    void applyFlavorVisibility();

    QLineEdit *m_nameEdit = nullptr;
    QComboBox *m_flavorCombo = nullptr;
    QLineEdit *m_gameVersionEdit = nullptr;
    QLineEdit *m_loaderEdit = nullptr;
    QLineEdit *m_installerEdit = nullptr;
    QLineEdit *m_buildEdit = nullptr;
    QSpinBox *m_memorySpin = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QPushButton *m_createButton = nullptr;

    /// 那三行"按档露/收"的行（整行一起露/收）。
    QWidget *m_loaderRow = nullptr;
    QWidget *m_installerRow = nullptr;
    QWidget *m_buildRow = nullptr;
    /// 下拉框里那八档（下标 = `QComboBox` 的 `currentIndex()`）。
    QList<FlavorOption> m_flavors;
};
