#pragma once
// 页面对照截图（--page-shots）：设置、活动中心、任务详情三态、账号与外观、游戏日志、对话框。
// 和 DesktopDesignShots 一样只截图：任务是写进活动中心的夹具，日志是灌进窗口的假文本，不启动游戏。
#include "DesktopDesignShots.hpp"
#include "AccountSkinDialog.hpp"
#include "GameLogWindow.hpp"
#include "InlineMessage.hpp"
#include "JavaMismatchDialog.hpp"
#include "OverlayHost.hpp"
#include "controllers/ActivityCenterController.hpp"
#include "controllers/CrashReportController.hpp"
#include "controllers/TaskDetailController.hpp"
#include <QListWidget>
#include <QPointer>
#include <QTimer>

namespace PageShots {

struct Fixture {
    QString crash, running, failed;
};

inline Fixture seed(MainWindow &window) {
    Fixture f;
    auto *activity = window.findChild<ActivityCenterController *>();
    auto *crash = window.findChild<CrashReportController *>();
    if (!activity) return f;
    const QList<Tasks::TaskStep> install{
        {"下载版本清单", Tasks::TaskStepState::Done, "0.4 s"},
        {"下载客户端 1.21.4.jar", Tasks::TaskStepState::Done, "25.6 MB"},
        {"下载库文件", Tasks::TaskStepState::Running, "128 / 342"},
        {"下载资源文件", Tasks::TaskStepState::Pending, {}},
        {"安装 Fabric Loader 0.16.9", Tasks::TaskStepState::Pending, {}}};
    const auto running = activity->begin(Tasks::TaskKind::Install, QStringLiteral("安装 Fabric 1.21.4"), "spring", install);
    activity->tasks()->setProgress(running, 37, 100);
    activity->tasks()->setDetail(running, QStringLiteral("春日生存 · 库文件 128 / 342"));
    const QList<Tasks::TaskStep> download{
        {"下载文件", Tasks::TaskStepState::Done, "12.0 MB"},
        {"校验并保存", Tasks::TaskStepState::Running, {}}};
    const auto failed = activity->begin(Tasks::TaskKind::Download, QStringLiteral("下载光影包 Complementary"), {}, download);
    activity->tasks()->setDetail(failed, QStringLiteral("ComplementaryReimagined_r5.2.2.zip"));
    activity->tasks()->setProgress(failed, 100, 100);
    activity->finish(failed, false, QStringLiteral("校验失败：SHA1 不一致"), false);
    f.running = QString::number(running);
    f.failed = QString::number(failed);
    if (crash) {
        CrashReportController::LaunchFacts facts;
        facts.instanceId = "create";
        facts.instanceName = QStringLiteral("机械动力 Create");
        facts.requiredJavaMajor = 17;
        facts.loaderName = "Forge";
        facts.loaderVersion = "47.3.0";
        f.crash = QString::number(crash->report(0, int(0xC0000005), true, facts, {}, {}));
    }
    return f;
}

inline QString logFixture() {
    return QStringLiteral(
        "[14:02:10] [main/INFO]: [net.minecraftforge.fml.loading.moddiscovery.ModDiscoverer/SCAN]: Found mod file create-1.20.1-0.5.1.f.jar of type MOD\n"
        "[14:02:10] [main/INFO]: [net.minecraftforge.fml.loading.moddiscovery.ModDiscoverer/SCAN]: Found mod file jei-1.20.1-forge-15.3.0.4.jar of type MOD\n"
        "[14:02:10] [main/DEBUG]: [cpw.mods.modlauncher.LaunchServiceHandler/MODLAUNCHER]: Found launch services [fmlclientdev,forgeclient]\n"
        "[14:02:11] [main/INFO]: [mixin/]: SpongePowered MIXIN Subsystem Version=0.8.5 Source=union:/D:/Games/.minecraft/libraries/org/spongepowered/mixin/0.8.5/mixin-0.8.5.jar\n"
        "[14:02:11] [main/INFO]: [mixin/]: Compatibility level set to JAVA_17\n"
        "[14:02:11] [main/INFO]: [cpw.mods.modlauncher.Launcher/MODLAUNCHER]: ModLauncher running: args [--username, Wanderer, --version, 1.20.1-forge-47.3.0]\n"
        "[14:02:13] [main/INFO]: [net.minecraftforge.fml.earlydisplay.DisplayWindow/]: Trying GL version 4.6\n"
        "[14:02:15] [main/WARN]: [mixin/]: Reference map 'xaerominimap.refmap.json' for xaerominimap.mixins.json could not be read.\n"
        "[14:02:19] [Render thread/INFO]: [com.simibubi.create.Create/]: Create 0.5.1.f initializing! Commit hash: 2bb7a4d\n"
        "[14:02:22] [Render thread/WARN]: [net.minecraftforge.registries.GameData/REGISTRIES]: Registry minecraft:menu: The object steam_rails:conductor_whistle has been registered twice\n"
        "[14:02:31] [Render thread/INFO]: [net.minecraft.server.packs.resources.ReloadableResourceManager/]: Reloading ResourceManager: vanilla, mod_resources\n"
        "[14:02:38] [Render thread/INFO]: [com.mojang.blaze3d.audio.Library/]: OpenAL initialized on device OpenAL Soft\n"
        "[14:02:44] [Render thread/INFO]: [net.minecraft.client.Minecraft/]: Connecting to localhost, 25565\n"
        "[14:03:02] [Render thread/INFO]: [net.minecraft.client.gui.components.ChatComponent/]: [CHAT] Wanderer joined the game\n"
        "[14:03:05] [Render thread/WARN]: [net.minecraft.client.multiplayer.ClientPacketListener/]: Unknown custom packet identifier: create:schematics\n"
        "[14:03:09] [Render thread/ERROR]: [net.minecraft.client.renderer.texture.TextureAtlas/]: Missing texture create:block/cogwheel_shaft\n"
        "[14:03:17] [Render thread/INFO]: [net.minecraft.client.gui.components.ChatComponent/]: [CHAT] [Wanderer: Set own game mode to Creative Mode]\n");
}

struct Shot {
    QString file;
    QString theme;
    std::function<QWidget *(MainWindow &, const Fixture &)> setup;
};

inline QWidget *settings(MainWindow &window, int row) {
    window.ui()->getTabWidget()->setCurrentIndex(MainWindowUI::settingsTabIndex());
    if (auto *list = window.findChild<QListWidget *>(MainWindowUI::settingsCategoryListName())) list->setCurrentRow(row);
    return &window;
}

inline QWidget *task(MainWindow &window, const QString &key) {
    window.ui()->getTabWidget()->setCurrentIndex(MainWindowUI::homeTabIndex());
    if (auto *detail = window.findChild<TaskDetailController *>()) detail->open(key);
    return &window;
}

/// 提示是同步的（嵌一层事件循环），所以下一拍再开，开着的时候截、截完 evict 收掉。
inline QWidget *prompt(MainWindow &window, std::function<void(OverlayHost *)> ask) {
    window.ui()->getTabWidget()->setCurrentIndex(MainWindowUI::homeTabIndex());
    DesignShots::seedHome(window);
    QTimer::singleShot(0, &window, [&window, ask] { ask(window.ui()->overlay()); });
    return &window;
}

inline QWidget *gameLog(const QString &text, bool filterEmpty) {
    auto *log = new GameLogWindow(QStringLiteral("机械动力 Create"));
    log->setAttribute(Qt::WA_DeleteOnClose);
    log->appendText(text);
    if (filterEmpty) {
        log->setLevelVisible(GameLogWindow::LogLevel::Info, false);
        log->getSearchEdit()->setText(QStringLiteral("blockentity"));
    }
    log->show();
    log->resize(1280, 800);
    return log;
}

inline QList<Shot> shots() {
    QList<Shot> list;
    for (const QString theme : {QStringLiteral("dark"), QStringLiteral("emerald"), QStringLiteral("ocean")}) {
        const bool dark = theme == QLatin1String("dark");
        list.append({"settings-" + theme + ".png", theme, [](MainWindow &w, const Fixture &) { return settings(w, 3); }});
        // 整窗衔接自检（轨、顶栏、内容区是不是同一张底）：管理、下载、智能体三页原样截一张。
        for (const auto &[name, index] : {std::pair{"manage", MainWindowUI::instancesTabIndex()},
                                          std::pair{"download", MainWindowUI::downloadTabIndex()},
                                          std::pair{"agent", MainWindowUI::agentTabIndex()}}) {
            list.append({QStringLiteral("seam-%1-").arg(QLatin1String(name)) + theme + ".png", theme,
                         [index](MainWindow &w, const Fixture &) {
                             w.ui()->getTabWidget()->setCurrentIndex(index);
                             return &w;
                         }});
        }
        list.append({"task-crash-" + theme + ".png", theme, [](MainWindow &w, const Fixture &f) { return task(w, f.crash); }});
        if (dark) {
            list.append({"settings-launch-dark.png", theme, [](MainWindow &w, const Fixture &) { return settings(w, 0); }});
            list.append({"task-running-dark.png", theme, [](MainWindow &w, const Fixture &f) { return task(w, f.running); }});
            list.append({"task-failed-dark.png", theme, [](MainWindow &w, const Fixture &f) { return task(w, f.failed); }});
            list.append({"panels-tasks-dark.png", theme, [](MainWindow &w, const Fixture &) {
                w.ui()->getTabWidget()->setCurrentIndex(MainWindowUI::homeTabIndex());
                w.ui()->getTaskSummaryButton()->click();
                return &w;
            }});
        }
        list.append({"account-" + theme + ".png", theme, [](MainWindow &w, const Fixture &) {
            w.ui()->getTabWidget()->setCurrentIndex(MainWindowUI::homeTabIndex());
            QMetaObject::invokeMethod(&w, "onAccountSkinButtonClicked", Qt::QueuedConnection);
            return &w;
        }});
        if (dark) {  // 设计稿那一态：三个账号、三件披风（夹具数据，只喂这个窗，不写盘）
            list.append({"account-list-dark.png", theme, [](MainWindow &w, const Fixture &) {
                w.ui()->getTabWidget()->setCurrentIndex(MainWindowUI::homeTabIndex());
                QMetaObject::invokeMethod(&w, "onAccountSkinButtonClicked", Qt::QueuedConnection);
                QTimer::singleShot(600, &w, [&w] {
                    auto *dialog = w.findChild<AccountSkinDialog *>();
                    if (!dialog) return;
                    dialog->setAccounts({{"a", "Wanderer", "2026-11-09T10:00:00Z", true, false},
                                         {"b", "Steve_test", {}, false, true},
                                         {"c", "Builder", {}, false, true}});
                    dialog->setCapes({{"migrator", "Migrator", true}, {"vanilla", "Vanilla", false},
                                      {"cherry", "Cherry Blossom", false}});
                });
                return &w;
            }});
        }
        list.append({"gamelog-running-" + theme + ".png", theme, [](MainWindow &, const Fixture &) { return gameLog(logFixture(), false); }});
        if (dark) {
            list.append({"gamelog-empty-dark.png", theme, [](MainWindow &, const Fixture &) { return gameLog(logFixture(), true); }});
        }
        list.append({"dialogs-java-" + theme + ".png", theme, [](MainWindow &w, const Fixture &) {
            return prompt(w, [](OverlayHost *host) {
                JavaMismatchDialog dialog(QStringLiteral("机械动力 Create"), 17, 8, QStringLiteral("1.8.0_421"),
                                          QStringLiteral("C:\\Program Files\\Java\\jre1.8.0_421\\bin\\java.exe"));
                host->execPrompt(&dialog);
            });
        }});
        list.append({"dialogs-discard-" + theme + ".png", theme, [](MainWindow &w, const Fixture &) {
            return prompt(w, [](OverlayHost *host) {
                InlineMessage::question(host, QStringLiteral("改过还没保存"),
                    QStringLiteral("这个文件刚才改的地方还没写到盘上，离开就会丢掉这些改动。\n\n要丢掉吗？"),
                    QStringLiteral("丢掉改动"), QStringLiteral("留在这儿"), false);
            });
        }});
        if (dark) {
            list.append({"dialogs-warn-dark.png", theme, [](MainWindow &w, const Fixture &) {
                return prompt(w, [](OverlayHost *host) {
                    InlineMessage::warning(host, QStringLiteral("收藏未保存"), QStringLiteral("没能写入实例配置，收藏状态下次启动会恢复原样。"));
                });
            }});
            list.append({"dialogs-error-dark.png", theme, [](MainWindow &w, const Fixture &) {
                return prompt(w, [](OverlayHost *host) {
                    InlineMessage::critical(host, QStringLiteral("删除失败"),
                        QStringLiteral("有文件正被占用，没能删除：\nD:\\Games\\.minecraft\\versions\\机械动力 Create\\mods\\create-1.20.1-0.5.1.f.jar"));
                });
            }});
            list.append({"dialogs-input-dark.png", theme, [](MainWindow &w, const Fixture &) {
                return prompt(w, [](OverlayHost *host) {
                    bool ok = false;
                    InlineMessage::getText(host, QStringLiteral("复制实例"), QStringLiteral("副本名称："),
                                           QStringLiteral("机械动力 Create 2"), &ok);
                });
            }});
        }
    }
    return list;
}

inline void run(QApplication &app, MainWindow &window, const QString &root) {
    static const QList<Shot> list = shots();
    static Fixture fixture;
    static int index = 0;
    static QJsonArray report;
    static std::function<void()> step;
    fixture = seed(window);
    step = [&app, &window, root]() {
        if (index >= list.size()) {
            QFile out(root + QStringLiteral("/design-shots.json"));
            const bool ok = out.open(QIODevice::WriteOnly) &&
                            out.write(QJsonDocument(QJsonObject{{"devicePixelRatio", window.devicePixelRatioF()},
                                                                {"shots", report}}).toJson()) > 0;
            app.exit(ok ? 0 : 1);
            return;
        }
        const Shot shot = list.at(index++);
        Theme::setSpec(Theme::presetSpec(shot.theme));
        Theme::apply(app);
        window.showNormal();
        window.resize(1280, 800);
        QPointer<QWidget> target = shot.setup(window, fixture);
        QTimer::singleShot(300, &window, [&window]() { window.resize(1280, 800); });
        QTimer::singleShot(1000, &window, [&window, root, shot, target]() {
            const bool saved = target && target->grab().save(root + QLatin1Char('/') + shot.file);
            report.append(QJsonObject{{"file", shot.file}, {"saved", saved}});
            if (target && target != &window) target->close();
            window.ui()->overlay()->evict();
            if (auto *popup = QApplication::activePopupWidget()) popup->close();
            QTimer::singleShot(200, &window, [&window]() {
                window.ui()->overlay()->evict();
                QTimer::singleShot(150, &window, step);
            });
        });
    };
    QTimer::singleShot(1200, &window, step);
}

}  // namespace PageShots
