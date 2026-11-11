#include "Support.hpp"
int UiInstanceFiltersChecks(QMainWindow &window, MainWindowUI &ui, QTabWidget * &tabs) {
    // ---------- U32 实例页那一行「搜索 + 四颗筛选 chip」 ----------
    //
    // 落到界面上的那一份：树**上面**一行 —— 左边一个搜索框，右边四颗筛子，再下面是
    // 一句「没有符合筛选条件的实例」（平时收着）。这一节量的是**看得见的那一份**：五样东西
    // 在不在、摆在哪一栏、谁占宽度、chip 有多高、那句话平时收不收着。
    //
    // NOTE: 接线（打一个字剩下几行、chip 点一下剩什么、清空之后展开状态回不回得去）在
    // mw_check 的 T37；判据层（怎么分词、哪六个串参与比对、"与/或"）在 verify 的
    // ZS 段。这一节**不重验**那两半，只管"这一行长什么样"。
    {
        window.show();
        window.resize(900, 760);
        if (tabs != nullptr) {
            tabs->setCurrentIndex(MainWindowUI::instancesTabIndex());
        }
        QApplication::processEvents();

        QWidget *filterRow = ui.getInstanceFilterRow();
        QLineEdit *search = ui.getInstanceSearchEdit();
        QLabel *noMatch = ui.getInstanceNoMatchLabel();
        QTreeWidget *tree = ui.getInstanceList();
        QList<QPushButton *> chips;
        for (int i = 0; i < 4; ++i) {
            chips.append(ui.getInstanceFilterButton(
                static_cast<MainWindowUI::InstanceFilterButton>(i)));
        }
        const bool allThere = filterRow != nullptr && search != nullptr && noMatch != nullptr &&
                              tree != nullptr && chips.size() == 4 && chips.at(0) != nullptr &&
                              chips.at(1) != nullptr && chips.at(2) != nullptr &&
                              chips.at(3) != nullptr;
        check(allThere,
              "U32.0 前提：那一行 / 搜索框 / 四颗 chip / 那句话 / 下面那棵树都按 objectName "
              "找得到");
        if (allThere) {
            // ---------- ① 落在哪儿：和树同栏、在树上面，那句话夹在中间 ----------
            //
            // 次序按**布局里那一位**比，不比屏幕坐标：藏起来的控件不参加布局，算出来的
            // 位置是个"上一次摆在哪儿"的残留值（这一节开量之前那句话正是收着的）。
            auto *column = qobject_cast<QBoxLayout *>(filterRow->parentWidget()->layout());
            const int rowAt = column != nullptr ? column->indexOf(filterRow) : -1;
            const int noteAt = column != nullptr ? column->indexOf(noMatch) : -1;
            const int treeAt = column != nullptr ? column->indexOf(tree) : -1;
            check(column != nullptr && filterRow->parentWidget() == tree->parentWidget() &&
                      rowAt >= 0 && rowAt < noteAt && noteAt < treeAt,
                  "★★ U32.1 那一行和树**同一栏、在树上面**，那句「没有符合筛选条件的实例」"
                  "夹在两者中间（这是「下面这棵树怎么摆」，不是另一块面板）",
                  QStringLiteral("同一栏=%1 次序 %2/%3/%4")
                      .arg(filterRow->parentWidget() == tree->parentWidget() ? 1 : 0)
                      .arg(rowAt)
                      .arg(noteAt)
                      .arg(treeAt));

            // ---------- ② 搜索框：一个**带 ✕ 的输入框**，不是一个按钮 ----------
            //
            // 「清空搜索恢复原展开状态」那一句要有个能"清空"的动作，而这一格自己就带着
            // 它（那个 ✕）—— 再摆一颗「清空」按钮是同一件事说两遍。
            check(search->placeholderText() == MainWindowUI::instanceSearchPlaceholder() &&
                      search->text().isEmpty(),
                  "U32.2 搜索框平时是空的、占位那句是约定的那一句（点名的三样：实例名、"
                  "版本号、加载器）",
                  search->placeholderText());
            check(search->isClearButtonEnabled(),
                  "U32.2 搜索框上带着那个 ✕（清空走它，不另摆一颗「清空搜索」按钮）");

            // ---------- ③ 四颗 chip：文字、次序、能亮、初始都不亮 ----------
            QStringList texts;
            QStringList expected;
            for (int i = 0; i < 4; ++i) {
                const auto which = static_cast<MainWindowUI::InstanceFilterButton>(i);
                texts << chips.at(i)->text();
                expected << MainWindowUI::instanceFilterText(which);
            }
            check(texts == expected,
                  "★ U32.3 四颗的字和次序就是约定的那一串「原版 / 模组 / 可更新 / 最近运行」",
                  texts.join(QLatin1Char('|')));
            int checkable = 0;
            int checkedNow = 0;
            int touched = 0;
            for (QPushButton *button : chips) {
                if (button->isCheckable()) {
                    ++checkable;
                }
                if (button->isChecked()) {
                    ++checkedNow;
                }
                if (!button->toolTip().isEmpty()) {
                    ++touched;
                }
            }
            check(checkable == 4,
                  "★ U32.3 四颗都是**能亮/能灭**的那一档（它们要的是「正按着」这个状态，"
                  "不是四颗一点就走的按钮）",
                  QString::number(checkable));
            check(checkedNow == 0, "U32.3 刚打开这一页时四颗一颗都不亮（= 没在筛）",
                  QString::number(checkedNow));
            check(touched == 4,
                  "U32.3 四颗各自挂着一句说明（「最近运行」那种口径 —— 认的是账本上最近二十来条 "
                  "—— 得写在那儿）",
                  QString::number(touched));

            // ---------- ④ chip 那一档：24px 高、共用同一份样式表 ----------
            //
            // 和资源详情页那排游戏版本**同一个出处**（`Theme::markChipButton()`）：这两处
            // 各写一份的话，同一种控件在两张页面上会长成两个样子。这一条钉的就是"共用
            // 那一份"，不是"高度等于某个数"。
            int chipHeightOk = 0;
            int chipStyleOk = 0;
            for (QPushButton *button : chips) {
                if (button->height() == Theme::chipHeight()) {
                    ++chipHeightOk;
                }
                if (button->styleSheet() == Theme::chipStyleSheet(QApplication::palette()) &&
                    button->styleSheet().contains(QStringLiteral(":checked"))) {
                    ++chipStyleOk;
                }
            }
            check(chipHeightOk == 4 &&
                      search->height() == Theme::chipHeight(),
                  "★★ U32.4 四颗都是那一档高度（`Theme::chipHeight()`）—— 全局那条 "
                  "min-height 顶不动它们（`markIconButton()` 挡在前面）；**搜索框也钉在同一"
                  "档**（它自然高度是字号算出来的，和旁边差 1px —— 真屏上就是 23 对 24，"
                  "见 ui_shot 9d）",
                  QStringLiteral("对上 %1/4 颗（各高 %2/%3/%4/%5），搜索框 %6")
                      .arg(chipHeightOk)
                      .arg(chips.at(0)->height())
                      .arg(chips.at(1)->height())
                      .arg(chips.at(2)->height())
                      .arg(chips.at(3)->height())
                      .arg(search->height()));
            check(chipStyleOk == 4,
                  "★ U32.4 四颗各自挂着的就是 `Theme::chipStyleSheet()` 现算的那一份"
                  "（含勾上那一档实底色）—— 不是这一页自己另写一套",
                  QString::number(chipStyleOk));

            // ---------- ⑤ 谁占宽度：搜索框吃掉剩下的，四颗一颗都不跟着长 ----------
            auto *rowLayout = qobject_cast<QBoxLayout *>(filterRow->layout());
            // 设计稿 manage：四颗 chip 在左、一段伸缩、定宽搜索框靠右。
            check(rowLayout != nullptr && rowLayout->count() == 6 && rowLayout->stretch(4) == 1 &&
                      rowLayout->itemAt(5)->widget() == search && search->minimumWidth() == search->maximumWidth(),
                  "★ U32.5 那一行 = 四颗 chip + 一段伸缩 + 定宽搜索框（窗口拉宽时长的只有中间那段空）",
                  rowLayout != nullptr ? QStringLiteral("%1 项 / 伸缩 %2 / 搜索宽 %3..%4").arg(rowLayout->count()).arg(rowLayout->stretch(4)).arg(search->minimumWidth()).arg(search->maximumWidth()) : QStringLiteral("<没有布局>"));
            int fixedChips = 0;
            for (QPushButton *button : chips) {
                if (button->sizePolicy().horizontalPolicy() == QSizePolicy::Fixed) {
                    ++fixedChips;
                }
            }
            check(fixedChips == 4,
                  "U32.5 四颗的横向策略是 Fixed（自己那点宽度说了算，不参与被拉伸）",
                  QString::number(fixedChips));

            // ---------- ⑥ 那句话：居中、折行、平时收着 ----------
            check(noMatch->text() == MainWindowUI::instanceNoMatchText(),
                  "U32.6 那句话逐字就是约定的「没有符合筛选条件的实例」", noMatch->text());
            check((noMatch->alignment() & Qt::AlignHCenter) != 0 && noMatch->wordWrap(),
                  "★ U32.6 它是**居中 + 折行**的一行说明（不是贴着左边的一行小字）",
                  QStringLiteral("居中=%1 折行=%2")
                      .arg((noMatch->alignment() & Qt::AlignHCenter) != 0 ? 1 : 0)
                      .arg(noMatch->wordWrap() ? 1 : 0));
            check(!noMatch->isVisibleTo(noMatch->parentWidget()),
                  "★ U32.6 平时**收着**（要它占着位置的话，树和筛选行中间会永远空一条）");
            check(noMatch->sizePolicy().horizontalPolicy() == QSizePolicy::Ignored,
                  "U32.6 那句话**不参与横向度量**（同一句说明在别处也是这条规矩："
                  "折行 QLabel 报出来的 sizeHint 是「整句排成一行」的宽度）",
                  QString::number(int(noMatch->sizePolicy().horizontalPolicy())));

            // 收尾：把这一页摆回"什么都没筛"的样子（下面那几节看的是干净界面）。
            search->clear();
            QApplication::processEvents();
            check(!noMatch->isVisibleTo(noMatch->parentWidget()),
                  "U32 收尾：清空搜索 ⇒ 那句话又收起来");
        }
    }
    return 0;
}


