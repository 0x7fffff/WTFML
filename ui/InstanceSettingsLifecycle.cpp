// Maintenance/creation pages, file inspection and transfer/upgrade presentation.
#include "InstanceSettingsDialog.hpp"
#include "ResourcePackPage.hpp"
#include "WheelSafeComboBox.hpp"

#include "Animations.hpp"
#include "Theme.hpp"   // 浮层的底色（Theme::Part::Panel）

#include <QAbstractItemView>  // eventFilter()：那两件导航控件共同的兜底（focusInEvent 替人挑第一条）
#include <QCheckBox>
#include <QButtonGroup>
#include <QComboBox>
#include <QCompleter>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFutureWatcher>
#include <QImage>
#include <QImageReader>
#include <QtConcurrent/QtConcurrentRun>
#include <QFontMetrics>   // 左栏那一列的宽度（见 navListWidth()）
#include <QGuiApplication>
#include <QMimeData>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSize>
#include <QUrl>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QDirIterator>
#include <QSet>
#include <QImageReader>
#include <QVBoxLayout>
#include <utility>
#include <algorithm>
#include <functional>

#include "ConfigFileEditorPage.hpp"
#include "InstanceFilesPage.hpp"
#include "MainWindowUI.hpp"
#include "MemorySlider.hpp"
#include "VersionIcons.hpp"
// 第 1 格那个世界设置。头文件那边已经 include 了它（进参是个嵌套类型），
// 这一句是给"这一份 .cpp 自己用得上它的完整定义"留的。
#include "WorldSettingsPage.hpp"
#include "VersionTreeDelegate.hpp"


namespace {
/// 「截图 / Replay / 日志 / 崩溃 / 结构」这几页的列表：普通 QListWidget，但要能把条目
/// 作为**文件**拖出去（Windows 原生 CF_HDROP）。QListWidget 默认给的内部 mime 外部程序不认。
class LifecycleFileList : public QListWidget {
public:
    using QListWidget::QListWidget;

protected:
    QMimeData *mimeData(const QList<QListWidgetItem *> &items) const override {
        auto *data = new QMimeData;
        QList<QUrl> urls;
        for (const QListWidgetItem *item : items) {
            const QString path = item->data(Qt::UserRole).toString();
            if (!path.isEmpty() && QFileInfo::exists(path)) urls.append(QUrl::fromLocalFile(path));
        }
        data->setUrls(urls);
        return data;
    }

    void startDrag(Qt::DropActions supportedActions) override {
        QListWidget::startDrag(supportedActions);
        // 外部程序收下之后，Qt 有时不重排这一页（行高/图标缓存），表现为列表空白，
        // 直到点「刷新」才回来。收尾强制重排 + 重绘一次。
        doItemsLayout();
        viewport()->update();
    }
};
}  // namespace


void InstanceSettingsDialog::setupLifecyclePages() {
    for (const QString &key : lifecyclePageKeys()) {
        QWidget *page = addPage(key);
        auto *layout = qobject_cast<QVBoxLayout *>(page->layout());
        // Old page keys remain valid for saved navigation and external callers;
        // showPage() redirects them to the consolidated maintenance pages.
        if (key == QStringLiteral("integrity") || key == QStringLiteral("repair") ||
            key == QStringLiteral("export") || key == QStringLiteral("diagnosticsmenu") ||
            key == QStringLiteral("integritymenu") || key == QStringLiteral("snapshots")) continue;
        auto *title = new QLabel(pageText(key), page);
        QFont font = title->font();
        font.setBold(true);
        font = Theme::textFont(font, Theme::TextRole::Heading);
        title->setFont(font);
        layout->addWidget(title);
        const auto hint = [layout, page](const QString &text) {
            auto *label = new QLabel(text, page);
            label->setWordWrap(true);
            layout->addWidget(label);
        };
        const auto action = [layout, page, this](const QString &text, const std::function<void()> &callback) {
            auto *button = new QPushButton(text, page);
            Theme::markButton(button, Theme::ButtonKind::Neutral);
            connect(button, &QPushButton::clicked, this, callback);
            layout->addWidget(button, 0, Qt::AlignLeft);
            return button;
        };
        int commandWidth = 160;
        for (const QString &name : {createSnapshotButtonName(), restoreSnapshotButtonName(),
                 QStringLiteral("inspectRecoveryButton"), copyButtonName(), exportButtonName(),
                 deleteButtonName(), integrityCheckButtonName(), repairFilesButtonName()}) {
            if (auto *button = findChild<QPushButton *>(name))
                commandWidth = qMax(commandWidth, button->fontMetrics().horizontalAdvance(button->text()) + 32);
        }
        const auto buttonRow = [page, layout, commandWidth](const QList<QPushButton *> &buttons) {
            auto *rowHost = new QWidget(page);
            rowHost->setObjectName(QStringLiteral("instanceMaintenanceActionRow"));
            auto *row = new QHBoxLayout(rowHost);
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(8);
            layout->addWidget(rowHost);
            for (auto *button : buttons) {
                if (!button) continue;
                if (auto *previous = button->parentWidget(); previous && previous != page && previous != rowHost)
                    previous->hide();
                button->setParent(rowHost);
                if (button->objectName() == createSnapshotButtonName())
                    button->setToolTip(createSnapshotTooltip() + QLatin1Char('\n') + createSnapshotHint());
                if (button->objectName() == restoreSnapshotButtonName())
                    button->setToolTip(restoreSnapshotTooltip() + QLatin1Char('\n') + restoreSnapshotHint());
                if (button->property("wtfmlButtonKind").toString().isEmpty())
                    Theme::markButton(button, Theme::ButtonKind::Neutral);
                button->setFixedSize(commandWidth, actionButtonHeight());
                button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
                row->addWidget(button);
                button->show();
            }
            row->addStretch();
        };
        if (key == QStringLiteral("operationsmenu")) {
            const auto section = [page, layout](const QString &text) {
                auto *heading = new QLabel(text, page);
                QFont font = heading->font(); font.setBold(true); heading->setFont(font);
                layout->addWidget(heading);
            };
            section(tr("备份与恢复"));
            buttonRow({findChild<QPushButton *>(createSnapshotButtonName()),
                       findChild<QPushButton *>(restoreSnapshotButtonName()),
                       findChild<QPushButton *>(QStringLiteral("inspectRecoveryButton"))});
            section(tr("复制与导出"));
            auto *preset = new QPushButton(tr("另存为预设"), page);
            preset->setObjectName("instanceExportPresetButton");
            connect(preset, &QPushButton::clicked, this, &InstanceSettingsDialog::exportPresetRequested);
            buttonRow({findChild<QPushButton *>(copyButtonName()), findChild<QPushButton *>(exportButtonName()), preset});
            section(tr("删除实例"));
            buttonRow({findChild<QPushButton *>(deleteButtonName())});
            layout->addStretch(1);
            continue;
        }
        const bool browse = key == QStringLiteral("screenshots") || key == QStringLiteral("replay")
                            || key == QStringLiteral("logs") || key == QStringLiteral("crash")
                            || key == QStringLiteral("structure");
        if (browse) {
            if (key == QStringLiteral("logs") || key == QStringLiteral("crash")) {
                auto *log = new QPushButton(tr("游戏日志"), page);
                auto *crash = new QPushButton(tr("崩溃报告"), page);
                for (auto *button : {log, crash}) {
                    button->setCheckable(true);
                    Theme::markButton(button, Theme::ButtonKind::Neutral);
                }
                auto *tabs = new QButtonGroup(page);
                tabs->setExclusive(true);
                tabs->addButton(log);
                tabs->addButton(crash);
                log->setChecked(key == QStringLiteral("logs"));
                crash->setChecked(key == QStringLiteral("crash"));
                connect(log, &QPushButton::clicked, this, [this] { showPage(QStringLiteral("logs")); });
                connect(crash, &QPushButton::clicked, this, [this] { showPage(QStringLiteral("crash")); });
                buttonRow({log, crash});
            }
            hint(key == QStringLiteral("structure")
                     ? tr("存档 generated 目录中的结构文件。双击打开，右键打开目录。")
                     : tr("按修改时间从新到旧。双击打开，右键打开目录。"));
            action(tr("刷新"), [this, key]() { refreshLifecycleFiles(key); });
            auto *search = new QLineEdit(page);
            search->setObjectName(QStringLiteral("instanceLifecycleSearch_%1").arg(key));
            search->setPlaceholderText(tr("搜索文件名或路径（空格分隔多个关键词）"));
            search->setClearButtonEnabled(true);
            layout->addWidget(search);
            auto *result = new QLabel(page);
            result->setObjectName(QStringLiteral("instanceLifecycleResult_%1").arg(key));
            layout->addWidget(result);
            auto *list = new LifecycleFileList(page);
            list->setSelectionMode(QAbstractItemView::ExtendedSelection);
            list->setDragEnabled(true);
            list->setDragDropMode(QAbstractItemView::DragOnly);
            list->setDefaultDropAction(Qt::CopyAction);
            list->setObjectName(QStringLiteral("instanceLifecycleFiles_%1").arg(key));
            list->setIconSize(QSize(96, 64));
            list->setContextMenuPolicy(Qt::CustomContextMenu);
            layout->addWidget(list, 1);
            m_lifecycleLists.insert(key, list);
            if (key == QStringLiteral("replay") || key == QStringLiteral("structure")) {
                auto *info = new QLabel(page);
                info->setObjectName(lifecycleInfoLabelName(key));
                info->setWordWrap(true);
                info->setTextInteractionFlags(Qt::TextSelectableByMouse);
                // 比正文浅一档 —— 和 InstanceFilesPage::makeSecondary() 同一个口径
                // （`QPalette::PlaceholderText` 是全仓唯一那个"次要文字"色）。
                QPalette infoPalette = info->palette();
                infoPalette.setColor(QPalette::WindowText,
                                     infoPalette.color(QPalette::PlaceholderText));
                info->setPalette(infoPalette);
                info->setVisible(false);
                layout->addWidget(info);
                m_lifecycleInfo.insert(key, info);
                auto *timer = new QTimer(info);
                timer->setSingleShot(true);
                timer->setInterval(150);
                connect(timer, &QTimer::timeout, this, [this, key] {
                    const QString path = m_lifecycleInfoPath.value(key);
                    if (!path.isEmpty()) emit lifecycleFileInfoRequested(key, path);
                });
                m_lifecycleInfoTimers.insert(key, timer);
                connect(list, &QListWidget::currentItemChanged, this,
                        [this, key] { updateLifecycleInfo(key); });
            }
            connect(search, &QLineEdit::textChanged, this,
                    [this, key] { filterLifecycleFiles(key); });
            connect(list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
                const QString path = item->data(Qt::UserRole).toString();
                if (!path.isEmpty()) emit openInstanceFileRequested(path);
            });
            connect(list, &QListWidget::customContextMenuRequested, this, [this, list](const QPoint &point) {
                if (auto *item = list->itemAt(point)) {
                    const QString path = item->data(Qt::UserRole).toString();
                    if (!path.isEmpty()) emit openInstanceFolderRequested(QFileInfo(path).absolutePath());
                }
            });
            continue;
        }
        if (key == QStringLiteral("worldbackup")) {
            action(tr("返回存档"), [this]() { showPage(QStringLiteral("save")); })
                ->setObjectName(QStringLiteral("instanceWorldBackupBack"));
            hint(tr("备份只覆盖选中的世界。"));
            auto *list = new QListWidget(page);
            list->setObjectName(QStringLiteral("instanceWorldBackupList"));
            m_lifecycleLists.insert(key, list);
            layout->addWidget(list, 1);
            action(tr("刷新世界列表"), [this, key]() { refreshLifecycleFiles(key); });
            action(tr("创建世界备份"), [this, list]() {
                if (auto *item = list->currentItem()) {
                    const QString path = item->data(Qt::UserRole).toString();
                    if (!path.isEmpty()) emit backupSelectedWorldRequested(path);
                }
            });
            action(tr("恢复世界备份…"), [this, list]() {
                if (auto *item = list->currentItem()) {
                    const QString path = item->data(Qt::UserRole).toString();
                    if (!path.isEmpty()) emit restoreSelectedWorldRequested(path);
                }
            });
        } else if (key == QStringLiteral("diagnosis")) {
            auto *refresh = new QPushButton(tr("重新诊断"), page);
            refresh->setObjectName(diagnosisRefreshButtonName());
            connect(refresh, &QPushButton::clicked, this, [this] { emit diagnosisRequested(); });
            auto *repair = findChild<QPushButton *>(repairFilesButtonName());
            buttonRow({refresh, findChild<QPushButton *>(integrityCheckButtonName()), repair});
            m_diagnosisReport = new QPlainTextEdit(page);
            m_diagnosisReport->setObjectName(diagnosisReportName());
            m_diagnosisReport->setReadOnly(true);
            m_diagnosisReport->setPlainText(diagnosisPendingText());
            m_diagnosisReport->setLineWrapMode(QPlainTextEdit::WidgetWidth);
            layout->addWidget(m_diagnosisReport, 1);
        } else if (key == QStringLiteral("migration") || key == QStringLiteral("upgrade")) {
            // 「迁移」和「升级」共用一套控件：目标 → 计划正文 → 可勾选的范围表 → 动手那一颗。
            // 计划怎么算、勾什么、兼容性怎么说全在 `TransferPlanner`；这一层只摆字。
            //
            // NOTE: 「升级」**不下载任何东西** —— 目标版本必须是这个游戏文件夹里已经
            // 装好的那一份。要装新版本走下载页（这里只说清这一条，不假装能装）。
            const bool upgrade = key == QStringLiteral("upgrade");
            if (upgrade) {
                auto *context = new QLabel(page);
                context->setObjectName(QStringLiteral("instanceUpgradeContext"));
                context->setWordWrap(true);
                context->setTextInteractionFlags(Qt::TextSelectableByMouse);
                layout->addWidget(context);
                hint(tr("原地更新会先创建完整快照，确认后才开始。"));
            } else {
                hint(tr("仅增量复制文件，不覆盖同名项；请确保双方游戏均已退出。"));
            }
            auto *targetLine = new QHBoxLayout();
            auto *targetLabel = new QLabel(upgrade ? tr("目标版本") : tr("目标实例"), page);
            auto *combo = new QComboBox(page);
            combo->setObjectName(transferTargetComboName(key));
            combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            if (upgrade) {
                combo->setEditable(true);
                combo->setInsertPolicy(QComboBox::NoInsert);
                combo->lineEdit()->setPlaceholderText(tr("搜索目标版本或加载器"));
                combo->completer()->setFilterMode(Qt::MatchContains);
                combo->completer()->setCompletionMode(QCompleter::PopupCompletion);
            }
            targetLine->addWidget(targetLabel);
            targetLine->addWidget(combo, 1);
            layout->addLayout(targetLine);
            m_transferTargets.insert(key, combo);
            if (upgrade) {
                auto *prepare = new QPushButton(tr("准备此版本的核心与加载器…"), page);
                prepare->setObjectName(QStringLiteral("instanceUpgradePrepareTarget"));
                prepare->setAutoDefault(false);
                Theme::markButton(prepare, Theme::ButtonKind::Primary);
                layout->addWidget(prepare, 0, Qt::AlignLeft);
                prepare->hide();
                connect(prepare, &QPushButton::clicked, this, [this] {
                    const QString selected = transferTarget(QStringLiteral("upgrade"));
                    if (selected.startsWith(QStringLiteral("@download:"))) emit prepareUpgradeTargetRequested(selected.mid(10));
                });
            }

            // 计划正文：只读、可选中复制（用户想把这份范围说明贴到别处就用得上）。
            auto *plan = new QPlainTextEdit(page);
            plan->setObjectName(transferPlanTextName(key));
            plan->setReadOnly(true);
            plan->setPlainText(tr("正在准备计划…"));
            plan->setLineWrapMode(QPlainTextEdit::WidgetWidth);
            layout->addWidget(plan, 2);
            m_transferPlans.insert(key, plan);

            // 可勾选的范围表。**只让勾选框可用、不让整行被选中**：点行 = 勾/取消勾，
            // 不会出现"选中了一条但没勾上"那种两套状态。
            auto *items = new QListWidget(page);
            items->setObjectName(transferItemsListName(key));
            items->setSelectionMode(QAbstractItemView::NoSelection);
            layout->addWidget(items, 3);
            m_transferItems.insert(key, items);
            if (upgrade) {
                layout->removeWidget(plan); layout->removeWidget(items);
                auto *columns = new QHBoxLayout;
                auto *assessment = new QWidget(page);
                auto *assessmentLayout = new QVBoxLayout(assessment);
                assessmentLayout->setContentsMargins(0, 0, 0, 0);
                assessmentLayout->addWidget(new QLabel(tr("版本与迁移评估"), assessment));
                plan->setFixedHeight(104);
                assessmentLayout->addWidget(plan);
                auto *summary = new QLabel(tr("模组声明：正在读取…"), assessment);
                summary->setObjectName(QStringLiteral("instanceUpgradeModSummary"));
                summary->setWordWrap(true);
                auto *checkRow = new QHBoxLayout;
                checkRow->addWidget(summary, 1);
                auto *refreshCheck = new QPushButton(tr("重新检查"), assessment);
                refreshCheck->setObjectName(QStringLiteral("instanceUpgradeRecheck"));
                refreshCheck->setAutoDefault(false);
                Theme::markButton(refreshCheck, Theme::ButtonKind::Neutral);
                checkRow->addWidget(refreshCheck);
                assessmentLayout->addLayout(checkRow);
                auto *mods = new QTreeWidget(assessment);
                mods->setObjectName(QStringLiteral("instanceUpgradeModChecks"));
                mods->setHeaderLabels({tr("当前模组"), tr("状态"), tr("依据")});
                mods->setRootIsDecorated(false);
                mods->setAlternatingRowColors(false);
                mods->setMinimumHeight(80);
                items->setMinimumHeight(80);
                mods->header()->setSectionResizeMode(0, QHeaderView::Stretch);
                mods->header()->setSectionResizeMode(1, QHeaderView::Interactive);
                mods->setColumnWidth(1, 240);
                mods->header()->setSectionResizeMode(2, QHeaderView::Stretch);
                assessmentLayout->addWidget(mods, 1);
                auto *scope = new QWidget(page);
                scope->setMaximumWidth(290);
                auto *scopeLayout = new QVBoxLayout(scope);
                scopeLayout->setContentsMargins(0, 0, 0, 0);
                auto *scopeHint = new QLabel(tr("副本保留内容\n原地更新保留全部用户文件。"), scope);
                scopeHint->setWordWrap(true);
                scopeLayout->addWidget(scopeHint);
                scopeLayout->addWidget(items, 1);
                columns->addWidget(assessment, 3); columns->addWidget(scope, 1);
                layout->addLayout(columns, 1);
            }
            connect(items, &QListWidget::itemChanged, this,
                    [this, key] { updateTransferApplyState(key); });

            auto *apply = action(upgrade ? tr("创建升级副本…") : tr("搬过去…"), [this, key]() {
                emit transferApplyRequested(key, transferTarget(key), transferSelectedKeys(key));
            });
            apply->setObjectName(transferApplyButtonName(key));
            if (upgrade) {
                auto *inPlace = action(tr("完整快照并原地升级…"), [this, key] {
                    emit upgradeInPlaceRequested(transferTarget(key));
                });
                inPlace->setObjectName(QStringLiteral("instanceUpgradeInPlaceButton"));
                auto *rollback = action(tr("回滚最近一次更新…"), [this] { emit rollbackUpgradeRequested(); });
                rollback->setObjectName(QStringLiteral("instanceUpgradeRollbackButton"));
                layout->removeWidget(apply); layout->removeWidget(inPlace); layout->removeWidget(rollback);
                auto *actions = new QHBoxLayout;
                Theme::markButton(apply, Theme::ButtonKind::Primary);
                Theme::markButton(inPlace, Theme::ButtonKind::Danger);
                actions->addWidget(apply); actions->addWidget(inPlace); actions->addStretch(); actions->addWidget(rollback);
                layout->addLayout(actions);
            }

            // 换目标 ⇒ 只发"选的是谁"，计划由接的人算好再递回来（和选一行读元数据同一套）。
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                    [this, key, combo] {
                        const QString id = combo->currentData().toString();
                        emit transferTargetChosen(key, id);
                    });
            if (upgrade) connect(combo, &QComboBox::editTextChanged, this, [this, key] {
                updateTransferApplyState(key);
                emit transferTargetChosen(key, transferTarget(key));
            });
            updateTransferApplyState(key);
        }
        if (key != QStringLiteral("upgrade") && key != QStringLiteral("diagnosis")) layout->addStretch(1);
    }
}

void InstanceSettingsDialog::refreshLifecycleFiles(const QString &key) {
    QElapsedTimer refreshClock;
    refreshClock.start();
    // 重列一次 = 新的一代：上一份列表排出去的缩略图回来了也不许往这一份里塞。
    // 只有截图页在排缩略图，别的页重列不该把截图那几发作废（它们回来照样进缓存）。
    if (key == QStringLiteral("screenshots")) {
        ++m_lifecycleThumbGeneration;
    }
    auto *list = m_lifecycleLists.value(key);
    if (list == nullptr) return;
    list->clear();
    const QString root = m_fileDirs.value(key == QStringLiteral("structure") || key == QStringLiteral("worldbackup")
                                             ? QStringLiteral("save") : key);
    QFileInfoList files;
    if (!root.isEmpty() && QDir(root).exists()) {
        if (key == QStringLiteral("worldbackup")) {
            files = QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
        } else if (key == QStringLiteral("structure")) {
            for (const QFileInfo &world : QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                const QString generated = world.absoluteFilePath() + QStringLiteral("/generated");
                QDirIterator it(generated, {QStringLiteral("*.nbt")}, QDir::Files, QDirIterator::Subdirectories);
                while (it.hasNext()) {
                    it.next();
                    if (QDir(generated).relativeFilePath(it.filePath()).contains(QStringLiteral("/structures/")))
                        files.append(it.fileInfo());
                }
            }
        } else {
            QStringList filters;
            if (key == QStringLiteral("screenshots")) filters = {QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg")};
            if (key == QStringLiteral("replay")) filters = {QStringLiteral("*.mcpr")};
            files = QDir(root).entryInfoList(filters, QDir::Files, QDir::Time);
        }
    }
    std::sort(files.begin(), files.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return a.lastModified() > b.lastModified();
    });
    for (const QFileInfo &file : files) {
        QStringList details{file.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm"))};
        if (key == QStringLiteral("screenshots")) {
            QImageReader reader(file.absoluteFilePath());
            const QSize pixels = reader.size();
            // 截图也是**盘上的文件**（实例 / 存档跟着分享走的时候能带进来）：头里写得
            // 荒唐就不解 —— 8K 截图是 33 MP，这里留到 64 MP。
            constexpr int kMaxSide = 16384;
            constexpr qint64 kMaxPixels = 64LL * 1024 * 1024;
            const bool oversized =
                pixels.isValid() && pixels.width() > 0 && pixels.height() > 0
                && (pixels.width() > kMaxSide || pixels.height() > kMaxSide
                    || qint64(pixels.width()) * pixels.height() > kMaxPixels);
            if (pixels.isValid()) {
                details << QStringLiteral("%1×%2").arg(pixels.width()).arg(pixels.height());
                if (!oversized) {
                    reader.setScaledSize(pixels.scaled(QSize(96, 64), Qt::KeepAspectRatio));
                }
            }
            // 尺寸读不出来也照样试着解一张缩略图（有些 PNG 的头信息坏了，数据还能读）。
            // NOTE: 但"头里写得荒唐"的那一份不在此列 —— 那种读一次就是几个 G。
            // 缩略图**在后台线程解**（`requestLifecycleThumb()`），这一趟只排请求。
            details << InstanceFilesPage::formatSize(file.size());
            auto *item = new QListWidgetItem(
                file.fileName() + QLatin1Char('\n') + details.join(QStringLiteral(" / ")),
                list);
            item->setData(Qt::UserRole, file.absoluteFilePath());
            item->setToolTip(file.absoluteFilePath());
            // 透明占位图：让这一行的几何**按"有图"算**。图回来只换图标内容，行高不跳。
            QPixmap placeholder(list->iconSize());
            placeholder.fill(Qt::transparent);
            item->setIcon(placeholder);
            requestLifecycleThumb(file.absoluteFilePath());
            continue;
        }
        details << InstanceFilesPage::formatSize(file.size());
        auto *item = new QListWidgetItem(
            file.fileName() + QLatin1Char('\n') + details.join(QStringLiteral(" / ")), list);
        item->setData(Qt::UserRole, file.absoluteFilePath());
        item->setToolTip(file.absoluteFilePath());
    }
    if (files.isEmpty()) {
        auto *item = new QListWidgetItem(tr("暂无文件"), list);
        item->setFlags(Qt::NoItemFlags);
    }
    filterLifecycleFiles(key);
    // 重列之后选中项可能已经没了（`clear()` 不一定发 currentItemChanged）——
    // 和 InstanceFilesPage::rebuildRows() 一样，收尾再对一次，别让旧的一句留在底下。
    updateLifecycleInfo(key);
    if (refreshClock.elapsed() > 60)
        qInfo() << "[UI timing] lifecycle page refresh" << key << files.size() << "files,"
                << refreshClock.elapsed() << "ms";
}

void InstanceSettingsDialog::filterLifecycleFiles(const QString &key) {
    auto *list = m_lifecycleLists.value(key);
    if (list == nullptr) return;
    auto *search = list->parentWidget()->findChild<QLineEdit *>(
        QStringLiteral("instanceLifecycleSearch_%1").arg(key));
    if (search == nullptr) return; // 世界备份列表使用独立交互，不参与文件筛选。
    const QStringList terms = search->text().simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    int total = 0;
    int matches = 0;
    for (int row = 0; row < list->count(); ++row) {
        auto *item = list->item(row);
        const QString path = item->data(Qt::UserRole).toString();
        if (path.isEmpty()) continue;
        ++total;
        const bool match = std::all_of(terms.cbegin(), terms.cend(), [&path](const QString &term) {
            return path.contains(term, Qt::CaseInsensitive);
        });
        item->setHidden(!match);
        if (match) ++matches;
    }
    if (auto *result = list->parentWidget()->findChild<QLabel *>(
            QStringLiteral("instanceLifecycleResult_%1").arg(key))) {
        result->setText(total == 0 ? tr("暂无文件")
            : matches == 0 ? tr("未找到匹配文件（共 %1 个）").arg(total)
                           : tr("显示 %1 / %2 个文件").arg(matches).arg(total));
    }
}

void InstanceSettingsDialog::updateLifecycleInfo(const QString &key) {
    auto *label = m_lifecycleInfo.value(key);
    if (label == nullptr) return;
    auto *list = m_lifecycleLists.value(key);
    QString path;
    if (list != nullptr && list->currentItem() != nullptr) {
        path = list->currentItem()->data(Qt::UserRole).toString();
    }
    if (path.isEmpty()) {
        // 没选中 / 只选中了「暂无文件」那一格 ⇒ 那一格整颗收起来，别摆个空行。
        if (auto *timer = m_lifecycleInfoTimers.value(key)) timer->stop();
        m_lifecycleInfoPath.remove(key);
        label->clear();
        label->setToolTip(QString());
        label->setVisible(false);
        return;
    }
    if (path == m_lifecycleInfoPath.value(key)) return; // 同一行：正在读 / 已经读好了
    if (auto *timer = m_lifecycleInfoTimers.value(key)) timer->stop();
    m_lifecycleInfoPath.insert(key, path);
    label->setText(lifecycleInfoPendingText());
    label->setToolTip(QString());
    label->setVisible(true);
    if (auto *timer = m_lifecycleInfoTimers.value(key)) timer->start();
}

void InstanceSettingsDialog::setLifecycleInfoText(const QString &key, const QString &path,
                                                  const QString &text, const QString &toolTip) {
    auto *label = m_lifecycleInfo.value(key);
    if (label == nullptr) return;
    // 答案自带"讲的是哪一条" —— 用户已经翻到别的行上时，这一发就是过期的（同一口径
    // 见 InstanceFilesPage::setInfoText()）。
    if (path != m_lifecycleInfoPath.value(key)) return;
    label->setText(text);
    label->setToolTip(toolTip);
    label->setVisible(!text.isEmpty());
}

void InstanceSettingsDialog::setDiagnosisText(const QString &text) {
    if (m_diagnosisReport == nullptr) return;
    m_diagnosisReport->setPlainText(text.isEmpty() ? diagnosisPendingText() : text);
}

// ---------- 「迁移」/「升级」那两页 ----------
//
// 两页的控件是 setupLifecyclePages() 建好的；这一组只负责"把接的人递来的东西摆上去"和
// "把用户勾了什么读回来"。**不读盘、不算兼容性、也不写盘**。

void InstanceSettingsDialog::setTransferTargets(const QString &key,
                                                const QList<QPair<QString, QString>> &targets) {
    auto *combo = m_transferTargets.value(key);
    if (combo == nullptr) return;
    // 这一发**不出声**：灌列表是 controller 在开页时做的，它紧接着会自己算第一份计划；
    // 这里要是发 currentIndexChanged，controller 会在同一次调用里再算一遍（白扫一次盘）。
    const QSignalBlocker blocker(combo);
    const QString previous = transferTarget(key);
    combo->clear();
    if (key == QStringLiteral("upgrade")) combo->addItem(tr("请选择目标版本"), QString());
    for (const QPair<QString, QString> &target : targets) {
        combo->addItem(target.first, target.second);
    }
    const int keep = combo->findData(previous);
    if (keep >= 0) combo->setCurrentIndex(keep);
    updateTransferApplyState(key);
}

void InstanceSettingsDialog::setTransferPlan(const QString &key, const QString &text) {
    auto *plan = m_transferPlans.value(key);
    if (plan == nullptr) return;
    plan->setPlainText(text);
}

void InstanceSettingsDialog::setUpgradeContext(const QString &text) {
    if (auto *label = findChild<QLabel *>(QStringLiteral("instanceUpgradeContext"))) label->setText(text);
}

void InstanceSettingsDialog::setUpgradeModChecks(const QList<QStringList> &rows, const QString &summary) {
    if (auto *tree = findChild<QTreeWidget *>(QStringLiteral("instanceUpgradeModChecks"))) {
        const int scroll = tree->verticalScrollBar()->value();
        const QString selected = tree->currentItem() ? tree->currentItem()->text(0) : QString();
        bool expanded = false;
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            if (tree->topLevelItem(i)->data(0, Qt::UserRole + 1).toBool()) expanded = tree->topLevelItem(i)->isExpanded();
        tree->setUpdatesEnabled(false);
        tree->clear();
        tree->setProperty("upgradeModCount", rows.size());
        // 报告直接读这份原始行（含评估分类），不再从界面文字反推。
        setProperty("upgradeModRows", QVariant::fromValue(rows));
        QTreeWidgetItem *compatible = nullptr;
        QList<QStringList> ordered;
        if (property("modernUpgradeWizard").toBool()) {
            for (const auto &row : rows) if (row.value(5) != QStringLiteral("compatible")) ordered.append(row);
            for (const auto &row : rows) if (row.value(5) == QStringLiteral("compatible")) ordered.append(row);
        } else ordered = rows;
        for (const auto &row : ordered) {
            QTreeWidgetItem *item = nullptr;
            if (property("modernUpgradeWizard").toBool() && row.value(5) == QStringLiteral("compatible")) {
                if (!compatible) {
                    compatible = new QTreeWidgetItem(tree);
                    compatible->setData(0, Qt::UserRole + 1, true);
                    compatible->setFlags(Qt::ItemIsEnabled);
                }
                item = new QTreeWidgetItem(compatible, row.mid(0, 3));
            } else item = new QTreeWidgetItem(tree, row.mid(0, 3));
            if (!row.value(7).isEmpty()) {
                item->setData(0, Qt::UserRole + 2, row.value(7));
            }
            if (property("modernUpgradeFlow").toBool() && !row.value(3).isEmpty()) {
                item->setData(0, Qt::UserRole, row.value(3));
                auto *choice = new WheelSafeComboBox(tree);
                choice->addItem(tr("请选择处理方式"), QVariant());
                choice->addItem(tr("禁用，保留文件"), false);
                choice->addItem(tr("保留启用"), true);
                const bool canReplace = !row.value(6).isEmpty();
                if (canReplace) {
                    choice->addItem(tr("替换为平台适配版 %1").arg(row.value(6)),
                                    QStringLiteral("replace"));
                }
                const QVariantMap overrides = property("upgradeModOverrides").toMap();
                const QVariant previous = overrides.value(row.value(3));
                int index = 0;
                if (previous.isValid()) {
                    if (previous.typeId() == QMetaType::QString
                        && previous.toString() == QStringLiteral("replace")) {
                        index = canReplace ? 3 : 0;
                    } else {
                        index = previous.toBool() ? 2 : 1;
                    }
                } else if (row.value(4) == QStringLiteral("replace") && canReplace) {
                    index = 3;
                } else if (row.value(4) == QStringLiteral("keep")) {
                    index = 2;
                } else if (row.value(4) == QStringLiteral("disable")) {
                    index = 1;
                }
                choice->setCurrentIndex(index);
                choice->setProperty("upgradeModFile", row.value(3));
                connect(choice, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, choice] {
                    auto overrides = property("upgradeModOverrides").toMap();
                    const QString file = choice->property("upgradeModFile").toString();
                    if (choice->currentData().isValid()) overrides.insert(file, choice->currentData());
                    else overrides.remove(file);
                    setProperty("upgradeModOverrides", overrides);
                });
                tree->setItemWidget(item, 3, choice);
            }
            for (int column = 0; column < qMin(3, row.size()); ++column) item->setToolTip(column, row.at(column));
            if (!selected.isEmpty() && row.value(0) == selected) tree->setCurrentItem(item);
        }
        if (compatible) {
            compatible->setText(0, tr("正常兼容（%1）").arg(compatible->childCount()));
            compatible->setExpanded(expanded);
        }
        tree->setRootIsDecorated(compatible != nullptr);
        tree->doItemsLayout();
        tree->verticalScrollBar()->setValue(scroll);
        tree->setUpdatesEnabled(true);
    }
    if (auto *label = findChild<QLabel *>(QStringLiteral("instanceUpgradeModSummary"))) label->setText(summary);
    updateTransferApplyState(QStringLiteral("upgrade"));
}

QStringList InstanceSettingsDialog::upgradeKeptMods() const {
    QStringList files;
    if (auto *tree = findChild<QTreeWidget *>(QStringLiteral("instanceUpgradeModChecks"))) {
        for (QTreeWidgetItemIterator it(tree); *it; ++it) {
            auto *item = *it;
            if (item->data(0, Qt::UserRole + 1).toBool()) continue;
            auto *choice = qobject_cast<QComboBox *>(tree->itemWidget(item, 3));
            if (choice && choice->currentData().typeId() == QMetaType::Bool
                && choice->currentData().toBool()) {
                files.append(item->data(0, Qt::UserRole).toString());
            }
        }
    }
    return files;
}

QStringList InstanceSettingsDialog::upgradeReplaceMods() const {
    QStringList files;
    if (auto *tree = findChild<QTreeWidget *>(QStringLiteral("instanceUpgradeModChecks"))) {
        for (QTreeWidgetItemIterator it(tree); *it; ++it) {
            auto *item = *it;
            if (item->data(0, Qt::UserRole + 1).toBool()) continue;
            auto *choice = qobject_cast<QComboBox *>(tree->itemWidget(item, 3));
            if (choice && choice->currentData().typeId() == QMetaType::QString
                && choice->currentData().toString() == QStringLiteral("replace")) {
                files.append(item->data(0, Qt::UserRole).toString());
            }
        }
    }
    return files;
}

QVariantMap InstanceSettingsDialog::upgradeModReplacements() const {
    return property("upgradeModReplacements").toMap();
}

int InstanceSettingsDialog::upgradePendingModCount() const {
    int pending = 0;
    if (auto *tree = findChild<QTreeWidget *>(QStringLiteral("instanceUpgradeModChecks"))) {
        for (QTreeWidgetItemIterator it(tree); *it; ++it) {
            if ((*it)->data(0, Qt::UserRole + 1).toBool()) continue;
            auto *choice = qobject_cast<QComboBox *>(tree->itemWidget(*it, 3));
            if (!choice || !choice->currentData().isValid()) ++pending;
        }
    }
    return pending;
}

void InstanceSettingsDialog::setTransferItems(const QString &key,
                                              const QList<QPair<QString, QString>> &items,
                                              const QStringList &defaultKeys) {
    auto *list = m_transferItems.value(key);
    if (list == nullptr) return;
    // 清空 / 添加都会经 itemChanged，而这里正是一批**程序摆上去的**默认勾选 —— 挡掉这
    // 一批只是别让它把"按钮状态"重算三遍；算三遍也没有副作用，但更吵。
    const QSignalBlocker blocker(list);
    list->clear();
    for (const QPair<QString, QString> &entry : items) {
        auto *item = new QListWidgetItem(entry.first, list);
        item->setData(Qt::UserRole, entry.second);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(defaultKeys.contains(entry.second) ? Qt::Checked : Qt::Unchecked);
    }
    updateTransferApplyState(key);
}

QString InstanceSettingsDialog::transferTarget(const QString &key) const {
    auto *combo = m_transferTargets.value(key);
    if (combo == nullptr || (combo->isEditable() && combo->currentText() != combo->itemText(combo->currentIndex()))) return {};
    return combo->currentData().toString();
}

QStringList InstanceSettingsDialog::transferSelectedKeys(const QString &key) const {
    QStringList keys;
    auto *list = m_transferItems.value(key);
    if (list == nullptr) return keys;
    for (int i = 0; i < list->count(); ++i) {
        const QListWidgetItem *item = list->item(i);
        if (item != nullptr && item->checkState() == Qt::Checked) {
            keys << item->data(Qt::UserRole).toString();
        }
    }
    return keys;
}

void InstanceSettingsDialog::updateTransferApplyState(const QString &key) {
    auto *apply = findChild<QPushButton *>(transferApplyButtonName(key));
    if (apply == nullptr) return;
    // 两样都得有：选了目标、而且至少勾了一项。少了哪一样，按下去都只会弹一句错话。
    const QString selected = transferTarget(key);
    const bool needsDownload = selected.startsWith(QStringLiteral("@download:"));
    const bool hasTarget = !selected.isEmpty() && !needsDownload &&
        (key != QStringLiteral("upgrade") || !property("upgradeAssessmentReading").toBool());
    const bool hasSelection = !transferSelectedKeys(key).isEmpty();
    apply->setEnabled(hasTarget && hasSelection);
    if (key == QStringLiteral("upgrade")) {
        if (auto *prepare = findChild<QPushButton *>(QStringLiteral("instanceUpgradePrepareTarget"))) prepare->setVisible(needsDownload && !property("modernUpgradeFlow").toBool());
        if (auto *inPlace = findChild<QPushButton *>(QStringLiteral("instanceUpgradeInPlaceButton")))
            inPlace->setEnabled(hasTarget);
    }
}
void InstanceSettingsDialog::requestLifecycleThumb(const QString &path) {
    QListWidget *list = m_lifecycleLists.value(QStringLiteral("screenshots"));
    if (list == nullptr || path.isEmpty()) return;
    const QFileInfo info(path);
    // `路径|大小|修改时间`：同一个路径换了另一张图时后两位会变。
    const QString key = path + QLatin1Char('|') + QString::number(info.size()) +
                        QLatin1Char('|') + QString::number(info.lastModified().toMSecsSinceEpoch());
    const auto apply = [list, path](const QImage &image) {
        if (image.isNull()) return;
        for (int row = 0; row < list->count(); ++row) {
            QListWidgetItem *item = list->item(row);
            if (item != nullptr && item->data(Qt::UserRole).toString() == path) {
                item->setIcon(QPixmap::fromImage(image));
                return;
            }
        }
    };
    if (const auto hit = m_lifecycleThumbCache.constFind(key); hit != m_lifecycleThumbCache.constEnd()) {
        apply(*hit);
        return;
    }
    const quint64 generation = m_lifecycleThumbGeneration;
    auto *watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this,
            [this, watcher, key, apply = std::move(apply), generation] {
        const QImage image = watcher->result();
        watcher->deleteLater();
        if (image.isNull()) return;
        // 缓存按张数封顶：一页几十张 96×64 的缩略图很小，但换实例换文件夹会一直攒。
        if (m_lifecycleThumbCache.size() >= 512) m_lifecycleThumbCache.clear();
        m_lifecycleThumbCache.insert(key, image);
        // 这一页已经重列过 / 换过页：图照样进缓存，只是不再往现在这份列表里塞。
        if (generation != m_lifecycleThumbGeneration) return;
        apply(image);
    });
    watcher->setFuture(QtConcurrent::run([path] {
        // 截图也是**盘上的文件**（实例 / 存档跟着分享走的时候能带进来）：头里写得荒唐
        // 就不解 —— 8K 截图是 33 MP，这里留到 64 MP。解不开就给空图，那一行留着占位图标。
        constexpr int kMaxSide = 16384;
        constexpr qint64 kMaxPixels = 64LL * 1024 * 1024;
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const QSize pixels = reader.size();
        if (pixels.isValid() && pixels.width() > 0 && pixels.height() > 0) {
            if (pixels.width() > kMaxSide || pixels.height() > kMaxSide ||
                qint64(pixels.width()) * pixels.height() > kMaxPixels) {
                return QImage();
            }
            reader.setScaledSize(pixels.scaled(QSize(96, 64), Qt::KeepAspectRatio));
        }
        return reader.read();
    }));
}

