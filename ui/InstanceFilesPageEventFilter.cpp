/**
 * @file InstanceFilesPageEventFilter.cpp
 * @brief 这一页上的事件路由：浮条重定位、行控件悬停时露「详情」、右键进详情页。
 *
 * 【为什么单独一个翻译单元】
 *
 * 这一页的行不是普通条目，而是 `setItemWidget()` 摆上去的真控件；底下那条悬浮操作条又
 * **不在布局里**（它压在列表上）。于是有一批位置和悬停状态没有别人帮着算，全在这支过滤器里：
 *
 * - 页面 / 列表视口变大小 ⇒ 重新摆那条浮条（它在布局外，没有人会替它重排）；
 * - 鼠标进出一行 ⇒ 露出 / 收起那一行的「详情」按钮（Enter 是往祖先那串发的，进的是行里
 *   哪一个孩子也算）；
 * - 行上右键 ⇒ 打开这一条的详情页，**且保留上一页的选择**。
 *
 * 【几条踩过的坑】
 *
 * - 认"这一发来自哪一行"靠行控件上的动态属性（`rowPathProperty()`）：同一层里的另一个
 *   孩子是那条浮条，它身上没有这一格，于是自动被排除，不必再写一份"哪些控件算行"的名单；
 * - 右键那一发**必须吃掉、而且 `accept()` 不能省**：Qt 那个"被忽略了就往上冒"的循环判的
 *   是 `res && isAccepted()`，只 `return true` 的话这一发照样会被送到 viewport 上；
 * - `path` 必须在 `emit` **之前**取出来：接的人会把这一页收掉去开详情页，回来时
 *   `watched` 说不定已经不在了。
 */

#include "InstanceFilesPage.hpp"

#include "InstanceFilesPageInternals.hpp"

#include <QEvent>
#include <QListWidget>
#include <QMouseEvent>
#include <QAbstractButton>
#include <QApplication>
#include <QDrag>
#include <QFileInfo>
#include <QMimeData>
#include <QRubberBand>
#include <QUrl>
#include <QWidget>

bool InstanceFilesPage::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_contentHost && event->type() == QEvent::Resize)
        repositionSelectionBar();
    // 列表的 viewport 变大小了 ⇒ 底下那条悬浮操作条要重算位置（见 .hpp 里那段）。
    //
    // NOTE: 那条操作条**不在布局里**（在布局里就成了"列表下面另起一行"，而用户要的是
    // "浮在列表上"，见 repositionSelectionBar）—— 所以它的位置没有人帮着算，
    // 只有这一发能告诉我们"该算了"。
    // NOTE: `watched == m_list->viewport()` 那一句里 `viewport()` 可能为 null（这一页还没
    // 建到那一步），所以先判 m_list。
    if (m_list != nullptr && event->type() == QEvent::Resize &&
        watched == m_list->viewport()) {
        repositionSelectionBar();
    }

    // ---------- 行控件上那三发 ----------
    //
    // 认"这一发来自哪一行"靠行控件上那个**动态属性**（makeRow 里 `setProperty`，名字见
    // .hpp 里 rowPathProperty）—— 列表底下的另一个孩子是那条悬浮操作条，它身上没有这一格，
    // 于是同一段代码自动把它排除在外，不必再写一份"哪些控件算行"的名单。
    if (watched != nullptr && watched->property(rowPathProperty()).isValid()) {
        auto *rowWidget = static_cast<QWidget *>(watched);
        switch (event->type()) {
        case QEvent::Enter:
            // 鼠标进了这一行 ⇒ 露出这一行的「详情」。**进的是行里哪一个孩子也算** —— Qt 的
            // Enter 是往祖先那一串发的（见 .hpp 里 setRowDetailVisible 那段）。
            setRowDetailVisible(rowWidget, true);
            break;
        case QEvent::Leave:
            setRowDetailVisible(rowWidget, false);
            break;
        case QEvent::MouseButtonPress: {
            const auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() != Qt::RightButton) {
                break;
            }
            // 右键这一发 = 开这一条的详情页，并**保留上一页的选择**。**只认"认得出
            // 资源"的那些** —— 判据和这一行那格 ⓘ 的可用性逐字相同（`m_resourceTips`，
            // 见 applyDetailButtonState）。
            //
            // 【为什么必须把它吃掉，而不是让视图也看见这一发】
            // ① 视图收到右键会自己动当前项/选中态（Qt 为了让右键菜单有个目标，会先把
            // 鼠标底下那一条选中），而这里要保留上一页的选择；
            // ② 这一页没有右键菜单，放它过去也没有第二个读者。
            // ⚠ `accept()` 那一句**不能省**：Qt 那个"被忽略了就往上冒"的循环判的是
            // `res && isAccepted()` —— 事件过滤器只 return true（不 accept）的话，
            // 这一发照样会被送到 viewport 上（见 .hpp 里 eventFilter 那段）。
            // ⚠ `path` 必须在 emit **之前**取出来：接的人会把这一页收掉去开详情页，
            // 回来之后 `watched` 说不定已经不在了。
            const QString path = watched->property(rowPathProperty()).toString();
            if (m_batchMode && m_resourceTips.contains(identityPath(path))) {
                emit detailRequested(path);
                event->accept();
                return true;
            }
            break;
        }
        default:
            break;
        }
    }
    // ---------- 拖出 / Ctrl 框选取反 ----------
    {
        const bool fromViewport = m_list != nullptr && watched == m_list->viewport();
        const bool fromRow = watched != nullptr && watched->property(rowPathProperty()).isValid();
        if ((fromViewport || fromRow) && m_list != nullptr) {
            switch (event->type()) {
            case QEvent::MouseButtonPress: {
                const auto *mouse = static_cast<QMouseEvent *>(event);
                if (mouse->button() != Qt::LeftButton) break;
                const bool onButton = fromRow && qobject_cast<QAbstractButton *>(watched) != nullptr;
                m_pressViewportPos = m_list->viewport()->mapFromGlobal(mouse->globalPosition().toPoint());
                m_pressPath = (fromRow && !onButton) ? watched->property(rowPathProperty()).toString() : QString();
                m_pressOnRow = fromRow && !onButton;
                m_dragStarted = false;
                m_ctrlDragActive = mouse->modifiers().testFlag(Qt::ControlModifier);
                m_bandToggledIdentities.clear();
                // 按下那一行 MultiSelection 已经切换过了，框选别再翻它一次。
                if (!m_pressPath.isEmpty()) m_bandToggledIdentities.insert(identityPath(m_pressPath));
                break;
            }
            case QEvent::MouseMove: {
                const auto *mouse = static_cast<QMouseEvent *>(event);
                if (!(mouse->buttons() & Qt::LeftButton)) break;
                const QPoint pos = m_list->viewport()->mapFromGlobal(mouse->globalPosition().toPoint());
                const DragIntent intent = dragIntent(
                    (pos - m_pressViewportPos).manhattanLength(),
                    QApplication::startDragDistance(), m_pressOnRow, m_ctrlDragActive);
                if (!m_dragStarted && intent != DragIntent::None) {
                    m_dragStarted = true;
                    if (intent == DragIntent::ToggleSelection) updateToggleBand(pos, true);
                    else beginFileDrag();
                } else if (m_dragStarted && m_ctrlDragActive) {
                    updateToggleBand(pos, false);
                }
                break;
            }
            case QEvent::MouseButtonRelease:
                if (m_toggleBand != nullptr) {
                    m_toggleBand->hide();
                    m_toggleBand->deleteLater();
                    m_toggleBand = nullptr;
                }
                m_dragStarted = false;
                m_ctrlDragActive = false;
                m_pressOnRow = false;
                m_bandToggledIdentities.clear();
                break;
            default:
                break;
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}
void InstanceFilesPage::beginFileDrag() {
    QStringList paths;
    const auto selected = m_list->selectedItems();
    for (const QListWidgetItem *item : selected) {
        const QString path = item->data(pathRole()).toString();
        if (!path.isEmpty()) paths.append(path);
    }
    // 抓哪一条就拖哪一条：MultiSelection 的「按下即切换」可能刚把它取消掉，仍然按它算。
    if (!m_pressPath.isEmpty() && !paths.contains(m_pressPath)) paths.prepend(m_pressPath);
    QList<QUrl> urls;
    for (const QString &path : paths) {
        if (QFileInfo::exists(path)) urls.append(QUrl::fromLocalFile(path));
    }
    if (urls.isEmpty()) return;
    auto *mime = new QMimeData;
    mime->setUrls(urls);
    auto *drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->exec(Qt::CopyAction);
}

void InstanceFilesPage::updateToggleBand(const QPoint &viewportPos, bool firstUpdate) {
    Q_UNUSED(firstUpdate);
    if (m_toggleBand == nullptr) {
        m_toggleBand = new QRubberBand(QRubberBand::Rectangle, m_list->viewport());
        m_toggleBand->setGeometry(QRect(m_pressViewportPos, QSize()));
        m_toggleBand->show();
    }
    const QRect rect = QRect(m_pressViewportPos, viewportPos).normalized();
    m_toggleBand->setGeometry(rect);
    for (int row = 0; row < m_list->count(); ++row) {
        QListWidgetItem *item = m_list->item(row);
        if (item == nullptr || !m_list->visualItemRect(item).intersects(rect)) continue;
        const QString identity = identityPath(item->data(pathRole()).toString());
        if (m_bandToggledIdentities.contains(identity)) continue;
        m_bandToggledIdentities.insert(identity);
        item->setSelected(!item->isSelected());
    }
}
