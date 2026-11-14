// 设计系统默认生效（ui/DesignDefaults，步骤 120）：光秃秃的列表 / 树 / 表 / 文本框 / 栈
// 什么都不接也拿到自家默认；退出口那个属性管用；手动接过换页的栈不会一次换页播两发。
#include "Support.hpp"
#include "DesignDefaults.hpp"

#include <QComboBox>
#include <QEventLoop>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>
#include <QVariantAnimation>

namespace {

void wait(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

QWidget *curtain(QWidget *page) { return page->findChild<QWidget *>(QStringLiteral("wtfmlPageCurtain")); }
bool curtainShown(QWidget *page) { return curtain(page) != nullptr && curtain(page)->isVisible(); }

class OwnDelegate : public QStyledItemDelegate {
    using QStyledItemDelegate::QStyledItemDelegate;
};

bool defaultsOn(QAbstractItemView *view) {
    return DesignDefaults::isRowDelegate(view->itemDelegate()) && view->frameShape() == QFrame::NoFrame &&
           !view->viewport()->autoFillBackground();
}

void viewChecks() {
    QWidget window;
    window.resize(640, 480);
    auto *layout = new QVBoxLayout(&window);
    auto *list = new QListWidget;
    list->addItems({QStringLiteral("一"), QStringLiteral("二")});
    auto *tree = new QTreeView;
    auto *model = new QStandardItemModel(2, 2, tree);
    tree->setModel(model);
    auto *table = new QTableWidget(2, 3);
    auto *text = new QPlainTextEdit;
    auto *own = new QListWidget;
    own->setItemDelegate(new OwnDelegate(own));
    auto *native = new QListWidget;
    DesignDefaults::optOut(native);  // 测试：退出口
    auto *combo = new QComboBox;
    combo->addItems({QStringLiteral("甲"), QStringLiteral("乙")});
    for (QWidget *w : {static_cast<QWidget *>(list), static_cast<QWidget *>(tree), static_cast<QWidget *>(table),
                       static_cast<QWidget *>(text), static_cast<QWidget *>(own), static_cast<QWidget *>(native),
                       static_cast<QWidget *>(combo)})
        layout->addWidget(w);
    window.show();
    QApplication::processEvents();

    check(defaultsOn(list), "★ DD1 光秃秃的 QListWidget：默认行代理 + NoFrame + 视口透明");
    check(defaultsOn(tree), "★ DD1 光秃秃的 QTreeView：默认行代理 + NoFrame + 视口透明");
    check(defaultsOn(table) && !table->showGrid(), "DD1 光秃秃的 QTableWidget：默认行代理 + NoFrame + 视口透明 + 不画网格");
    check(!table->isCornerButtonEnabled() &&
              table->horizontalHeader()->defaultAlignment() == (Qt::AlignLeft | Qt::AlignVCenter),
          "DD1b 光秃秃的 QTableWidget：关左上角角按钮，表头字靠左");
    check(list->sizeHintForRow(0) >= DesignDefaults::minRowHeight(), "DD1 默认行代理行高不低于 minRowHeight",
          QString::number(list->sizeHintForRow(0)));
    check(!DesignDefaults::isRowDelegate(table->horizontalHeader()->itemDelegate()),
          "DD1 表头不是条目视图那一类，不被换代理");
    check(text->frameShape() == QFrame::NoFrame && !text->viewport()->autoFillBackground(),
          "★ DD2 光秃秃的 QPlainTextEdit：去原生框、视口透明（输入面归 QSS）");
    check(!DesignDefaults::isRowDelegate(own->itemDelegate()), "DD3 自家代理不被替换");
    check(!DesignDefaults::isRowDelegate(native->itemDelegate()) && native->frameShape() != QFrame::NoFrame,
          "★ DD4 挂了退出属性的列表留原生代理和边框");
    combo->showPopup();
    QApplication::processEvents();
    check(!DesignDefaults::isRowDelegate(combo->view()->itemDelegate()), "DD5 下拉弹层里的列表不被换代理");
    combo->hidePopup();
    // 焦点环那条会反复 unpolish / polish：再 polish 一次不叠第二层。
    QAbstractItemDelegate *before = list->itemDelegate();
    list->style()->unpolish(list);
    list->style()->polish(list);
    check(list->itemDelegate() == before, "DD6 重复 polish 不换掉已换上的默认代理");
}

/// @p area 里第一列有墨的 x（和这一片左缘那个底色差得开就算墨）；没有返回 -1。
int firstInkX(const QImage &image, const QRect &area) {
    const QRect r = area.intersected(image.rect());
    const QColor ground = image.pixelColor(r.left() + 1, r.center().y());
    for (int x = r.left(); x <= r.right(); ++x)
        for (int y = r.top(); y <= r.bottom(); ++y) {
            const QColor c = image.pixelColor(x, y);
            if (qMax(qAbs(c.red() - ground.red()), qMax(qAbs(c.green() - ground.green()), qAbs(c.blue() - ground.blue()))) > 60)
                return x;
        }
    return -1;
}

/// 表头字和单元格字左缘对齐：每一列（首列、中间列、末列）都量一遍，不按某张表打补丁。
void headerAlignChecks(QApplication &app) {
    Theme::apply(app);  // 表头那条 padding 在应用 QSS 里
    QWidget window;
    window.resize(640, 240);
    auto *layout = new QVBoxLayout(&window);
    auto *table = new QTableWidget(2, 3);
    table->setHorizontalHeaderLabels({QStringLiteral("甲乙"), QStringLiteral("甲乙"), QStringLiteral("甲乙")});
    for (int row = 0; row < 2; ++row)
        for (int col = 0; col < 3; ++col) table->setItem(row, col, new QTableWidgetItem(QStringLiteral("甲乙")));
    table->verticalHeader()->hide();
    layout->addWidget(table);
    window.show();
    QApplication::processEvents();
    const QImage shot = window.grab().toImage();
    QHeaderView *header = table->horizontalHeader();
    QStringList detail;
    bool aligned = true;
    for (int col = 0; col < 3; ++col) {
        const QPoint headerAt = header->mapTo(&window, QPoint(header->sectionViewportPosition(col), 0));
        const int headX = firstInkX(shot, QRect(headerAt, QSize(header->sectionSize(col), header->height())));
        const QRect cell = table->visualRect(table->model()->index(0, col));
        const int cellX = firstInkX(shot, QRect(table->viewport()->mapTo(&window, cell.topLeft()), cell.size()));
        aligned &= headX >= 0 && cellX >= 0 && qAbs(headX - cellX) <= 1;
        detail << QStringLiteral("列%1 表头 %2 / 格 %3").arg(col).arg(headX).arg(cellX);
    }
    check(aligned, "★ DD11 表头字和单元格字每一列左缘对齐（同一个 cellTextInset）", detail.join(QStringLiteral("；")));
}

/// 两页的栈；页上各一块标签撑面积。
QStackedWidget *twoPages(QWidget *parent) {
    auto *stack = new QStackedWidget(parent);
    for (const auto &name : {QStringLiteral("页一"), QStringLiteral("页二")}) stack->addWidget(new QLabel(name));
    return stack;
}

void stackChecks() {
    QWidget window;
    window.resize(640, 480);
    auto *layout = new QVBoxLayout(&window);
    QStackedWidget *bare = twoPages(&window);
    QStackedWidget *quiet = twoPages(&window);
    DesignDefaults::optOut(quiet);  // 测试：退出口
    QStackedWidget *manual = twoPages(&window);
    // 旧写法：手动接了一条一样的换页动效。
    QObject::connect(manual, &QStackedWidget::currentChanged, manual, [manual](int) {
        if (QWidget *page = manual->currentWidget(); page != nullptr && page->isVisible()) Anim::pageAppear(page);
    });
    auto *tabs = new QTabWidget(&window);
    tabs->addTab(new QLabel(QStringLiteral("卡一")), QStringLiteral("一"));
    tabs->addTab(new QLabel(QStringLiteral("卡二")), QStringLiteral("二"));
    auto *tabsQuiet = new QTabWidget(&window);
    tabsQuiet->addTab(new QLabel(QStringLiteral("卡一")), QStringLiteral("一"));
    tabsQuiet->addTab(new QLabel(QStringLiteral("卡二")), QStringLiteral("二"));
    DesignDefaults::optOut(tabsQuiet);  // 测试：选项卡退出（自己接 pageAppear 的那种）
    for (QWidget *w : {static_cast<QWidget *>(bare), static_cast<QWidget *>(quiet), static_cast<QWidget *>(manual),
                       static_cast<QWidget *>(tabs), static_cast<QWidget *>(tabsQuiet)})
        layout->addWidget(w);
    window.show();
    QApplication::processEvents();

    bare->setCurrentIndex(1);
    check(curtainShown(bare->widget(1)), "★ DD7 光秃秃的 QStackedWidget 换页：新那一页盖上换页幕布");
    quiet->setCurrentIndex(1);
    check(curtain(quiet->widget(1)) == nullptr, "★ DD8 挂了退出属性的栈换页硬切");
    tabs->setCurrentIndex(1);
    check(curtainShown(tabs->widget(1)), "DD9 QTabWidget 里那个栈换页同样有动效");
    tabsQuiet->setCurrentIndex(1);
    check(curtain(tabsQuiet->widget(1)) == nullptr, "DD9 选项卡退出时它里面那个栈也不动");

    // 手动接线 + 默认：第一次换页把幕布建出来、跑完；第二次换页时数这一拍起了几发动画。
    manual->setCurrentIndex(1);
    wait(Anim::appearMs() + 150);
    manual->setCurrentIndex(0);
    manual->setCurrentIndex(1);
    QWidget *c = curtain(manual->widget(1));
    const int runs = c != nullptr ? int(c->findChildren<QVariantAnimation *>().size()) : -1;
    check(runs == 1, "★ DD10 手动接过换页的栈：一次换页只起一发（不重播）", QString::number(runs));
    wait(Anim::appearMs() + 150);
    check(c != nullptr && !c->isVisible(), "DD10 那一发照常收尾");
}

}  // namespace

int UiDesignDefaultsChecks(QApplication &app) {
    viewChecks();
    Anim::setEnabled(true);
    stackChecks();
    Anim::setEnabled(false);
    headerAlignChecks(app);
    return 0;
}
