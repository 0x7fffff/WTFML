// 加载态组件（ui/LoadingState）：整块 begin / finish / fail 与行内 beginRow / failRow。
// 钉的是规矩本身：150ms 门槛、出错态「重试」接线、重复 begin 不叠层、动效关时照样露面且点不动、
// 行内只重绘登记的那几行、行删了时钟就停。
#include "Support.hpp"
#include "LoadingState.hpp"
#include "VersionTreeDelegate.hpp"

#include <QEventLoop>
#include <QMouseEvent>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

void wait(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

int overlayCount(QWidget *host) {
    return host->findChildren<QWidget *>(QStringLiteral("wtfmlLoadingOverlay"), Qt::FindDirectChildrenOnly).size();
}

/// 数 Show 事件：「150ms 内结束就从没露过面」要的是**一次都没 show 过**，不是结束时恰好藏着。
class ShowCounter : public QObject {
public:
    int shows = 0;

protected:
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::Show) ++shows;
        return false;
    }
};

/// 记下 viewport 每次重绘的区域。
class PaintLog : public QObject {
public:
    QList<QRect> rects;

protected:
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::Paint) rects.append(static_cast<QPaintEvent *>(event)->rect());
        return false;
    }
};

void blockChecks() {
    QWidget window;
    window.resize(600, 400);
    auto *layout = new QVBoxLayout(&window);
    auto *tree = new QTreeWidget(&window);
    tree->setHeaderHidden(true);
    layout->addWidget(tree);
    window.show();
    QApplication::processEvents();

    // ---- 150ms 内结束：从没露过面 ----
    LoadingState::begin(tree, QStringLiteral("正在获取最热门的模组…"));
    QWidget *layer = LoadingState::overlay(tree);
    check(layer != nullptr, "LS1 begin 之后宿主上挂着一层加载态");
    ShowCounter shows;
    if (layer != nullptr) layer->installEventFilter(&shows);
    const QPointer<QWidget> early(layer);
    check(layer != nullptr && !layer->isVisible(), "LS1 刚 begin 时它还藏着（门槛没到）");
    wait(70);
    LoadingState::finish(tree);
    check(LoadingState::overlay(tree) == nullptr, "LS1 150ms 内 finish：这一层立刻撤掉");
    wait(150);
    check(early.isNull() && shows.shows == 0, "★ LS1 150ms 内结束的那一发从头到尾没露过面",
          QString("还在=%1 露面=%2").arg(early.isNull() ? 0 : 1).arg(shows.shows));

    // ---- 过了 150ms：露面，写着那句话 ----
    LoadingState::begin(tree, QStringLiteral("正在搜索「钠」…"));
    layer = LoadingState::overlay(tree);
    wait(260);
    check(layer != nullptr && layer->isVisible() && layer->accessibleName() == QStringLiteral("正在搜索「钠」…") &&
              layer->property("state").toString() == QStringLiteral("loading"),
          "★ LS2 过了 150ms 加载态露面，写着这一发的那句话",
          layer != nullptr ? layer->accessibleName() : QStringLiteral("没有那一层"));
    check(layer != nullptr && layer->geometry() == tree->rect(), "LS2 它铺满宿主（跟着宿主走）");
    check(tree->childAt(tree->rect().center()) != layer, "LS2 加载中不拦鼠标（点得穿）");
    window.resize(700, 460);
    QApplication::processEvents();
    check(layer != nullptr && layer->geometry() == tree->rect(), "LS2 宿主变大，它跟着变大");

    // ---- 重复 begin：不叠第二层，只换文案 ----
    LoadingState::begin(tree, QStringLiteral("第二句"));
    LoadingState::begin(tree, QStringLiteral("第三句"));
    check(overlayCount(tree) == 1 && LoadingState::overlay(tree) == layer &&
              layer->accessibleName() == QStringLiteral("第三句"),
          "★ LS3 重复 begin 不叠第二层，只换成最后那句", QString::number(overlayCount(tree)));
    check(layer != nullptr && layer->isVisible(), "LS3 已经露着的那一层换文案时不先藏起来");

    // ---- 动效开着：点在呼吸（隔一段时间拍两张不一样） ----
    const QImage a = tree->grab().toImage();
    wait(300);
    const QImage b = tree->grab().toImage();
    check(a != b, "LS4 动效开着时三颗点在动（两次截图不一样）");

    // ---- 失败：出错态 + 「重试」，点它 = 调 retry ----
    int retried = 0;
    LoadingState::fail(tree, QStringLiteral("没取到模组列表：连接超时"), [&] {
        ++retried;
        LoadingState::begin(tree, QStringLiteral("正在获取最热门的模组…"));
    });
    layer = LoadingState::overlay(tree);
    auto *retry = layer != nullptr ? layer->findChild<QPushButton *>(LoadingState::retryButtonName()) : nullptr;
    check(layer != nullptr && layer->isVisible() && layer->property("state").toString() == QStringLiteral("error") &&
              layer->accessibleName() == QStringLiteral("没取到模组列表：连接超时"),
          "★ LS5 fail：立刻换成出错态，写着原因");
    check(retry != nullptr && retry->isVisible() && retry->text() == QStringLiteral("重试"),
          "★ LS5 出错态上有一颗「重试」");
    if (retry != nullptr) {
        const QPoint at = retry->mapTo(tree, retry->rect().center());
        check(tree->childAt(at) == retry, "LS5 「重试」点得到（出错态接鼠标）");
        retry->click();
    }
    check(retried == 1, "★ LS5 点「重试」= 调了 retry 一次", QString::number(retried));
    check(overlayCount(tree) == 1 && LoadingState::overlay(tree) != nullptr &&
              LoadingState::overlay(tree)->property("state").toString() == QStringLiteral("loading") &&
              LoadingState::overlay(tree)->isVisible() && retry != nullptr && !retry->isVisible(),
          "LS5 重试里再 begin：同一层直接换成点（不叠层、不闪、「重试」收起）");
    LoadingState::finish(tree);
    wait(Anim::exitMs() + 80);
    check(overlayCount(tree) == 0, "LS6 露过面的那一层 finish 后淡出撤掉", QString::number(overlayCount(tree)));

    // ---- 动效关着：门槛照旧，照样露面，点不动，时钟不空转 ----
    Anim::setEnabled(false);
    LoadingState::begin(tree, QStringLiteral("正在获取…"));
    layer = LoadingState::overlay(tree);
    check(layer != nullptr && !layer->isVisible(), "LS7 动效关：门槛照旧（刚 begin 时还藏着）");
    wait(220);
    check(layer != nullptr && layer->isVisible(), "★ LS7 动效关：过了门槛照样露面");
    const QImage c = tree->grab().toImage();
    wait(300);
    const QImage d = tree->grab().toImage();
    check(c == d, "★ LS7 动效关：三颗点是静的（两次截图一模一样）");
    check(!LoadingState::clockRunning(), "LS7 动效关：露面之后时钟不再空转");
    LoadingState::finish(tree);
    check(LoadingState::overlay(tree) == nullptr, "LS7 动效关：finish 立刻撤掉");
    Anim::setEnabled(true);
    QApplication::processEvents();
}

void inlineChecks() {
    QWidget window;
    window.resize(600, 400);
    auto *layout = new QVBoxLayout(&window);
    auto *tree = new QTreeWidget(&window);
    tree->setHeaderHidden(true);
    tree->setItemDelegate(new VersionTreeDelegate(tree, false));
    layout->addWidget(tree);
    auto *other = new QTreeWidgetItem(tree, {QStringLiteral("Fabric")});
    for (int i = 0; i < 3; ++i) new QTreeWidgetItem(other, {QStringLiteral("0.16.%1").arg(i)});
    auto *parent = new QTreeWidgetItem(tree, {QStringLiteral("Forge")});
    other->setExpanded(true);
    parent->setExpanded(true);
    window.show();
    QApplication::processEvents();

    QTreeWidgetItem *row = LoadingState::beginRow(parent, QStringLiteral("正在获取…"));
    check(row != nullptr && row->parent() == parent && row->isDisabled() &&
              row->text(0) == QStringLiteral("正在获取…"),
          "LI1 beginRow：在那一层底下建一条禁用的占位行，文字照旧");
    check(LoadingState::clockRunning() && LoadingState::tickingRowCount() == 1, "LI1 登记上了，共享时钟在转");

    // ---- 只重绘登记的那一行 ----
    wait(Anim::loadingDelayMs() + 60);
    PaintLog log;
    tree->viewport()->installEventFilter(&log);
    wait(200);
    tree->viewport()->removeEventFilter(&log);
    const QRect rowRect = tree->visualItemRect(row);
    bool onlyRow = !log.rects.isEmpty();
    for (const QRect &r : log.rects) onlyRow = onlyRow && rowRect.contains(r);
    check(onlyRow, "★ LI2 时钟每一跳只重绘登记的那一行，不重绘整棵树",
          QString("重绘 %1 次，行 %2,%3 %4x%5").arg(log.rects.size()).arg(rowRect.x()).arg(rowRect.y())
              .arg(rowRect.width()).arg(rowRect.height()));

    // ---- 行删了：自动注销，时钟停 ----
    delete parent->takeChild(parent->indexOfChild(row));
    wait(60);
    check(LoadingState::tickingRowCount() == 0 && !LoadingState::clockRunning(), "★ LI3 占位行删掉后时钟停了");

    // ---- 出错行：「重试」那一格点中 = 调 retry ----
    int retried = 0;
    QTreeWidgetItem *failed = LoadingState::beginRow(parent);
    LoadingState::failRow(failed, QStringLiteral("列表没取到"), [&] {
        ++retried;
        delete parent->takeChild(parent->indexOfChild(failed));
        LoadingState::beginRow(parent);
    });
    check(failed->text(0) == QStringLiteral("列表没取到") && failed->isDisabled(), "LI4 failRow：同一行改写成原因，仍是灰的");
    check(LoadingState::tickingRowCount() == 0, "LI4 出错行不再按时钟重绘");
    tree->viewport()->repaint();
    const QModelIndex failedIndex = tree->model()->index(0, 0, tree->model()->index(1, 0));
    const QRect retryRect = LoadingState::rowRetryRect(failedIndex);
    check(!retryRect.isEmpty() && tree->visualItemRect(failed).contains(retryRect),
          "LI4 「重试」那一格画出来了，落在这一行里");
    if (!retryRect.isEmpty()) {
        const QPoint at = retryRect.center();
        QMouseEvent press(QEvent::MouseButtonPress, at, tree->viewport()->mapToGlobal(at), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(tree->viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, at, tree->viewport()->mapToGlobal(at), Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(tree->viewport(), &release);
    }
    check(retried == 1, "★ LI4 点「重试」那一格 = 调了 retry 一次", QString::number(retried));
    check(parent->childCount() == 1 && parent->child(0)->data(0, LoadingState::rowLoadingRole()).toLongLong() >= 0 &&
              LoadingState::tickingRowCount() == 1,
          "LI4 重试里删掉出错行、重新 beginRow：又是一条加载行");
    qDeleteAll(parent->takeChildren());
    wait(60);
    check(!LoadingState::clockRunning(), "LI5 收尾：没有登记行时时钟停着");

    // ---- 动效关：行内同样露面一次后就不再跳 ----
    Anim::setEnabled(false);
    LoadingState::beginRow(parent);
    wait(Anim::loadingDelayMs() + 80);
    check(LoadingState::tickingRowCount() == 0 && !LoadingState::clockRunning(),
          "LI6 动效关：行内加载行露面那一下画完就不再按时钟重绘");
    Anim::setEnabled(true);
}

}  // namespace

int UiLoadingStateChecks(QApplication &) {
    Anim::setEnabled(true);
    blockChecks();
    inlineChecks();
    Anim::setEnabled(false);
    return 0;
}
