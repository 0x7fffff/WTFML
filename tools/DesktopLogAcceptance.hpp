#pragma once
// Native production log widgets/controller with controlled output, not a game launch.
#include "controllers/GameSessionController.hpp"
#include "GameLogWindow.hpp"
#include "util/SecretRedactor.hpp"
#include "util/ErrorReport.hpp"
#include <QElapsedTimer>
#include <QPlainTextEdit>
#include <QPointer>
#include <memory>
#include <QDateTime>

namespace DesktopLogAcceptance {
inline void start(QApplication &app, MainWindow &main, const QString &root) {
    auto *controller = main.findChild<GameSessionController *>();
    if (!controller) { app.exit(2); return; }
    const QString secret = QStringLiteral("__MC_SECURITY_TEST_ABC123456789__");
    SecretRedactor::remember(secret);
    auto *one = controller->ensureGameLogWindow(9101, true);
    auto *two = controller->ensureGameLogWindow(9102, true);
    if (!one || !two) { app.exit(2); return; }
    one->setWindowTitle("Controlled instance A output");
    two->setWindowTitle("Controlled instance B output");
    one->resize(900, 600); two->resize(760, 520);
    auto *timer = new QTimer(&main);
    auto *pulse = new QTimer(&main);
    auto clock = std::make_shared<QElapsedTimer>(); clock->start();
    auto last = std::make_shared<qint64>(0);
    auto gap = std::make_shared<qint64>(0);
    auto ticks = std::make_shared<int>(0);
    auto batch = std::make_shared<int>(0);
    pulse->setInterval(25);
    QObject::connect(pulse, &QTimer::timeout, &main, [=] {
        const auto now = clock->elapsed(); *gap = qMax(*gap, now - *last); *last = now; ++*ticks;
    });
    timer->setInterval(20);
    QObject::connect(timer, &QTimer::timeout, &main, [=, &app, &main] {
        if (*batch < 100) {
            QString a, b;
            for (int i = 0; i < 250; ++i) {
                const int row = *batch * 250 + i;
                const auto level = row % 10 ? QStringLiteral("INFO") : QStringLiteral("ERROR");
                a += QString("[12:00:00] [Render thread/%1]: ONLY_A row=%2 token=%3\n").arg(level).arg(row).arg(secret);
                b += QString("[12:00:00] [Render thread/%1]: ONLY_B row=%2 token=%3\n").arg(level).arg(row).arg(secret);
            }
            controller->onProcessOutput(9101, a);
            controller->onProcessError(9102, b);
            if (*batch % 20 == 0) main.ui()->getTabWidget()->setCurrentIndex((*batch / 20) % 4);
            ++*batch;
            return;
        }
        timer->stop(); pulse->stop();
        QTimer::singleShot(300, &main, [=, &app, &main] {
            QJsonArray checks;
            auto check = [&](bool ok, const QString &name) { checks.append(QJsonObject{{"name",name},{"ok",ok}}); };
            const auto a = one->allLogText(), b = two->allLogText();
            check(a.contains("ONLY_A row=24999") && !a.contains("ONLY_B"), "session A isolation and final row");
            check(b.contains("ONLY_B row=24999") && !b.contains("ONLY_A"), "session B isolation and final row");
            check(!a.contains(secret) && !b.contains(secret) && a.contains("<redacted>"), "both native log models redact credentials");
            check(one->lineCount() <= GameLogWindow::maxLogBlocks() && two->lineCount() <= GameLogWindow::maxLogBlocks(), "bounded models");
            check(one->droppedLineCount() > 0 && two->droppedLineCount() > 0, "overflow recorded");
            check(*ticks > 5 && *gap < 1000, "event loop heartbeat under 1000ms");
            one->setLevelVisible(GameLogWindow::LogLevel::Info, false);
            check(!one->logText().contains("thread/INFO") && one->allLogText().contains("thread/INFO"), "filter does not lose export data");
            ErrorReport::Facts facts; facts.instanceName="Controlled instance A";
            const auto reportText = ErrorReport::text(facts, QDateTime::currentDateTime(), a, controller->sessionLogTail(9101));
            check(!reportText.contains(secret), "error report remains redacted");
            auto save = [&](const QString &name, const QString &value) {
                QFile file(root+'/'+name); const auto bytes=value.toUtf8();
                return file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size();
            };
            check(save("instance-A.log",a) && save("instance-B.log",b) && save("error-report.txt",reportText), "text exports saved");
            check(one->grab().save(root+"/log-A.png") && two->grab().save(root+"/log-B.png") && main.grab().save(root+"/launcher.png"), "native screenshots saved");
            bool ok=true; for (const auto &entry:checks) ok &= entry.toObject().value("ok").toBool();
            const QJsonObject result{{"scope","controlled output through real session controller and native log windows; no game process"},
                {"devicePixelRatio",main.devicePixelRatioF()},{"rowsPerSession",25000},{"elapsedMs",clock->elapsed()},
                {"maxHeartbeatGapMs",*gap},{"heartbeatTicks",*ticks},{"checks",checks}};
            ok &= save("report.json",QString::fromUtf8(QJsonDocument(result).toJson()));
            controller->closeAllGameLogWindows();
            app.exit(ok ? 0 : 1);
        });
    });
    pulse->start(); timer->start();
    QTimer::singleShot(90000,&app,[&app]{app.exit(124);});
}
}
