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
#include <QSignalBlocker>
#include <QTimer>
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
            if (m_list != nullptr) m_list->viewport()->update();  // 行底的悬停淡底跟着换
            break;
        case QEvent::Leave:
            setRowDetailVisible(rowWidget, false);
            if (m_list != nullptr) m_list->viewport()->update();
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
        // 认"这一发属于哪一行"：可能是行本身（透明/空白处），也可能是行里的某个孩子
        // （名字、大小那几格）。孩子身上没有那个动态属性，所以要沿 parent 往上找 ——
        // 否则整行只有"行控件自己没有孩子"的那一小块能拖，看着就是"拽不动"。
        QWidget *rowWidget = nullptr;
        for (QWidget *w = qobject_cast<QWidget *>(watched); w != nullptr; w = w->parentWidget()) {
            if (w->property(rowPathProperty()).isValid()) {
                rowWidget = w;
                break;
            }
        }
        const bool fromViewport = m_list != nullptr && watched == m_list->viewport();
        if ((fromViewport || rowWidget != nullptr) && m_list != nullptr) {
            switch (event->type()) {
            case QEvent::MouseButtonPress: {
                const auto *mouse = static_cast<QMouseEvent *>(event);
                if (mouse->button() != Qt::LeftButton) break;
                const QPoint pos =
                    m_list->viewport()->mapFromGlobal(mouse->globalPosition().toPoint());
                const bool onButton = qobject_cast<QAbstractButton *>(watched) != nullptr;
                QString path;
                if (rowWidget != nullptr) {
                    path = rowWidget->property(rowPathProperty()).toString();
                } else if (const QModelIndex index = m_list->indexAt(pos); index.isValid()) {
                    if (const QListWidgetItem *item = m_list->item(index.row()))
                        path = item->data(pathRole()).toString();
                }
                m_pressViewportPos = pos;
                m_pressPath = onButton ? QString() : path;
                m_pressOnRow = !onButton && !path.isEmpty();
                m_dragStarted = false;
                m_ctrlDragActive = mouse->modifiers().testFlag(Qt::ControlModifier);
                if (m_ctrlDragActive && m_list != nullptr) m_list->setAutoScroll(false);
                m_bandToggledIdentities.clear();
                m_pressSelection.clear();
                for (const QListWidgetItem *selectedItem : m_list->selectedItems())
                    m_pressSelection.append(selectedItem->data(pathRole()).toString());
                if (!m_pressPath.isEmpty()) m_bandToggledIdentities.insert(identityPath(m_pressPath));
                // 已选行的普通点击延迟到 release：单选可取消，多选可收缩，拖拽则保留原选择。
                bool pathSelected = false;
                for (const QString &selectedPath : m_pressSelection)
                    if (selectedPath == path) pathSelected = true;
                m_swallowPress = !onButton && pathSelected && (m_batchMode || m_pressSelection.size() > 1)
                    && !(mouse->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier));
                if (m_swallowPress) {
                    if (QListWidgetItem *pressedItem = m_list->itemAt(pos))
                        m_list->setCurrentItem(pressedItem, QItemSelectionModel::NoUpdate);
                }
                    qInfo() << "[drag] press watched="
                            << (watched != nullptr ? watched->metaObject()->className() : "null")
                            << "row=" << (rowWidget != nullptr) << "onButton=" << onButton
                            << "path=" << path << "swallow=" << m_swallowPress;
                break;
            }
            case QEvent::MouseMove: {
                const auto *mouse = static_cast<QMouseEvent *>(event);
                if (!(mouse->buttons() & Qt::LeftButton)) break;
                const QPoint pos =
                    m_list->viewport()->mapFromGlobal(mouse->globalPosition().toPoint());
                const DragIntent intent = dragIntent(
                    (pos - m_pressViewportPos).manhattanLength(),
                    QApplication::startDragDistance(), m_pressOnRow, m_ctrlDragActive);
                if (!m_dragStarted && intent != DragIntent::None) {
                    m_dragStarted = true;
                    if (intent == DragIntent::ToggleSelection) updateToggleBand(pos, true);
                    else {
                        beginFileDrag();
                        // 原生拖放返回后，这一发 move 已经过期，不能再让视图据此重新拖选。
                        event->accept();
                        return true;
                    }
                } else if (m_dragStarted && m_ctrlDragActive) {
                    updateToggleBand(pos, false);
                }
                // 自定义取反框选已处理这一帧，不能再交给 Qt 的默认拖选改变选择与滚动。
                if (m_dragStarted && m_ctrlDragActive) {
                    event->accept();
                    return true;
                }
                break;
            }
            case QEvent::MouseButtonRelease: {
                if (m_toggleBand != nullptr) {
                    m_toggleBand->hide();
                    m_toggleBand->deleteLater();
                    m_toggleBand = nullptr;
                }
                const bool wasBandDrag = m_ctrlDragActive;
                if (m_list != nullptr) m_list->setAutoScroll(true);
                const bool dragged = m_dragStarted;
                const bool handledClick = m_swallowPress && !dragged && !m_pressPath.isEmpty();
                if (handledClick) {
                    {
                        const QSignalBlocker blocker(m_list);
                        m_list->clearSelection();
                        if (m_pressSelection.size() > 1) {
                            for (int row = 0; row < m_list->count(); ++row) {
                                QListWidgetItem *item = m_list->item(row);
                                if (item != nullptr && item->data(pathRole()).toString() == m_pressPath)
                                    item->setSelected(true);
                            }
                        }
                    }
                    syncSelectedPaths();
                    updateSelectionBar();
                    updateInfoPanel();
                    updateJoinEnabled();
                }
                m_dragStarted = false;
                m_ctrlDragActive = false;
                m_pressOnRow = false;
                m_bandToggledIdentities.clear();
                if (wasBandDrag && m_list != nullptr) {
                    QTimer::singleShot(0, this, [this] {
                        if (m_list == nullptr) return;
                        rebuildRows();
                        m_list->viewport()->update();
                    });
                }
                m_swallowPress = false;
                if (handledClick) {
                    event->accept();
                    return true;
                }
                break;
            }
            default:
                break;
            }
            if (m_swallowPress) {
                event->accept();
                return true;
            }

        }
    }
    return QWidget::eventFilter(watched, event);
}
void InstanceFilesPage::beginFileDrag() {
    // 拖哪些：按在"已经选中的那一条"上 ⇒ 拖整份多选；按在别的条上 ⇒ 只拖这一条。
    // Qt 处理 press 时会把多选收缩成单条，所以要用按下那一刻的快照（m_pressSelection）。
    QStringList paths;
    if (!m_pressPath.isEmpty() && m_pressSelection.contains(m_pressPath)) {
        paths = m_pressSelection;
    } else if (!m_pressPath.isEmpty()) {
        paths = {m_pressPath};
    } else {
        for (const QListWidgetItem *item : m_list->selectedItems())
            paths.append(item->data(pathRole()).toString());
    }
    paths.removeAll(QString());
    paths.removeDuplicates();
    QList<QUrl> urls;
    for (const QString &path : paths) {
        if (QFileInfo::exists(path)) urls.append(QUrl::fromLocalFile(path));
    }
    qInfo() << "[drag] start urls=" << urls.size() << "press=" << m_pressPath;

    if (urls.isEmpty()) return;
    const QStringList selectionBeforeDrag = m_pressSelection;
    auto *mime = new QMimeData;
    mime->setUrls(urls);
    auto *drag = new QDrag(this);
    // 原生拖放有自己的事件循环；保留列表重绘，避免鼠标未释放时露出空白行。
    const bool autoScroll = m_list->hasAutoScroll();
    m_list->setAutoScroll(false);
    drag->setMimeData(mime);
    drag->exec(Qt::CopyAction);
    m_list->setAutoScroll(autoScroll);
    {
        const QSignalBlocker blocker(m_list);
        const QSet<QString> selectedPaths(selectionBeforeDrag.cbegin(), selectionBeforeDrag.cend());
        for (int i = 0; i < m_list->count(); ++i) {
            QListWidgetItem *item = m_list->item(i);
            item->setSelected(selectedPaths.contains(item->data(pathRole()).toString()));
        }
    }
    syncSelectedPaths();
    updateSelectionBar();
    updateInfoPanel();
    updateJoinEnabled();
    // 原生拖放可能吞掉 release；在这里清理按下状态，避免下一次点击沿用旧选择。
    m_swallowPress = false;
    m_dragStarted = false;
    m_ctrlDragActive = false;
    m_pressOnRow = false;
    m_pressSelection.clear();
    m_pressPath.clear();
    if (m_toggleBand != nullptr) {
        m_toggleBand->hide();
        m_toggleBand->deleteLater();
        m_toggleBand = nullptr;
    }
    qInfo() << "[drag] end count=" << m_list->count()
            << "current=" << m_list->currentRow() << "selected=" << m_list->selectedItems().size()
            << "listVis=" << m_list->isVisible() << "vpVis=" << m_list->viewport()->isVisible()
            << "vp=" << m_list->viewport()->size()
            << "item0=" << (m_list->item(0) != nullptr
                                ? m_list->visualItemRect(m_list->item(0))
                                : QRect())
            << "row0=" << (m_list->itemWidget(m_list->item(0)) != nullptr
                               ? m_list->itemWidget(m_list->item(0))->geometry()
                               : QRect())
            << "row0Vis=" << (m_list->itemWidget(m_list->item(0)) != nullptr
                                  && m_list->itemWidget(m_list->item(0))->isVisible());
    if (m_list != nullptr) {
        // 不要 doItemsLayout()：它会按滚动位置 0 重排，把行控件和当前滚动位置错开，
        // 屏幕上就是“有些行在、有些行空着”。让 Qt 自己按当前滚动位置收敛即可。
        m_list->viewport()->update();
    }
}

void InstanceFilesPage::updateToggleBand(const QPoint &viewportPos, bool firstUpdate) {
    Q_UNUSED(firstUpdate);
    if (m_toggleBand == nullptr) {
        m_toggleBand = new QRubberBand(QRubberBand::Rectangle, m_list->viewport());
        m_toggleBand->setGeometry(QRect(m_pressViewportPos, QSize()));
        m_toggleBand->show();
    }
    // 一行只切换一次：滑回来**不恢复**（用户要的是“碰到过就算改过”）。
    const QRect rect = QRect(m_pressViewportPos, viewportPos).normalized();
    m_toggleBand->setGeometry(rect);
    // 但每帧都把已经切换过的行强制摆回切换后的状态 —— 否则滚动/自动滚动之类的
    // 视图动作会把最远那一条又选回去。
    const QSignalBlocker selectionSignals(m_list);
    for (int row = 0; row < m_list->count(); ++row) {
        QListWidgetItem *item = m_list->item(row);
        if (item == nullptr) continue;
        const QString path = item->data(pathRole()).toString();
        const QString identity = identityPath(path);
        if (m_list->visualItemRect(item).intersects(rect) &&
            !m_bandToggledIdentities.contains(identity)) {
            m_bandToggledIdentities.insert(identity);
        }
        if (m_bandToggledIdentities.contains(identity)) {
            const bool originallySelected = m_pressSelection.contains(path);
            item->setSelected(!originallySelected);
        }
    }
    m_actionPanelDismissed = false;
    syncSelectedPaths();
    updateSelectionBar();
    updateInfoPanel();
    updateJoinEnabled();
}
