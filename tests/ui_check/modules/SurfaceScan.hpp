// 扫一棵控件树里「还长着 Qt 默认样子」的控件：列表代理、视口/边框、原生表头、
// 分组框、单选钮、页签、带边框的文本框、没走 markSwitch 的复选框，以及换页没有动效的
// QStackedWidget。
//
// 判据只报「看起来没接设计系统」，不断言失败：清单里有些是产品本来就要的原生件
// （比如 QTabWidget 只是容器），要人看一眼再决定改不改。
//
// 检测「换页动效」的办法：打开 Anim，切一页，看有没有 `wtfmlPageCurtain`
// （`Anim::pageAppear()` 挂上去的那块幕布）露出来 —— 有就是接了，没有就是硬切。
#pragma once

#include "Animations.hpp"

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QHeaderView>
#include <QItemDelegate>
#include <QLayout>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QTextEdit>
#include <QTreeView>
#include <QWidget>

#include <cstdio>
#include <cstdlib>
#include <typeinfo>
#if defined(__GNUC__)
#include <cxxabi.h>
#endif

struct SurfaceFinding {
    QString kind;    ///< 类别（汇总时按它计数）
    QString cls;     ///< 真实类名
    QString name;    ///< objectName，空的话写父控件链上的线索
    QString detail;  ///< 看到的样子
};

namespace SurfaceScan {

inline QString classNameOf(const QObject *object) {
    return object == nullptr ? QString() : QString::fromLatin1(object->metaObject()->className());
}

/// 认人用的标签：objectName → 父控件类名 + 父控件 objectName → 可见文字。
inline QString labelOf(QWidget *widget) {
    if (widget == nullptr) return QString();
    if (!widget->objectName().isEmpty()) return widget->objectName();
    QWidget *parent = widget->parentWidget();
    QString parentName = classNameOf(parent);
    if (parent != nullptr && !parent->objectName().isEmpty()) parentName += QLatin1Char('#') + parent->objectName();
    return parentName;
}

inline bool isDefaultDelegate(const QAbstractItemDelegate *delegate) {
    // 认「是不是 Qt 自带那一个」要靠 typeid：本仓不少自家代理没有 Q_OBJECT
    // （它们不发信号），metaObject()->className() 会报成基类名 QStyledItemDelegate，
    // 照名字判会把它们全当成没挂代理。
    if (delegate == nullptr) return false;
    return typeid(*delegate) == typeid(QStyledItemDelegate) || typeid(*delegate) == typeid(QItemDelegate);
}

/// 给人看的类名：typeid 的原文比 metaObject 准（见 isDefaultDelegate()），GCC 下再拆一次名字。
inline QString runtimeClassName(const QObject *object) {
    if (object == nullptr) return QString();
#if defined(__GNUC__)
    int status = 0;
    char *demangled = abi::__cxa_demangle(typeid(*object).name(), nullptr, nullptr, &status);
    QString name = status == 0 && demangled != nullptr ? QString::fromLatin1(demangled)
                                                       : QString::fromLatin1(typeid(*object).name());
    std::free(demangled);
    return name;
#else
    return QString::fromLatin1(typeid(*object).name());
#endif
}

/// 父布局里只摆了这一件（「单独成行」的复选框多半是想当开关用）。
inline bool aloneInItsRow(QWidget *widget) {
    QWidget *parent = widget != nullptr ? widget->parentWidget() : nullptr;
    QLayout *layout = parent != nullptr ? parent->layout() : nullptr;
    if (layout == nullptr) return false;
    int visible = 0;
    for (int i = 0; i < layout->count(); ++i) {
        QLayoutItem *item = layout->itemAt(i);
        QWidget *w = item != nullptr ? item->widget() : nullptr;
        if (w == nullptr || qobject_cast<QCheckBox *>(w) == nullptr) continue;
        if (w->isVisible()) ++visible;
    }
    return visible == 1;
}

inline QList<SurfaceFinding> scanWidgets(QWidget *root) {
    QList<SurfaceFinding> out;
    if (root == nullptr) return out;
    const bool dumpViews = qEnvironmentVariableIsSet("WTFML_SURFACE_DEBUG_VIEWS");
    const auto add = [&out](const QString &kind, QWidget *widget, const QString &detail) {
        SurfaceFinding finding;
        finding.kind = kind;
        finding.cls = classNameOf(widget);
        finding.name = labelOf(widget);
        finding.detail = detail;
        out.append(finding);
    };
    for (QWidget *w : root->findChildren<QWidget *>()) {
        if (!w->isVisible() || w->width() <= 0) continue;
        // 下拉框自己那套内部件（弹层列表、编辑框）不算一页的表面。
        if (qobject_cast<QComboBox *>(w->parentWidget()) != nullptr) continue;

        if (auto *view = qobject_cast<QAbstractItemView *>(w)) {
            if (dumpViews)
                std::printf("     VIEW %-24s %-28s delegate=%s\n", classNameOf(view).toUtf8().constData(),
                            qPrintable(view->objectName()),
                            runtimeClassName(view->itemDelegate()).toUtf8().constData());
            if (isDefaultDelegate(view->itemDelegate())) {
                add(QStringLiteral("default-delegate"), view,
                    QStringLiteral("delegate=%1（没挂自家代理：整行选中底、圆角、悬停动效都没有）")
                        .arg(runtimeClassName(view->itemDelegate())));
            }
            const QFrame::Shape shape = view->frameShape();
            const bool autoFill = view->viewport() != nullptr && view->viewport()->autoFillBackground();
            if (autoFill) {
                add(QStringLiteral("opaque-viewport"), view,
                    QStringLiteral("frame=%1 + 视口 autoFillBackground=1（自家列表是 NoFrame + 视口透明）")
                        .arg(shape));
            } else if (shape != QFrame::NoFrame) {
                add(QStringLiteral("native-frame"), view,
                    QStringLiteral("frame=%1（QSS 里可能已经 border:none 盖掉，待人看）").arg(shape));
            }
            QHeaderView *header = nullptr;
            if (auto *tree = qobject_cast<QTreeView *>(view)) header = tree->header();
            if (auto *table = qobject_cast<QTableView *>(view)) header = table->horizontalHeader();
            if (header != nullptr && header->isVisible()) {
                add(QStringLiteral("native-header"), view,
                    QStringLiteral("表头可见（%1 段），看 QSS 接管了没有").arg(header->count()));
            }
            continue;
        }
        if (auto *edit = qobject_cast<QPlainTextEdit *>(w)) {
            if (edit->frameShape() != QFrame::NoFrame)
                add(QStringLiteral("framed-textedit"), edit,
                    QStringLiteral("frame=%1（裸 QPlainTextEdit 的立体边框）").arg(edit->frameShape()));
            continue;
        }
        if (auto *edit = qobject_cast<QTextEdit *>(w)) {
            if (edit->frameShape() != QFrame::NoFrame)
                add(QStringLiteral("framed-textedit"), edit,
                    QStringLiteral("frame=%1（裸 QTextEdit 的立体边框）").arg(edit->frameShape()));
            continue;
        }
        if (auto *scroll = qobject_cast<QScrollArea *>(w)) {
            if (scroll->frameShape() != QFrame::NoFrame)
                add(QStringLiteral("native-scroll-frame"), scroll,
                    QStringLiteral("frame=%1").arg(scroll->frameShape()));
            continue;
        }
        if (auto *box = qobject_cast<QGroupBox *>(w)) {
            add(QStringLiteral("groupbox"), box, box->title());
            continue;
        }
        if (qobject_cast<QRadioButton *>(w) != nullptr) {
            add(QStringLiteral("radiobutton"), w, static_cast<QRadioButton *>(w)->text());
            continue;
        }
        if (auto *check = qobject_cast<QCheckBox *>(w)) {
            // 复选框的勾选框本身是全局代理画的（Theme.cpp 的 PE_IndicatorCheckBox），
            // 所以只挑「单独成行、想当开关用」的：这些才该走 markSwitch() / ToggleSwitch。
            if (check->property("wtfmlSwitch").toBool() || check->text().trimmed().isEmpty()) continue;
            for (QWidget *parent = check->parentWidget(); parent != nullptr; parent = parent->parentWidget()) {
                if (qobject_cast<QAbstractItemView *>(parent) != nullptr) {
                    check = nullptr;  // 表格/列表行里的勾：归代理管，不算开关
                    break;
                }
            }
            if (check == nullptr) continue;
            if (aloneInItsRow(check)) add(QStringLiteral("switch-like-checkbox"), check, check->text());
            continue;
        }
        if (auto *tabs = qobject_cast<QTabWidget *>(w)) {
            add(QStringLiteral("tab-widget"), tabs,
                QStringLiteral("%1 页，tabBar 原生类=%2").arg(tabs->count()).arg(classNameOf(tabs->tabBar())));
            continue;
        }
        if (qobject_cast<QTabBar *>(w) != nullptr) {
            add(QStringLiteral("tab-bar"), w, QStringLiteral("%1 页").arg(static_cast<QTabBar *>(w)->count()));
            continue;
        }
    }
    return out;
}

/// 一个一个 QStackedWidget 试：切到下一页，看有没有换页幕布。
inline QList<SurfaceFinding> probePageTransitions(QWidget *root, QApplication &app) {
    QList<SurfaceFinding> out;
    if (root == nullptr) return out;
    const bool wasEnabled = Anim::enabled();
    Anim::setEnabled(true);
    for (QStackedWidget *stack : root->findChildren<QStackedWidget *>()) {
        if (!stack->isVisible() || stack->count() < 2) continue;
        const int old = stack->currentIndex();
        const int target = (old + 1) % stack->count();
        stack->setCurrentIndex(target);
        for (int i = 0; i < 4; ++i) app.processEvents();
        bool curtainUp = false;
        for (QWidget *curtain : root->findChildren<QWidget *>(QStringLiteral("wtfmlPageCurtain")))
            if (curtain->isVisible()) {
                curtainUp = true;
                break;
            }
        stack->setCurrentIndex(old);
        for (int i = 0; i < 2; ++i) app.processEvents();
        Anim::finishPageAppear(root);
        if (!curtainUp) {
            SurfaceFinding finding;
            finding.kind = QStringLiteral("stacked-no-anim");
            finding.cls = QStringLiteral("QStackedWidget");
            finding.name = stack->objectName().isEmpty() ? labelOf(stack) : stack->objectName();
            finding.detail =
                QStringLiteral("%1 页，setCurrentIndex 硬切（没看到 pageAppear 的幕布；待人看）").arg(stack->count());
            out.append(finding);
        }
    }
    Anim::setEnabled(wasEnabled);
    return out;
}

inline QString findingSummary(const QList<SurfaceFinding> &findings) {
    QMap<QString, int> counts;
    for (const SurfaceFinding &finding : findings) ++counts[finding.kind];
    QStringList parts;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
        parts.append(QStringLiteral("%1×%2").arg(it.key()).arg(it.value()));
    return parts.join(QLatin1Char(' '));
}

}  // namespace SurfaceScan
