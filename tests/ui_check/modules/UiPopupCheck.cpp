#include "Support.hpp"
#include "Popups.hpp"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QGraphicsOpacityEffect>
#include <QHelpEvent>
#include <QPushButton>
#include <QToolButton>
#include <QTimer>
#include <QComboBox>
#include <QListView>
#include <QLineEdit>
#include <QMenu>
#include <QScreen>
#include <QScrollBar>

namespace {
constexpr int kPopupRowHeight = 34;
}

namespace {

/// 浮层这一层现在的样子：磨砂玻璃底 + 1px 亮边，**不再有**系统那个黄底方角气泡。
/// 判据取"整块主色离 tokens().glass 很近、且一个黄底像素都没有"。
bool looksLikeGlass(const QImage &shot) {
    const QColor glass = overOpaque(Theme::tokens().glass, Theme::tokens().page);
    const QColor fill = dominantColor(shot, shot.rect().adjusted(4, 4, -4, -4));
    // 系统那条是黄底（#ffffe1 一类）；我们这条是玻璃压在页面色上，两者差得开。
    return colorDistance(fill, glass) <= 24 &&
           colorDistance(fill, QColor(0xff, 0xff, 0xe1)) > 12;
}

/// 把一张带透明底的浮层图垫到页面色上，存成验收图。
bool saveShot(const QWidget *widget, const QString &path) {
    const QImage shot = captureWidgetOn(const_cast<QWidget *>(widget), Theme::tokens().page);
    QImage canvas(shot.size() + QSize(40, 40), QImage::Format_ARGB32);
    canvas.fill(Theme::tokens().page);
    QPainter painter(&canvas);
    painter.drawImage(QPoint(20, 20), shot);
    painter.end();
    return canvas.save(path);
}

}  // namespace

/// 浮层那一层：tooltip / 下拉弹层 / 菜单。验收图写进 `WTFML_POPUP_SHOTS` 指的那个目录。
int UiPopupCheck(QApplication &app) {
    const int fontId = QFontDatabase::addApplicationFont(
        QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts/msyh.ttc")));
    const auto families = QFontDatabase::applicationFontFamilies(fontId);
    if (!families.isEmpty()) {
        app.setFont(QFont(families.first(), 10));
    }
    app.setStyle(Theme::createStyle());
    Theme::setSpec(Theme::defaultSpec());
    Theme::apply(app);
    Anim::setEnabled(false);  // 验收图不靠动效，断言也不该等它

    int failures = 0;
    const auto expect = [&failures](bool ok, const QString &text,
                                    const QString &detail = QString()) {
        const QByteArray utf8 = text.toUtf8();
        const QByteArray extra = detail.toUtf8();
        printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", utf8.constData(),
               extra.isEmpty() ? "" : "   ", extra.constData());
        fflush(stdout);
        if (!ok) ++failures;
    };

    const QString shots = qEnvironmentVariable("WTFML_POPUP_SHOTS");
    if (!shots.isEmpty()) {
        QDir().mkpath(shots);
    }

    // ---------- ① tooltip：自绘那一条 ----------
    {
        QWidget host;
        auto *button = new QPushButton(QStringLiteral("文件夹"), &host);
        button->setIcon(QIcon());
        Q_UNUSED(button);
        host.resize(240, 120);
        host.show();
        QApplication::processEvents();

        Popups::showTooltip(QStringLiteral("打开实例文件夹"), host.mapToGlobal(QPoint(40, 40)), nullptr);
        QApplication::processEvents();
        expect(Popups::tooltipVisible() && Popups::tooltipText() == QStringLiteral("打开实例文件夹"),
               "tooltip 是自绘的那一条（内容对得上，系统那条不出现）");
        QWidget *bubble = nullptr;
        for (QWidget *widget : QApplication::allWidgets()) {
            if (widget->objectName() == QStringLiteral("wtfmlTooltip")) {
                bubble = widget;
                break;
            }
        }
        expect(bubble != nullptr && bubble->testAttribute(Qt::WA_TranslucentBackground),
               "tooltip 那个窗口是透明底的（磨砂才透得出来）");
        if (bubble != nullptr) {
            const QImage shot = captureWidgetOn(bubble, Theme::tokens().page);
            expect(looksLikeGlass(shot),
                   "tooltip 是磨砂玻璃、不是系统那个黄底方框",
                   QStringLiteral("主色 %1（该是 %2）")
                       .arg(dominantColor(shot, shot.rect().adjusted(4, 4, -4, -4)).name(),
                            overOpaque(Theme::tokens().glass, Theme::tokens().page).name()));
        }
        Popups::hideTooltip();
        QApplication::processEvents();
        expect(!Popups::tooltipVisible(), "hideTooltip() 能收回去");
        host.hide();
    }

    // ---------- ①b 500ms 才出、1 秒内换控件立刻出 ----------
    {
        QWidget host;
        auto *iconA = new QPushButton(&host);
        auto *iconB = new QPushButton(&host);
        iconA->setToolTip(QStringLiteral("重新扫描 Java"));   // 没有 text() ⇒ 图标钮，出提示
        iconB->setToolTip(QStringLiteral("打开实例文件夹"));
        auto *row = new QHBoxLayout(&host);
        row->addWidget(iconA);
        row->addWidget(iconB);
        host.resize(240, 80);
        host.show();
        QApplication::processEvents();
        // 先等过「刚收起 1 秒内立刻出」那个窗口，不然这一条量的是那条规则（不是 500ms）。
        {
            QElapsedTimer warm;
            warm.start();
            while (warm.elapsed() < 1100) {
                QApplication::processEvents(QEventLoop::AllEvents, 50);
            }
        }

        // 进去之后马上查：还没到 500ms，什么都不该出。
        QEvent enter(QEvent::Enter);
        QApplication::sendEvent(iconA, &enter);
        QApplication::processEvents();
        const bool early = Popups::tooltipVisible();
        // 再等够 500ms（跑事件循环让它到点）。
        QElapsedTimer clock;
        clock.start();
        while (clock.elapsed() < 700) {
            QApplication::processEvents(QEventLoop::AllEvents, 50);
        }
        expect(!early && Popups::tooltipVisible() &&
                   Popups::tooltipText() == QStringLiteral("重新扫描 Java"),
               "悬停 500ms 才出提示（更早的时候不出）",
               QStringLiteral("早查 %1 / 到点 %2").arg(early ? 1 : 0).arg(Popups::tooltipVisible() ? 1 : 0));

        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(iconA, &leave);
        QApplication::processEvents();
        // 刚收起，马上移到相邻那一颗：不等，立刻出。
        QApplication::sendEvent(iconB, &enter);
        QApplication::processEvents();
        expect(Popups::tooltipVisible() &&
                   Popups::tooltipText() == QStringLiteral("打开实例文件夹"),
               "收起后 1 秒内换到相邻控件：立刻出，不再等 500ms",
               Popups::tooltipText());
        QApplication::sendEvent(iconB, &leave);
        QApplication::processEvents();
        host.hide();
    }

    // ---------- ② 文字按钮不出提示 ----------
    {
        QPushButton textButton(QStringLiteral("浏览…"));
        QPushButton iconButton;
        iconButton.setIcon(QIcon());
        QComboBox combo;
        QLineEdit edit;
        expect(Popups::isTextButton(&textButton) && !Popups::isTextButton(&iconButton) &&
                   !Popups::isTextButton(&combo) && !Popups::isTextButton(&edit),
               "判据：只有带 text() 的按钮算「文字按钮」，图标钮 / 下拉 / 输入框不算");
        // 实例文件那排 ⓘ / ✕：屏幕上是图标，text() 只是给无障碍与验证工程读的 —— 它们仍该有提示。
        QToolButton glyphButton;
        glyphButton.setToolButtonStyle(Qt::ToolButtonIconOnly);
        glyphButton.setText(QStringLiteral("详情"));
        QPushButton markedIcon;
        markedIcon.setText(QStringLiteral("详情"));
        Theme::markIconButton(&markedIcon);
        expect(!Popups::isTextButton(&glyphButton) && !Popups::isTextButton(&markedIcon),
               "只画图标的钮（`ToolButtonIconOnly` / `markIconButton()` 过）不算文字按钮，提示照出");
        // 真发一条 ToolTip 事件：文字按钮那一条要被吃掉，而且不许弹出自绘气泡。
        textButton.setToolTip(QStringLiteral("浏览…"));
        QHelpEvent help(QEvent::ToolTip, QPoint(2, 2), textButton.mapToGlobal(QPoint(2, 2)));
        QApplication::sendEvent(&textButton, &help);
        QApplication::processEvents();
        expect(!Popups::tooltipVisible(), "文字按钮的悬停提示不出（事件被吃掉）");
    }

    // ---------- ③ 下拉弹层 ----------
    {
        const auto makeCombo = [](QWidget *parent, int items) {
            auto *combo = new QComboBox(parent);
            for (int i = 0; i < items; ++i) {
                combo->addItem(QStringLiteral("选项 %1").arg(i + 1));
            }
            return combo;
        };
        QWidget host;
        auto *shortCombo = makeCombo(&host, 5);
        auto *longCombo = makeCombo(&host, 45);
        auto *row = new QHBoxLayout(&host);
        row->addWidget(shortCombo);
        row->addWidget(longCombo);
        host.resize(420, 120);
        host.show();
        shortCombo->showPopup();
        QApplication::processEvents();

        expect(shortCombo->maxVisibleItems() == 8, "下拉最多露 8 项（限高，不再整屏铺开）");
        QAbstractItemView *view = shortCombo->view();
        QWidget *popup = view != nullptr ? view->window() : nullptr;
        expect(popup != nullptr && popup != &host && popup->testAttribute(Qt::WA_TranslucentBackground),
               "下拉弹层是透明底的独立窗口（磨砂才透得出来）");
        expect(view != nullptr && view->itemDelegate() != nullptr &&
                   view->itemDelegate()->property("wtfmlPopupDelegate").toBool(),
               "下拉那一列换成了自绘代理（项高 34 / 悬停淡底 / 选中强调淡底）");
        if (view != nullptr) {
            const QRect rowRect = view->visualRect(view->model()->index(0, 0));
            expect(rowRect.height() == 34, "项高 34",
                   QStringLiteral("量到 %1").arg(rowRect.height()));
        }
        expect(popup != nullptr && popup->height() <= 34 * 8 + 12 + 4,
               "弹层高度压在 8 项以内",
               QStringLiteral("%1 px").arg(popup != nullptr ? popup->height() : -1));
        expect(popup != nullptr &&
                   popup->findChild<QLineEdit *>(QStringLiteral("wtfmlPopupSearch")) == nullptr,
               "短列表（5 项）没有搜索框");
        const int scrollMax = view != nullptr ? view->verticalScrollBar()->maximum() : 0;
        expect(scrollMax > 0, "内容超出 8 项时弹层里能滚（滚轮 / 拖动 / 键盘）",
               QStringLiteral("滚动上限 %1").arg(scrollMax));
        if (view != nullptr) {
            // 细条：常驻的是 3px 一根，但常态透明度是 0（滚动时才露、停 600ms 淡出）。
            auto *effect = qobject_cast<QGraphicsOpacityEffect *>(view->verticalScrollBar()->graphicsEffect());
            expect(Theme::widgetStyleSheet().contains(
                       QLatin1String("QComboBox QAbstractItemView QScrollBar:vertical { width: 3px")) &&
                       effect != nullptr && effect->opacity() < 0.5,
                   "滚动条是 3px 细条、常态不显（S2）",
                   QStringLiteral("3px 规则 %1 / 透明度 %2")
                       .arg(Theme::widgetStyleSheet().contains(
                                QLatin1String("QComboBox QAbstractItemView QScrollBar:vertical { width: 3px"))
                                ? 1 : 0)
                       .arg(effect != nullptr ? effect->opacity() : -1.0));
            // Qt 自带那两块上下翻页箭头也不许在（用户不要任何小箭头）。
            int scrollers = 0;
            if (popup != nullptr) {
                for (QWidget *child : popup->findChildren<QWidget *>()) {
                    if (QString::fromLatin1(child->metaObject()->className())
                            .contains(QLatin1String("QComboBoxPrivateScroller")) &&
                        child->isVisible()) {
                        ++scrollers;
                    }
                }
            }
            expect(scrollers == 0, "弹层上下那两块系统翻页箭头不在了",
                   QStringLiteral("还露着 %1 块").arg(scrollers));
        }
        shortCombo->hidePopup();
        QApplication::processEvents();

        // 长列表：顶上多一条搜索框，键入只留匹配项。
        longCombo->showPopup();
        QApplication::processEvents();
        QAbstractItemView *longView = longCombo->view();
        QWidget *longPopup = longView != nullptr ? longView->window() : nullptr;
        auto *search = longPopup != nullptr
                           ? longPopup->findChild<QLineEdit *>(QStringLiteral("wtfmlPopupSearch"))
                           : nullptr;
        expect(search != nullptr, "长列表（45 项）顶上有一条搜索框");
        // 限高按**项数**算：8 项 × 34 + 内边距，再给顶上那条搜索框留出自己那一截。
        const int longHeight = longPopup != nullptr ? longPopup->height() : -1;
        const int designHeight = kPopupRowHeight * 8 + 12;  // 项 8 个 + 上下内边距 6
        expect(longHeight <= designHeight + 48,
               "长列表也只露 8 项（弹层高度压在 8 项 + 搜索框以内，不再整屏铺开）",
               QStringLiteral("弹层 %1 px（8 项配比 %2 px）").arg(longHeight).arg(designHeight));
        if (search != nullptr && longView != nullptr) {
            search->setText(QStringLiteral("选项 4"));
            QApplication::processEvents();
            auto *longList = qobject_cast<QListView *>(longView);
            int visible = 0;
            if (longList != nullptr) {
                for (int i = 0; i < longCombo->count(); ++i) {
                    visible += longList->isRowHidden(i) ? 0 : 1;
                }
            }
            expect(visible > 0 && visible < longCombo->count(),
                   "搜索框键入之后只剩匹配项（跳转不靠滚）",
                   QStringLiteral("剩 %1 项").arg(visible));
            search->clear();
            QApplication::processEvents();
        }
        longCombo->hidePopup();
        QApplication::processEvents();
        host.hide();
    }

    // ---------- ④ 菜单：同一个外壳 ----------
    {
        QMenu menu;
        menu.addAction(QStringLiteral("打开实例文件夹"));
        menu.addAction(QStringLiteral("复制路径"));
        menu.show();
        QApplication::processEvents();
        expect(menu.testAttribute(Qt::WA_TranslucentBackground),
               "菜单是透明底的（底由那层磨砂玻璃画）");
        expect(Theme::widgetStyleSheet().contains(
                   QLatin1String("QMenu { background-color: transparent;")),
               "菜单样式表里不再铺不透明底（否则会把玻璃整块盖掉）");
        menu.hide();
        QApplication::processEvents();
    }

    // ---------- ⑤ 样式表里那几条规格还在 ----------
    {
        const QString sheet = Theme::widgetStyleSheet();
        expect(sheet.contains(QLatin1String("QComboBox QAbstractItemView { background-color: transparent;")),
               "下拉弹层那张表不再自己铺不透明底（玻璃由弹层窗口画）");
        expect(!sheet.contains(QLatin1String("#ffffe1")),
               "样式表里没有系统 tooltip 那个黄底");
    }

    // ---------- ⑥ 验收图：深色 + 青瓷各一遍（`grab()` 拍不到 DWM 合成那层，图里是自绘玻璃） ----------
    if (!shots.isEmpty()) {
        for (const QString &preset : {QStringLiteral("dark"), QStringLiteral("emerald")}) {
            Theme::setSpec(Theme::presetSpec(preset));
            Theme::apply(app);
            QApplication::processEvents();

            QWidget host;
            host.resize(220, 90);
            host.show();
            QApplication::processEvents();
            Popups::showTooltip(QStringLiteral("打开实例文件夹"), host.mapToGlobal(QPoint(30, 30)), nullptr);
            QApplication::processEvents();
            for (QWidget *widget : QApplication::allWidgets()) {
                if (widget->objectName() == QStringLiteral("wtfmlTooltip") && widget->isVisible()) {
                    expect(saveShot(widget, QDir(shots).filePath(preset + QStringLiteral("-tooltip.png"))),
                           QStringLiteral("%1 tooltip 验收图写出来了").arg(preset));
                    break;
                }
            }
            Popups::hideTooltip();

            auto *shortCombo = new QComboBox(&host);
            for (int i = 0; i < 5; ++i) {
                shortCombo->addItem(QStringLiteral("选项 %1").arg(i + 1));
            }
            shortCombo->setGeometry(10, 10, 200, 32);
            shortCombo->show();  // 先 show 才会走 Polish，装饰器（弹层外壳）是在那一步装的
            shortCombo->showPopup();
            QApplication::processEvents();
            if (QWidget *popup = shortCombo->view() != nullptr ? shortCombo->view()->window() : nullptr) {
                expect(saveShot(popup, QDir(shots).filePath(preset + QStringLiteral("-dropdown-short.png"))),
                       QStringLiteral("%1 短列表验收图写出来了").arg(preset));
            }
            shortCombo->hidePopup();

            auto *longCombo = new QComboBox(&host);
            for (int i = 0; i < 45; ++i) {
                longCombo->addItem(QStringLiteral("选项 %1").arg(i + 1));
            }
            longCombo->setGeometry(10, 10, 200, 32);
            longCombo->show();  // 先 show 才会走 Polish，装饰器（弹层外壳）是在那一步装的
            longCombo->showPopup();
            QApplication::processEvents();
            if (QWidget *popup = longCombo->view() != nullptr ? longCombo->view()->window() : nullptr) {
                expect(popup->findChild<QLineEdit *>(QStringLiteral("wtfmlPopupSearch")) != nullptr,
                       QStringLiteral("%1 长列表顶上那条搜索框在").arg(preset));
                expect(saveShot(popup, QDir(shots).filePath(preset + QStringLiteral("-dropdown-long.png"))),
                       QStringLiteral("%1 长列表 + 搜索验收图写出来了").arg(preset));
                // 只看细条那一档：把透明度钉到 1 再拍一张（S2 的常态是 0，屏幕上要滚才露）
                if (auto *bar = longCombo->view()->verticalScrollBar()) {
                    if (auto *effect = qobject_cast<QGraphicsOpacityEffect *>(bar->graphicsEffect())) {
                        effect->setOpacity(1.0);
                        QApplication::processEvents();
                        expect(saveShot(popup, QDir(shots).filePath(
                                            preset + QStringLiteral("-dropdown-thumb.png"))),
                               QStringLiteral("%1 细条（3px）验收图写出来了").arg(preset));
                        effect->setOpacity(0.0);
                    }
                }
            }
            longCombo->hidePopup();

            QMenu menu;
            menu.addAction(QStringLiteral("打开实例文件夹"));
            menu.addAction(QStringLiteral("复制路径"));
            menu.addSeparator();
            menu.addAction(QStringLiteral("删除实例"));
            menu.show();
            QApplication::processEvents();
            expect(saveShot(&menu, QDir(shots).filePath(preset + QStringLiteral("-menu.png"))),
                   QStringLiteral("%1 菜单验收图写出来了").arg(preset));
            menu.hide();
            host.hide();
            QApplication::processEvents();
        }
        Theme::setSpec(Theme::defaultSpec());
        Theme::apply(app);
    }

    printf("%s %d 项失败\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
