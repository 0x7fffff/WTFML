#include "Support.hpp"

// Prompt automation belongs to the test fixture, not every assertion translation unit.
namespace {
void waitForPrompt(int generation, std::function<void()> action) {
    auto *timer = new QTimer(qApp);
    QElapsedTimer clock;
    clock.start();
    const auto attempt = [generation, action = std::move(action), timer, clock] {
        if (generation != g_modalHandlerGeneration || clock.elapsed() > 20000) {
            timer->stop();
            timer->deleteLater();
            if (generation == g_modalHandlerGeneration)
                check(false, "提示探针在20秒内未发现提示，拒绝无限等待");
            return;
        }
        if (currentPrompt() == nullptr) return;
        timer->stop();
        timer->deleteLater();
        action();
    };
    QObject::connect(timer, &QTimer::timeout, timer, attempt);
    timer->start(25);
    QTimer::singleShot(0, timer, attempt);
}
}

void runPromptProbeChecks() {
    const QString previousTitle = g_modalTitle;
    const QString previousText = g_modalText;
    const bool previousWarning = g_modalWasWarning;
    QMainWindow pageWindow, promptWindow;
    auto *pageAnchor = new QWidget(&pageWindow);
    auto *promptAnchor = new QWidget(&promptWindow);
    pageWindow.setCentralWidget(pageAnchor);
    promptWindow.setCentralWidget(promptAnchor);
    auto *pageHost = new OverlayHost(pageAnchor, &pageWindow);
    auto *promptHost = new OverlayHost(promptAnchor, &promptWindow);
    pageWindow.resize(640, 480);
    promptWindow.resize(640, 480);
    pageWindow.show();
    promptWindow.show();
    pageHost->openPage(QStringLiteral("probe-existing-page"));
    QEventLoop loop;
    QTimer guard;
    guard.setSingleShot(true);
    bool expired = false;
    QObject::connect(&guard, &QTimer::timeout, &loop, [&] {
        expired = true;
        if (auto *box = promptHost->findChild<InlineMessageBox *>()) box->reject();
        loop.quit();
    });
    dismissNextPrompt();
    // The old 0/300ms pair expires before this prompt even exists. Another
    // window's page must not hide the actual prompt from the automation either.
    QTimer::singleShot(450, &promptWindow, [&] {
        InlineMessage::warning(promptHost, QStringLiteral("probe-delayed-prompt"),
                               QStringLiteral("probe-body"));
        loop.quit();
    });
    guard.start(2000);
    loop.exec();
    guard.stop();
    check(!expired && g_modalTitle == QStringLiteral("probe-delayed-prompt") &&
              g_modalText == QStringLiteral("probe-body") && g_modalWasWarning && pageHost->pageOpen(),
          "多窗口提示探针能关闭晚于300毫秒出现的提示，且保留另一窗口的页面");
    g_modalTitle = previousTitle;
    g_modalText = previousText;
    g_modalWasWarning = previousWarning;
}

int topLevelWindowCount() {
    int n = 0;
    for (QWidget *w : QApplication::topLevelWidgets()) {
        if (w->isWindow() && w->isVisible()) {
            ++n;
        }
    }
    return n;
}

OverlayHost *currentOverlay() {
    for (QWidget *w : QApplication::allWidgets()) {
        if (auto *host = qobject_cast<OverlayHost *>(w)) {
            if (host->isOpen()) {
                return host;
            }
        }
    }
    return nullptr;
}

OverlayHost *currentPageOverlay() {
    OverlayHost *host = currentOverlay();
    return host != nullptr && host->pageOpen() ? host : nullptr;
}

InlineMessageBox *currentPrompt() {
    // A different window can have a settings page open. Do not stop at that
    // first open overlay and miss the actual prompt in another window.
    for (QWidget *widget : QApplication::allWidgets()) {
        auto *host = qobject_cast<OverlayHost *>(widget);
        if (host == nullptr || !host->promptOpen()) continue;
        for (auto *box : host->findChildren<InlineMessageBox *>()) {
            if (!box->isHidden()) return box;
        }
    }
    return nullptr;
}

void recordPrompt(InlineMessageBox *box) {
    g_modalTitle.clear();
    g_modalText.clear();
    g_modalWasWarning = false;
    if (box == nullptr) {
        return;
    }
    if (auto *title = box->findChild<QLabel *>(InlineMessageBox::titleLabelName())) {
        g_modalTitle = title->text();
    }
    if (auto *text = box->findChild<QLabel *>(InlineMessageBox::textLabelName())) {
        g_modalText = text->text();
    }
    // 这一格量的是"这是出错，还是一句报信"（`MessageKind::Warning`），只影响标题的颜色。
    g_modalWasWarning = box->kind() == MessageKind::Warning;
}

void clickPromptButton(int index) {
    InlineMessageBox *box = currentPrompt();
    // 框不在了就**什么都不做** —— 包括不把上一次记下来的话抹掉。
    // `recordPrompt(nullptr)` 会先把 `g_modal*` 清空（它要给"真的没弹东西"一个空记录），
    // 所以**落空的**那一发不能顺手调它：`answerNextPrompt()` 那一族除了排 0 毫秒那一发，
    // 还会排一发 300 毫秒的兜底（防"框还没摆上来"），而兜底那一发是在**答案已经点完、
    // 框已经收掉之后**才醒的（它只按 `g_modalHandlerGeneration` 认过期，而那一格要到
    // `withPage` 的收尾才 ++，正好晚于 act() 里那一发 `processEvents()`）。于是
    // "答得够快 + 后面那一节干得够慢（>300 毫秒）"时，真正记下来的那一句话会被这一发
    // 抹成空 —— 断言读到的就是"什么都没弹"，而盘上文件明明装进去了（实测：
    // T30.7 三次里红一次，红的正是这一句）。
    if (box == nullptr) {
        return;
    }
    recordPrompt(box);
    if (auto *button = box->findChild<QPushButton *>(InlineMessageBox::buttonName(index))) {
        button->click();
        return;
    }
    box->close();
}

void dismissNextPrompt() {
    g_modalTitle.clear();
    g_modalText.clear();
    g_modalWasWarning = false;
    const int gen = ++g_modalHandlerGeneration;
    const auto dismiss = [gen] {
        if (gen != g_modalHandlerGeneration) {
            return; // 这一发过期了（后面某一段已经排了新的）
        }
        InlineMessageBox *box = currentPrompt();
        if (box == nullptr) {
            recordPrompt(nullptr);
            return;
        }
        int last = 0;
        while (box->findChild<QPushButton *>(InlineMessageBox::buttonName(last + 1)) != nullptr) {
            ++last;
        }
        clickPromptButton(last);
    };
    waitForPrompt(gen, dismiss);
}

void answerNextPrompt(int index) {
    g_modalTitle.clear();
    g_modalText.clear();
    g_modalWasWarning = false;
    const int gen = ++g_modalHandlerGeneration;
    const auto answer = [index, gen] {
        if (gen != g_modalHandlerGeneration) {
            return; // 这一发过期了（理由见 g_modalHandlerGeneration）
        }
        clickPromptButton(index);
    };
    waitForPrompt(gen, answer);
}

void answerStopGamePrompt() {
    g_modalTitle.clear();
    g_modalText.clear();
    g_modalWasWarning = false;
    const int gen = ++g_modalHandlerGeneration;
    const auto answer = [gen] {
        if (gen != g_modalHandlerGeneration) {
            return; // 这一发过期了（理由见 g_modalHandlerGeneration）
        }
        InlineMessageBox *box = currentPrompt();
        recordPrompt(box);
        if (box == nullptr) {
            return;
        }
        auto *stop = box->findChild<QPushButton *>(InlineMessageBox::buttonName(0));
        auto *cancel = box->findChild<QPushButton *>(InlineMessageBox::buttonName(1));
        if (stop != nullptr && cancel != nullptr &&
            stop->text() == QStringLiteral("停止游戏") &&
            cancel->text() == QStringLiteral("取消")) {
            stop->click();
            return;
        }
        box->close();  // 按钮表对不上：至少别让测试挂在这儿（断言会从"没停掉"那边红）
    };
    QTimer::singleShot(0, answer);
    QTimer::singleShot(300, answer);
}

void closeInstanceSettingsPage(QString *instanceOut) {
    // 三样**先清掉**（和上面那几个助手一样）：它们记的是"这一发弹了什么"，不清的话
    // 断言读到的是上一次调用留下的旧值 —— 那正是"多弹了一个"这条断言恒真的来路。
    g_modalTitle.clear();
    g_modalText.clear();
    g_modalWasWarning = false;
    ++g_modalHandlerGeneration;

    // **调用顺序变了**：实例设置那一页现在走 `OverlayHost::showPage()` —— 摆上去就返回
    // （`openInstanceSettings()` 不再卡在嵌套事件循环里）。所以这个助手不再是"先排两发
    // 定时器等循环来派"，而是**点完那一下之后再调**：先等那一页真的摆上来（有些入口
    // 把动作排到下一轮，比如底栏面板那颗齿轮），读一眼页眉，再按返回。
    OverlayHost *host = nullptr;
    const bool opened = pumpUntil(
        [&host] {
            host = currentPageOverlay();
            return host != nullptr;
        },
        3000);
    if (!opened) {
        return;  // 这一页压根没开起来：断言会从页眉那一条红，这里别崩
    }
    if (auto *title = host->findChild<QLabel *>(OverlayHost::overlayTitleLabelName())) {
        *instanceOut = title->text();
    } else {
        // 不是那一页：按一条提示记下来（断言会看到"多弹了一个"）。
        recordPrompt(host->findChild<InlineMessageBox *>());
    }
    if (auto *back = host->findChild<QPushButton *>(OverlayHost::overlayBackButtonName())) {
        back->click();  // 返回箭头 = reject（见 OverlayHost::showPage）
    } else {
        host->closeOverlay();  // 兜底：至少别让测试卡在这一层上
    }
    // 收页是同步的，但那一页的 `deleteLater()` 要等事件循环真的处理那条 DeferredDelete
    // —— `processEvents()` 在测试里够不着那一档（嵌套层数对不上）。不收掉的话，下一条
    // 再开时 `findChild<InstanceSettingsDialog *>()` 会摸到**这一份已经收起来的**。
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void answerJavaMismatchDialog(const QString &buttonName, bool checkDontRemind) {
    g_modalTitle.clear();
    g_modalText.clear();
    g_modalWasWarning = false;
    g_mismatchLabel.clear();
    const int gen = ++g_modalHandlerGeneration;
    const auto handle = [buttonName, checkDontRemind, gen] {
        if (gen != g_modalHandlerGeneration) {
            return; // 这一发过期了（理由见 g_modalHandlerGeneration）
        }
        OverlayHost *host = currentOverlay();
        if (host == nullptr) {
            return;
        }
        auto *label = host->findChild<QLabel *>(JavaMismatchDialog::textLabelName());
        if (label == nullptr) {
            // 不是那个三岔口：按一条提示记下来（断言会看到"多弹了一个"）。
            InlineMessageBox *box = host->findChild<InlineMessageBox *>();
            recordPrompt(box);
            if (box != nullptr) {
                box->close();
            } else {
                host->closeOverlay();
            }
            return;
        }
        g_mismatchLabel = label->text();
        if (checkDontRemind) {
            if (auto *check =
                    host->findChild<QCheckBox *>(JavaMismatchDialog::dontRemindCheckName())) {
                check->setChecked(true);
            }
        }
        if (auto *button = host->findChild<QPushButton *>(buttonName)) {
            button->click();
            return;
        }
        host->closeOverlay();
    };
    QTimer::singleShot(0, handle);
    // 兜底那一条的理由和 dismissNextPrompt() 一样。
    //
    // 点启动那一下的失败（进程起不来）也会弹一条提示，而那条提示排在**三岔口之后** ——
    // 单发定时器很容易在"三岔口已经收了、提示还没摆上来"的那段空档里醒过来，
    // 什么都不做就走了，于是那条提示没人收、测试挂死在嵌套事件循环里。
    // 所以这里**盯着看**：每 50 毫秒看一眼，直到后面某个助手换掉这一发（gen 变了）
    // 或者 15 秒到头。
    auto *watcher = new QTimer;
    watcher->setInterval(50);
    QObject::connect(watcher, &QTimer::timeout, watcher,
                     [watcher, gen, handle, ticks = 0]() mutable {
                         if (gen != g_modalHandlerGeneration || ++ticks > 300) {
                             watcher->stop();
                             watcher->deleteLater();
                             return;
                         }
                         handle();
                     });
    watcher->start();
}
