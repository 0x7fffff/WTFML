#pragma once

#include <QObject>
#include <functional>

class FolderManager;
class LauncherManager;
class MainWindowUI;
class OverlayHost;
class QWidget;

class FolderController final : public QObject {
    Q_OBJECT
public:
    struct Host {
        std::function<QString(const QString &)> title;
        std::function<void()> refreshList;
        std::function<void()> refreshInstances;
        std::function<void(const QString &)> log;
    };
    FolderController(FolderManager *folders, LauncherManager *launcher, MainWindowUI *ui,
                     QWidget *dialogParent, Host host, QObject *parent = nullptr);

    void add();
    void removeFromList(const QString &folder);
    void action(const QString &path, int action);
    void refresh(const QString &path);
    void rename(const QString &path);
    void open(const QString &path);
    void removeFromDisk(const QString &path);

private:
    void log(const QString &text) const;
    FolderManager *m_folders = nullptr;
    LauncherManager *m_launcher = nullptr;
    MainWindowUI *m_ui = nullptr;
    QWidget *m_dialogParent = nullptr;
    Host m_host;
};
