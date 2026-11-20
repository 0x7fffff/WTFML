#pragma once
// 主页启动卡片（#65）样张：三主题 × 四态（补全文件 / 等待游戏窗口 / 成功 / 失败），真窗口 1280×800。
// 状态全走真的任务账本（ActivityCenterController → MainWindow::syncLaunchCard → 卡片），不直接摆界面。
// 另出两条逐帧对照：W 循环跨过一圈的接缝、失败时铺满再转灰。开发工具，不进启动器。
#include "BrandMark.hpp"
#include "DesktopDesignShots.hpp"
#include "MainWindow.hpp"
#include "MainWindowUI.hpp"
#include "Theme.hpp"
#include "controllers/ActivityCenterController.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>

namespace LaunchCardShots {

inline QList<Tasks::TaskStep> launchSteps(int doneCount) {
    const char *names[] = {"校验登录状态", "检查启动文件", "准备 Java 运行环境", "等待游戏启动", "完成"};
    QList<Tasks::TaskStep> steps;
    for (int i = 0; i < 5; ++i)
        steps.append({QString::fromUtf8(names[i]),
                      i < doneCount ? Tasks::TaskStepState::Done
                                    : i == doneCount ? Tasks::TaskStepState::Running : Tasks::TaskStepState::Pending,
                      QString()});
    return steps;
}

/// 逐帧对照：@p times 每一刻的 W（放大 3 倍）排成一行，底下标时间。
inline QImage strip(const QString &title, const QList<qreal> &times, const std::function<void(LaunchMark &, qreal)> &pose) {
    const QSize cell(228, 156);
    const int labelH = 26, titleH = 34, pad = 12;
    const Theme::Tokens t = Theme::tokens();
    QImage image(pad + times.size() * (cell.width() + pad), titleH + cell.height() + labelH + pad, QImage::Format_ARGB32_Premultiplied);
    image.fill(t.page);
    QPainter p(&image);
    p.setPen(t.text);
    QFont f = p.font();
    f.setPixelSize(15);
    p.setFont(f);
    p.drawText(QRect(pad, 0, image.width() - pad, titleH), Qt::AlignVCenter | Qt::AlignLeft, title);
    for (int i = 0; i < times.size(); ++i) {
        LaunchMark mark;
        mark.resize(cell);
        pose(mark, times.at(i));
        const QPoint at(pad + i * (cell.width() + pad), titleH);
        p.fillRect(QRect(at, cell), t.subtle);
        p.drawImage(at, mark.grab().toImage());
        p.setPen(t.secondary);
        p.drawText(QRect(at.x(), at.y() + cell.height(), cell.width(), labelH), Qt::AlignCenter,
                   QStringLiteral("t = %1 s").arg(times.at(i), 0, 'f', 2));
    }
    return image;
}

inline void frameStrips(const QString &root, const QString &theme) {
    // 一圈 2.80 s：墨段中心在 t = 2.80 回到起笔。前后各 0.4 s 看它从收笔走出去、同时从起笔进来。
    QList<qreal> seam;
    for (int i = 0; i <= 10; ++i) seam << 2.40 + i * 0.08;
    strip(QStringLiteral("W 循环接缝（一圈 2.80 s，墨段跨过收笔 → 起笔）"), seam, [](LaunchMark &m, qreal t) {
        m.startWriting();
        m.setFrozenTime(t);
    }).save(root + QStringLiteral("/frames-seam-%1.png").arg(theme));
    // 失败：书写到 4.10 s（第二圈中途）时失败，0.25 s 铺满，再 0.45 s 转灰。
    const QList<qreal> after{0.0, 0.05, 0.10, 0.15, 0.20, 0.25, 0.40, 0.55, 0.70};
    QList<qreal> failTimes;
    for (qreal d : after) failTimes << 4.10 + d;
    strip(QStringLiteral("失败：4.10 s 进入完成，0.25 s 铺满、0.70 s 转灰完成"), failTimes, [](LaunchMark &m, qreal t) {
        m.startWriting();
        m.setFrozenTime(4.10);
        m.finish(false);
        m.setFrozenTime(t);
    }).save(root + QStringLiteral("/frames-fail-%1.png").arg(theme));
    strip(QStringLiteral("成功：4.10 s 进入完成，0.25 s 铺满后停住（不转灰）"), failTimes, [](LaunchMark &m, qreal t) {
        m.startWriting();
        m.setFrozenTime(4.10);
        m.finish(true);
        m.setFrozenTime(t);
    }).save(root + QStringLiteral("/frames-success-%1.png").arg(theme));
}

inline void run(QApplication &app, MainWindow &window, const QString &root) {
    struct Shot { QString theme, state; };
    static QList<Shot> shots;
    for (const QString &theme : {QStringLiteral("dark"), QStringLiteral("emerald"), QStringLiteral("ocean")})
        for (const char *state : {"files", "waiting", "success", "failed"}) shots.append({theme, QLatin1String(state)});
    static int index = 0;
    static Tasks::TaskId lastTask = Tasks::invalidTaskId();
    static QJsonArray report;
    static std::function<void()> step;
    auto *activity = window.findChild<ActivityCenterController *>();
    step = [&app, &window, root, activity]() {
        if (index >= shots.size()) {
            for (const QString &theme : {QStringLiteral("dark"), QStringLiteral("emerald"), QStringLiteral("ocean")}) {
                Theme::setSpec(Theme::presetSpec(theme));
                Theme::apply(app);
                frameStrips(root, theme);
            }
            QFile out(root + QStringLiteral("/launch-card-report.json"));
            if (out.open(QIODevice::WriteOnly)) out.write(QJsonDocument(report).toJson());
            app.exit(0);
            return;
        }
        const Shot shot = shots.at(index++);
        Theme::setSpec(Theme::presetSpec(shot.theme));
        Theme::apply(app);
        window.showNormal();
        window.resize(1280, 800);
        window.ui()->getTabWidget()->setCurrentIndex(MainWindowUI::homeTabIndex());
        DesignShots::seedHome(window);
        QTimer::singleShot(300, &window, [&window]() { window.resize(1280, 800); });
        QTimer::singleShot(700, &window, [&window]() { DesignShots::seedHome(window); });
        QTimer::singleShot(900, &window, [&window, activity, shot]() {
            const QString key = QStringLiteral("spring");
            const QString title = QStringLiteral("启动 春日生存");
            if (shot.state == QStringLiteral("files")) {
                lastTask = activity->begin(Tasks::TaskKind::Launch, title, key, launchSteps(1));
                activity->tasks()->setProgress(lastTask, 312, 1024);
            } else if (shot.state == QStringLiteral("waiting")) {
                lastTask = activity->begin(Tasks::TaskKind::Launch, title, key, launchSteps(3));
            } else if (shot.state == QStringLiteral("success")) {
                lastTask = activity->begin(Tasks::TaskKind::Launch, title, key, launchSteps(3));
                activity->finish(lastTask, true, QString(), false);
            } else {
                lastTask = activity->begin(Tasks::TaskKind::Launch, title, key, launchSteps(2));
                activity->finish(lastTask, false, QStringLiteral("找不到可用的 Java 21"), false);
            }
        });
        // 成功态停 1.5 s 就还原：0.8 s 时抓（W 已铺满）；失败等转灰走完；加载态任意一刻。
        const int grabAt = shot.state == QStringLiteral("success") ? 900 + 800 : 900 + 1100;
        QTimer::singleShot(grabAt, &window, [&window, root, shot]() {
            const QString file = QStringLiteral("%1-%2-1280x800.png").arg(shot.state, shot.theme);
            const bool saved = window.grab().save(root + QLatin1Char('/') + file);
            report.append(QJsonObject{{"file", file}, {"saved", saved}, {"cardShown", window.ui()->launchCardShown()},
                                      {"width", window.width()}, {"height", window.height()}});
            // 收掉这一项（还在跑的按「用户取消」收：没有原因，卡片还原），下一张从平常的启动区起。
            auto *activity = window.findChild<ActivityCenterController *>();
            if (Tasks::isActiveState(activity->tasks()->snapshot(lastTask).state))
                activity->finish(lastTask, false, QString(), false);
            QTimer::singleShot(1800, &window, [] { step(); });
        });
    };
    QTimer::singleShot(1500, &window, [] { step(); });
}

}  // namespace LaunchCardShots
