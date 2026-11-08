/**
 * @file PresetInstallPicker.hpp
 * @brief 安装时的预设选择：左侧分类、右侧候选、底部最终来源。
 * @note 只维护本次选择。读取、兼容检查及安装仍由控制器负责。
 */
#pragma once
#include <QWidget>
#include <QIcon>
#include <QList>
#include <QMap>
#include <QPair>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <functional>
class QListWidget;
class QLabel;
class QPushButton;
class QStackedWidget;
class QCheckBox;
class QDialog;
class QPlainTextEdit;

class PresetInstallPicker : public QWidget {
    Q_OBJECT
public:
    struct Option {
        QString id, name;
        bool available = true;
        bool composite = false;
        int itemCount = -1;
        QIcon icon = {};
    };
    struct Category {
        QString key, title;
        QList<Option> options;
        QString selectedId, status;
        QStringList reasons;
        bool problem = false;
    };
    struct Composite {
        QString id, name;
        QMap<QString, QString> parts;
        QIcon icon = {};
        int itemCount = -1;
        QMap<QString, int> counts = {};
    };
    explicit PresetInstallPicker(QWidget *parent = nullptr);
    void setContext(const QString &gameVersion, const QString &loader);
    void setCategories(const QList<Category> &categories);
    void setComposites(const QList<Composite> &presets);
    void setStatus(const QString &key, const QString &status, const QStringList &reasons,
                   bool problem, int itemCount = -1, int missing = -1, int unknown = -1);
    QString selectedId(const QString &key) const;
    QList<QPair<QString, QString>> selections() const;
    int selectedCount() const;
    int currentTab() const;
    void setCurrentTab(int index);
    static QString contextLabelName() { return QStringLiteral("presetInstallContext"); }
    static QString tabBarName() { return QStringLiteral("presetInstallNavigation"); }
    static QString listName(int i) { return QStringLiteral("presetInstallList%1").arg(i); }
    static QString statusName(int i) { return QStringLiteral("presetInstallStatus%1").arg(i); }
    static QString reasonsButtonName(int i) { return QStringLiteral("presetInstallReasons%1").arg(i); }
    static QString reasonsLabelName(int i) { return QStringLiteral("presetInstallReasonsText%1").arg(i); }
    static QString emptyLabelName(int i) { return QStringLiteral("presetInstallEmpty%1").arg(i); }
    static QString manageButtonName(int i) { return QStringLiteral("presetInstallManage%1").arg(i); }
    static QString noneText() { return QStringLiteral("不使用"); }
    static QString manageText() { return QStringLiteral("管理预设"); }
    static QString reasonsText() { return QStringLiteral("兼容性详情"); }
signals:
    void selectionChanged(const QString &categoryKey, const QString &presetId);
    void manageRequested();
    void pendingChanged(bool pending);
private:
    struct Page {
        QListWidget *list = nullptr;
        QPushButton *details = nullptr;
        QLabel *source = nullptr;
        QString status;
        QStringList reasons;
        int missing = -1, unknown = -1;
    };
    QWidget *buildPage(int index, const Category &category);
    int indexOfKey(const QString &key) const;
    QString nameFor(const QString &key, const QString &id) const;
    void selectId(const QString &key, const QString &id);
    void requestSelection(int index, const QString &id);
    void confirmReplacement(const QString &text, std::function<void()> action);
    void updateSummary();
    void updateCompositeChecks();
    void openDetails(int index);
    void updateDetails();
    QListWidget *m_navigation = nullptr;
    QStackedWidget *m_stack = nullptr;
    QLabel *m_summaryTitle = nullptr, *m_pendingLabel = nullptr;
    QWidget *m_summary = nullptr, *m_pendingBar = nullptr, *m_compositePage = nullptr;
    QListWidget *m_compositeList = nullptr;
    QMap<QString, QCheckBox *> m_compositeChecks;
    QList<Category> m_categories;
    QList<Page> m_pages;
    QList<Composite> m_composites;
    QString m_selectedComposite;
    QSet<QString> m_userTouched;
    std::function<void()> m_pendingSelection;
    QPointer<QDialog> m_details;
    QPlainTextEdit *m_detailsText = nullptr;
    int m_detailsIndex = -1;
    bool m_modsAllowed = false;
};
