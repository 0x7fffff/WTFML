/**
 * @file InstanceUpdateFlow.cpp
 * @brief 将目标核心安装与当前实例更新串联；只使用现有下载器和快照事务。
 */
#include "InstanceUpdateFlow.hpp"
#include "MainWindow.hpp"
#include "InstanceSettingsDialog.hpp"
#include "InstanceUpdateWizard.hpp"
#include "MainWindowUI.hpp"
#include "InlineMessage.hpp"
#include "Theme.hpp"
#include "controllers/InstanceTransferController.hpp"
#include "controllers/ActivityCenterController.hpp"
#include "manage/VersionManager.hpp"
#include "manage/InstanceFiles.hpp"
#include "launcher/LauncherManager.hpp"
#include "settings/GlobalJavaSettings.hpp"
#include "util/AppPaths.hpp"
#include <QComboBox>
#include <QCompleter>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QUuid>
#include <QVBoxLayout>
#include <memory>

namespace {
struct Selection {
    QString root, source, game, loader, loaderVersion, core, problem;
    bool preparing = false;
    bool executing = false;
    bool finished = false;
    bool requiresReload = false;
    bool evaluated = false;
    Tasks::TaskId firstTask = 0;
};
VersionManager::LoaderKind loaderKind(const QString &id) {
    if (id == QStringLiteral("forge")) return VersionManager::LoaderKind::Forge;
    if (id == QStringLiteral("neoforge")) return VersionManager::LoaderKind::NeoForge;
    if (id == QStringLiteral("optifine")) return VersionManager::LoaderKind::OptiFine;
    return VersionManager::LoaderKind::Fabric;
}
}

void InstanceUpdateFlow::attach(MainWindow *owner, InstanceSettingsDialog *dialog, const QString &sourceId) {
    dialog->setProperty("modernUpgradeFlow", true);
    auto *legacy = dialog->findChild<QComboBox *>(InstanceSettingsDialog::transferTargetComboName("upgrade"));
    auto *tree = dialog->findChild<QTreeWidget *>("instanceUpgradeModChecks");
    auto *plan = dialog->findChild<QPlainTextEdit *>(InstanceSettingsDialog::transferPlanTextName("upgrade"));
    auto *scopes = dialog->findChild<QListWidget *>(InstanceSettingsDialog::transferItemsListName("upgrade"));
    if (!legacy || !tree || !plan || !scopes) return;
    auto *page = legacy->parentWidget();
    auto *layout = qobject_cast<QVBoxLayout *>(page->layout());
    if (!layout) return;
    // Legacy transfer controls remain available to migration and compatibility tests,
    // but are not presented as a second update workflow.
    for (int i = 0; i < layout->count(); ++i) {
        if (auto *row = layout->itemAt(i)->layout()) {
            bool targetRow = false;
            for (int j = 0; j < row->count(); ++j) if (row->itemAt(j)->widget() == legacy) targetRow = true;
            if (targetRow) for (int j = 0; j < row->count(); ++j) if (auto *widget = row->itemAt(j)->widget()) widget->hide();
        }
    }
    scopes->parentWidget()->hide();
    for (const auto &name : {InstanceSettingsDialog::transferApplyButtonName("upgrade"),
            QStringLiteral("instanceUpgradeInPlaceButton"), QStringLiteral("instanceUpgradePrepareTarget"),
            QStringLiteral("instanceUpgradeRollbackButton")})
        if (auto *button = dialog->findChild<QPushButton *>(name)) button->hide();
    tree->setHeaderLabels({QObject::tr("当前模组"), QObject::tr("兼容检查"), QObject::tr("依据"), QObject::tr("更新时处理")});
    tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    tree->setColumnWidth(1, 220);
    plan->setFixedHeight(74);

    auto state = std::make_shared<Selection>();
    state->root = AppPaths::minecraftDir(); state->source = sourceId;
    const auto source = owner->m_versionManager->displayParts(sourceId);
    for (const auto &task : owner->m_activityCenter->tasks()->all()) state->firstTask = qMax(state->firstTask, task.id);
    auto *selectors = new QWidget(page);
    selectors->setObjectName("instanceUpdateSelectors");
    auto *selectorLayout = new QVBoxLayout(selectors);
    selectorLayout->setContentsMargins(16, 12, 16, 12);
    auto *sourceContext = new QLabel(QObject::tr("当前实例：%1\nMinecraft %2 · %3\n\n请选择更新后的游戏与加载器版本。下一步检查现有模组，确认前不会修改实例。")
        .arg(sourceId, source.realVersion, source.loaderName.isEmpty() ? QObject::tr("纯原版") : source.loaderName), selectors);
    sourceContext->setWordWrap(true); selectorLayout->addWidget(sourceContext);
    selectorLayout->addSpacing(16);
    auto *row = new QHBoxLayout;
    auto *game = new QComboBox(selectors); game->setObjectName("instanceUpdateGame");
    game->setEditable(true); game->setInsertPolicy(QComboBox::NoInsert);
    game->completer()->setFilterMode(Qt::MatchContains);
    game->completer()->setCompletionMode(QCompleter::PopupCompletion);
    game->lineEdit()->setPlaceholderText(QObject::tr("搜索 Minecraft 版本"));
    auto *loader = new QComboBox(selectors); loader->setObjectName("instanceUpdateLoader");
    for (const auto &id : {QString(), QStringLiteral("fabric"), QStringLiteral("forge"), QStringLiteral("neoforge"), QStringLiteral("optifine")})
        loader->addItem(id.isEmpty() ? QObject::tr("纯原版") : id, id);
    loader->setCurrentIndex(qMax(0, loader->findData(source.loaderName.toLower())));
    auto *loaderVersion = new QComboBox(selectors); loaderVersion->setObjectName("instanceUpdateLoaderVersion");
    row->addWidget(new QLabel(QObject::tr("1. 更新到"), selectors)); row->addWidget(game, 2);
    row->addWidget(loader, 1); row->addWidget(loaderVersion, 1);
    auto *retry = new QPushButton(QObject::tr("刷新"), selectors); retry->setObjectName("instanceUpdateRefresh");
    Theme::markButton(retry, Theme::ButtonKind::Neutral); row->addWidget(retry);
    selectorLayout->addLayout(row);
    selectorLayout->addStretch();
    auto *modeRow = new QHBoxLayout;
    auto *mode = new QComboBox(selectors); mode->setObjectName("instanceUpdateMode");
    mode->addItem(QObject::tr("创建更新副本（保留原实例）"), false);
    mode->addItem(QObject::tr("更新当前实例（先完整快照）"), true);
    auto *copyName = new QLineEdit(sourceId + QObject::tr("-更新副本"), selectors);
    copyName->setObjectName("instanceUpdateCopyName"); copyName->setPlaceholderText(QObject::tr("更新副本名称"));
    modeRow->addWidget(new QLabel(QObject::tr("2. 更新方式"), selectors)); modeRow->addWidget(mode, 1); modeRow->addWidget(copyName, 1);
    auto *modePanel = new QWidget(page); modePanel->setLayout(modeRow);
    auto *note = new QLabel(QObject::tr("兼容声明不是运行保证。未知项必须明确选择处理方式，不会自动禁用；本轮不自动下载适配模组。"), page);
    note->setWordWrap(true);
    auto *status = new QLabel(page); status->setObjectName("instanceUpdateStatus"); status->setWordWrap(true);
    auto *start = new QPushButton(QObject::tr("开始更新"), page); start->setObjectName("instanceUpdateStart");
    Theme::markButton(start, Theme::ButtonKind::Primary); start->setAutoDefault(false);
    auto *rollback = new QPushButton(QObject::tr("回滚上次更新…"), page);
    Theme::markButton(rollback, Theme::ButtonKind::Ghost); rollback->setAutoDefault(false);
    QObject::connect(rollback, &QPushButton::clicked, dialog, [dialog] { emit dialog->rollbackUpgradeRequested(); });

    // Move the real controls into a native wizard. The wizard stays in the backing
    // dialog's QObject tree so assessment updates still find the same widgets.
    auto *review = new QWidget;
    auto *reviewLayout = new QVBoxLayout(review); reviewLayout->setContentsMargins(0, 0, 0, 0);
    reviewLayout->addWidget(note);
    auto *batchRow = new QHBoxLayout;
    for (const bool keep : {true, false}) {
        auto *button = new QPushButton(keep ? QObject::tr("未确认项全部保留") : QObject::tr("未确认项全部禁用"), review);
        Theme::markButton(button, Theme::ButtonKind::Neutral); batchRow->addWidget(button);
        QObject::connect(button, &QPushButton::clicked, dialog, [tree, keep] {
            for (QTreeWidgetItemIterator it(tree); *it; ++it)
                if (auto *choice = qobject_cast<QComboBox *>(tree->itemWidget(*it, 3)); choice && !choice->currentData().isValid())
                    choice->setCurrentIndex(keep ? 2 : 1);
        });
    }
    auto *stopQuery = new QPushButton(QObject::tr("停止平台查询，使用已有结果"), review);
    Theme::markButton(stopQuery, Theme::ButtonKind::Ghost); batchRow->addStretch(); batchRow->addWidget(stopQuery);
    QObject::connect(stopQuery, &QPushButton::clicked, dialog, [dialog] { emit dialog->stopUpgradeQueryRequested(); });
    reviewLayout->addLayout(batchRow);
    plan->hide();
    reviewLayout->addWidget(tree->parentWidget(), 1);
    auto *confirmation = new QWidget;
    auto *confirmLayout = new QVBoxLayout(confirmation);
    confirmLayout->addWidget(modePanel);
    auto *summary = new QLabel(confirmation); summary->setObjectName("instanceUpdateConfirmation"); summary->setWordWrap(true);
    confirmLayout->addWidget(summary); confirmLayout->addStretch();
    auto *progress = new QWidget;
    auto *progressLayout = new QVBoxLayout(progress); progressLayout->addWidget(status);
    auto *steps = new QListWidget(progress); steps->setObjectName("instanceUpdateExecutionSteps");
    steps->setSelectionMode(QAbstractItemView::NoSelection); progressLayout->addWidget(steps, 1);
    auto *retryFlow = new QPushButton(QObject::tr("返回目标选择，重新检查"), progress);
    Theme::markButton(retryFlow, Theme::ButtonKind::Neutral); progressLayout->addWidget(retryFlow); retryFlow->hide();
    auto *wizard = new InstanceUpdateWizard(selectors, review, confirmation, progress, dialog);
    dialog->setProperty("modernUpgradeWizard", true);
    start->setParent(wizard); start->hide();
    const auto hideLayout = [](QLayout *root, const auto &self) -> void {
        for (int i = 0; i < root->count(); ++i) {
            if (auto *widget = root->itemAt(i)->widget()) widget->hide();
            if (auto *child = root->itemAt(i)->layout()) self(child, self);
        }
    };
    hideLayout(layout, hideLayout);
    auto *entry = new QLabel(QObject::tr("在独立向导中选择目标、检查模组并确认更新。更新当前实例前会创建完整快照。"), page);
    entry->setWordWrap(true); layout->addWidget(entry);
    auto *open = new QPushButton(QObject::tr("打开实例更新向导…"), page); open->setObjectName("instanceUpdateOpenWizard");
    Theme::markButton(open, Theme::ButtonKind::Primary); layout->addWidget(open, 0, Qt::AlignLeft);
    rollback->show(); layout->addWidget(rollback, 0, Qt::AlignLeft); layout->addStretch();
    const auto showWizard = [wizard, dialog, state] {
        if (state->requiresReload) return;
        if (!wizard->isVisible()) { wizard->restart(); wizard->show(); }
        wizard->raise(); wizard->activateWindow();
        Q_UNUSED(dialog);
    };
    QObject::connect(open, &QPushButton::clicked, dialog, showWizard);
    QObject::connect(dialog, &InstanceSettingsDialog::updateWizardRequested, dialog, [dialog, showWizard] {
        QTimer::singleShot(0, dialog, showWizard);
    });
    QObject::connect(wizard, &QDialog::finished, dialog, [dialog, state] { if (state->requiresReload) dialog->reject(); });
    wizard->cancelRequested = [owner, state, status] {
        if (state->preparing) {
            owner->m_downloadCancelInFlight = true;
            const bool requested = owner->m_versionManager->cancelActiveDownload();
            if (!requested) owner->m_downloadCancelInFlight = false;
            status->setText(requested ? QObject::tr("已请求取消核心准备，原实例未改变。")
                : QObject::tr("当前安装阶段暂不能取消，请等待阶段结束。")); return false;
        }
        if (state->executing) { status->setText(QObject::tr("正在进行文件事务，不能中途关闭；请等待完成或失败结果。")); return false; }
        return true;
    };
    QObject::connect(retryFlow, &QPushButton::clicked, wizard, [wizard, state, retryFlow] {
        state->finished = false; state->problem.clear(); retryFlow->hide(); wizard->restart();
    });
    QObject::connect(wizard, &QWizard::currentIdChanged, wizard, [wizard, start, state](int id) {
        if (id == 3 && !state->preparing && !state->executing && !state->finished)
            QTimer::singleShot(0, wizard, [wizard, start, state] {
                if (!wizard->isVisible() || wizard->currentId() != 3) return;
                start->click();
                if (!state->preparing && !state->executing && !state->finished) {
                    state->finished = true;
                    state->problem = QObject::tr("尚未开始更新。可以返回目标选择，重新检查并确认。");
                }
            });
    });

    const auto refreshEnabled = [owner, dialog, state, wizard, start, game, loaderVersion, copyName, mode, status, summary, steps, stopQuery, retryFlow, tree] {
        const bool selectedGame = game->currentIndex() > 0 && game->currentText() == game->itemText(game->currentIndex());
        const bool ready = selectedGame && (state->loader.isEmpty() || !state->loaderVersion.isEmpty());
        const bool named = mode->currentData().toBool() || !copyName->text().trimmed().isEmpty();
        const bool checksReady = !dialog->property("upgradeAssessmentReading").toBool()
            && !dialog->property("upgradeAssessmentQuerying").toBool() && dialog->upgradePendingModCount() == 0;
        const bool correctRoot = AppPaths::minecraftDir() == state->root;
        start->setEnabled(ready && named && checksReady && correctRoot && !state->preparing && !state->executing);
        wizard->setStepReady(0, ready && correctRoot);
        wizard->setStepReady(1, ready && checksReady && correctRoot);
        const QString name = copyName->text().trimmed();
        const bool copyValid = mode->currentData().toBool() || (InstanceFiles::validateDirName(name).isEmpty()
            && !QFileInfo::exists(AppPaths::versionDir(name)));
        wizard->setStepReady(2, ready && checksReady && correctRoot && named && copyValid);
        wizard->setStepReady(3, state->finished);
        wizard->setRunning(state->preparing || state->executing);
        stopQuery->setEnabled(dialog->property("upgradeAssessmentQuerying").toBool());
        const int kept = dialog->upgradeKeptMods().size(), pending = dialog->upgradePendingModCount();
        summary->setText(QObject::tr("当前实例：%1\n目标：Minecraft %2 · %3 %4\n\n保留启用：%5　禁用但保留文件：%6　待确认：%7\n\n%8\n\n存档跨版本后可能无法降级；执行前会检查磁盘空间。副本目前也使用完整快照事务，可能需要数倍空间。\n平台查询失败不等于没有适配版；本轮不会自动下载适配模组。%9")
            .arg(state->source, state->game, state->loader.isEmpty() ? QObject::tr("纯原版") : state->loader, state->loaderVersion)
            .arg(kept).arg(qMax(0, tree->property("upgradeModCount").toInt() - kept - pending)).arg(pending)
            .arg(mode->currentData().toBool() ? QObject::tr("更新当前实例：完整快照并校验后替换，保留回滚。")
                : QObject::tr("创建更新副本：%1，原实例保持不变。").arg(name))
            .arg(!copyValid ? QObject::tr("\n副本名称无效或已经存在，请修改。") : !correctRoot ? QObject::tr("\n游戏目录已改变，请关闭向导重新进入。") : QString()));
        if (state->preparing || state->executing || state->finished) {
            QStringList lines;
            for (const auto &task : owner->m_activityCenter->tasks()->all()) {
                if (task.id <= state->firstTask) continue;
                if (task.targetId != state->source && task.targetId != state->core && task.targetId != name) continue;
                lines.append(task.title);
                for (const auto &step : task.steps) {
                    const QString mark = step.state == Tasks::TaskStepState::Done ? QStringLiteral("✓ ")
                        : step.state == Tasks::TaskStepState::Running ? QStringLiteral("→ ")
                        : step.state == Tasks::TaskStepState::Failed ? QStringLiteral("× ") : QStringLiteral("· ");
                    lines.append(mark + step.name + (step.note.isEmpty() ? QString() : QStringLiteral(" — ") + step.note));
                }
                if (!task.detail.isEmpty()) lines.append(task.detail);
            }
            const QString joined = lines.join('\n');
            if (steps->property("snapshot").toString() != joined) {
                steps->setProperty("snapshot", joined); steps->clear(); steps->addItems(lines);
            }
        }
        retryFlow->setVisible(state->finished && !state->requiresReload);
        if (state->finished && !state->problem.isEmpty()) status->setText(state->problem);
        Q_UNUSED(loaderVersion);
        if (!state->preparing && !state->executing && !state->finished)
            status->setText(!state->problem.isEmpty() ? state->problem : ready ? QObject::tr("核心和加载器会自动准备，无需跳到下载页。") : QObject::tr("请选择目标 Minecraft 和加载器版本。"));
    };
    auto *poll = new QTimer(dialog); poll->setInterval(300);
    QObject::connect(poll, &QTimer::timeout, dialog, refreshEnabled); poll->start();
    const auto evaluate = [dialog, state, game, loader, loaderVersion, plan, refreshEnabled] {
        if (state->preparing || state->executing) return;
        const QString selectedGame = game->currentIndex() > 0 && game->currentText() == game->itemText(game->currentIndex()) ? game->currentData().toString() : QString();
        const bool changedCompatibility = selectedGame != state->game || loader->currentData().toString() != state->loader;
        if (state->evaluated && !changedCompatibility && loaderVersion->currentData().toString() == state->loaderVersion) {
            refreshEnabled(); return;
        }
        state->evaluated = true; state->game = selectedGame;
        state->loader = loader->currentData().toString(); state->loaderVersion = loaderVersion->currentData().toString();
        state->core.clear();
        state->problem.clear();
        dialog->setProperty("upgradeTargetLoader", state->loader);
        if (changedCompatibility) dialog->setProperty("upgradeModOverrides", QVariantMap());
        const QString target = state->game.isEmpty() ? QString() : QStringLiteral("@download:") + state->game;
        dialog->setTransferTargets("upgrade", {{state->game, target}});
        if (!target.isEmpty()) {
            auto *hidden = dialog->findChild<QComboBox *>(InstanceSettingsDialog::transferTargetComboName("upgrade"));
            const QSignalBlocker block(hidden); hidden->setCurrentIndex(1);
        }
        emit dialog->transferTargetChosen("upgrade", target);
        plan->setPlainText(state->game.isEmpty() ? QObject::tr("选择目标后会检查当前实例的模组。")
            : QObject::tr("Minecraft %1 · %2 %3\n创建副本保留全部用户文件；更新当前实例会先创建并校验完整快照。\n平台发行版与本地声明分别显示，旧模组按下方选择保留或禁用。")
                .arg(state->game, state->loader.isEmpty() ? QObject::tr("纯原版") : state->loader, state->loaderVersion));
        if (auto *old = dialog->findChild<QPushButton *>("instanceUpgradePrepareTarget")) old->hide();
        refreshEnabled();
    };
    const auto requestLoaders = [owner, state, game, loader, loaderVersion, evaluate] {
        if (state->preparing || state->executing) return;
        const QSignalBlocker block(loaderVersion); loaderVersion->clear();
        if (loader->currentData().toString().isEmpty()) loaderVersion->addItem(QObject::tr("无需加载器"), QString());
        else if (game->currentIndex() > 0) {
            loaderVersion->addItem(QObject::tr("正在获取版本…"), QString());
            owner->m_versionManager->requestLoaderVersions(game->currentData().toString(), loaderKind(loader->currentData().toString()));
        }
        evaluate();
    };
    QObject::connect(owner->m_versionManager, &VersionManager::loaderVersionsFetched, dialog,
        [state, game, loader, loaderVersion, evaluate](const QString &version, VersionManager::LoaderKind kind, bool ok,
            const LoaderMeta::LoaderVersionList &entries, const QString &problem) {
            if (state->preparing || state->executing) return;
            if (version != game->currentData().toString() || loader->currentData().toString().isEmpty()
                || kind != loaderKind(loader->currentData().toString())) return;
            const QSignalBlocker block(loaderVersion); loaderVersion->clear();
            loaderVersion->addItem(ok ? QObject::tr("请选择加载器版本") : QObject::tr("获取失败，点击刷新"), QString());
            loaderVersion->setToolTip(problem);
            int recommended = -1;
            for (const auto &entry : entries) {
                loaderVersion->addItem(entry.version + (entry.stable ? QString() : QObject::tr("（测试版）")), entry.version);
                if (recommended < 0 && entry.stable) recommended = loaderVersion->count() - 1;
            }
            if (recommended > 0) loaderVersion->setCurrentIndex(recommended);
            evaluate();
        });
    const auto populate = [owner, state, game, source, requestLoaders] {
        if (state->preparing || state->executing) return;
        const QString previous = game->currentData().toString();
        const QSignalBlocker block(game); game->clear(); game->addItem(QObject::tr("请选择 Minecraft 版本"), QString());
        QStringList versions = owner->m_versionManager->availableVersions();
        if (!source.realVersion.isEmpty() && !versions.contains(source.realVersion)) versions.append(source.realVersion);
        versions.removeDuplicates(); for (const auto &version : versions) game->addItem(version, version);
        const int index = game->findData(previous); game->setCurrentIndex(index >= 0 ? index : 0);
        requestLoaders();
    };
    QObject::connect(owner->m_versionManager, &VersionManager::versionsFetched, dialog, [populate](const QStringList &) { populate(); });
    QObject::connect(game, QOverload<int>::of(&QComboBox::currentIndexChanged), dialog, requestLoaders);
    QObject::connect(game, &QComboBox::editTextChanged, dialog, evaluate);
    QObject::connect(loader, QOverload<int>::of(&QComboBox::currentIndexChanged), dialog, requestLoaders);
    QObject::connect(loaderVersion, QOverload<int>::of(&QComboBox::currentIndexChanged), dialog, evaluate);
    QObject::connect(retry, &QPushButton::clicked, dialog, [owner, populate] { populate(); owner->m_versionManager->fetchAvailableVersions(); });
    QObject::connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged), dialog, [mode, copyName, refreshEnabled] {
        copyName->setVisible(!mode->currentData().toBool()); refreshEnabled();
    });
    QObject::connect(copyName, &QLineEdit::textChanged, dialog, refreshEnabled);
    const auto execute = [owner, dialog = QPointer<InstanceSettingsDialog>(dialog), state, mode, copyName, refreshEnabled, status] {
        if (!dialog || AppPaths::minecraftDir() != state->root) return;
        state->executing = true; state->finished = false;
        status->setText(QObject::tr("正在复制或执行完整快照更新，请等待事务结果…"));
        const QStringList keep = dialog->upgradeKeptMods();
        const auto completed = [dialog, state, refreshEnabled, status](bool changed, const QString &problem) {
            state->executing = false;
            state->finished = true; state->requiresReload = changed;
            state->problem = !problem.isEmpty() ? problem : changed ? QObject::tr("更新已完成。点击完成返回；实例设置将重新读取新版本。")
                : QObject::tr("本次更新未执行或已取消，原实例没有发布新核心。");
            if (dialog) { status->setText(state->problem); refreshEnabled(); }
        };
        if (mode->currentData().toBool()) owner->m_instanceTransferController->upgradeInPlace(state->source, state->core, completed, keep, true);
        else {
            const QString name = copyName->text().trimmed();
            owner->m_instanceTransferController->copyInstanceInBackground(state->source, state->root, name,
                [owner, dialog, state, name, keep, completed](bool ok, const QString &problem) {
                    if (!ok || !dialog || AppPaths::minecraftDir() != state->root) { completed(false, problem); return; }
                    owner->m_instanceTransferController->upgradeInPlace(name, state->core, completed, keep, true);
                }, false);
        }
    };
    QObject::connect(start, &QPushButton::clicked, dialog,
        [owner, dialog, state, mode, copyName, status, selectors, start, refreshEnabled, execute] {
            if (state->preparing || state->executing || dialog->property("upgradeAssessmentReading").toBool()
                || dialog->property("upgradeAssessmentQuerying").toBool() || dialog->upgradePendingModCount() != 0) return;
            state->finished = false; state->problem.clear();
            if (AppPaths::minecraftDir() != state->root || owner->m_downloadInFlight || owner->m_instanceTransferController->copying()
                || owner->installingVersionIds().contains(state->source)
                || owner->m_launcherManager->isVersionRunning(state->source)) {
                InlineMessage::warning(owner->m_ui->overlay(), QObject::tr("暂时不能更新"), QObject::tr("游戏目录已改变，或当前实例/下载/文件操作正在运行。请完成后重试。")); return;
            }
            if (!mode->currentData().toBool()) {
                const QString name = copyName->text().trimmed();
                const QString error = InstanceFiles::validateDirName(name);
                if (!error.isEmpty() || QFileInfo::exists(AppPaths::versionDir(name))) {
                    InlineMessage::warning(owner->m_ui->overlay(), QObject::tr("副本名称不可用"), error.isEmpty() ? QObject::tr("已经存在同名实例，请换一个名字。") : error); return;
                }
            }
            if (mode->currentData().toBool()
                && state->source.compare(owner->m_versionManager->instanceGameVersion(state->source), Qt::CaseInsensitive) == 0) {
                InlineMessage::warning(owner->m_ui->overlay(), QObject::tr("请创建更新副本"),
                    QObject::tr("当前实例是共享的原版核心，不能直接替换。选择创建更新副本即可保留原版和用户文件。")); return;
            }
            if (!state->core.isEmpty() && QFileInfo::exists(AppPaths::versionDir(state->core) + '/' + state->core + ".json")) {
                execute(); return;
            }
            QString installerJava = owner->m_javaSettings->javaPath();
            if (!state->loader.isEmpty() && !owner->pickInstallerJava(state->game,
                VersionManager::loaderIndexForKind(loaderKind(state->loader)), &installerJava)) return;
            if (!InlineMessage::question(owner->m_ui->overlay(), QObject::tr("确认实例更新"),
                    QObject::tr("更新到 Minecraft %1 · %2 %3。\n\n模组按列表逐项保留或禁用，不会自动下载适配模组。无法确认兼容性的模组必须先选择处理方式。\n存档跨版本后可能无法降级；请勿同时运行实例。\n\n下一步自动准备核心，之后执行副本或完整快照更新。")
                        .arg(state->game, state->loader.isEmpty() ? QObject::tr("纯原版") : state->loader, state->loaderVersion),
                    QObject::tr("准备并继续"), QObject::tr("取消"), false)) return;
            // Confirmation runs a nested event loop: recheck ownership before any write.
            if (AppPaths::minecraftDir() != state->root || owner->m_downloadInFlight
                || owner->m_instanceTransferController->copying() || owner->installingVersionIds().contains(state->source)
                || owner->m_launcherManager->isVersionRunning(state->source)) return;
            state->core = QStringLiteral("wtfml-update-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
            state->preparing = true; selectors->setEnabled(false); start->setEnabled(false);
            status->setText(QObject::tr("正在准备目标核心与加载器，完成后自动继续…"));
            auto *vm = owner->m_versionManager;
            vm->setProperty("instanceUpdatePreparedId", state->core);
            owner->m_installingInstanceId = state->core;
            owner->m_installPendingPresets.clear(); owner->m_installPendingPresetInstance.clear();
            owner->m_installPresetFingerprints.clear(); owner->m_installPresetModsReady = false;
            owner->m_installPresetModsOk = true; owner->m_installPresetModsReport.clear();
            owner->markFileOperationBusy(state->source);
            owner->setDownloadUiBusy(true); owner->beginDownloadTask();
            const auto core = state->core;
            QObject::connect(vm, &VersionManager::versionDownloadFinished, owner,
                [owner, guard = QPointer<InstanceSettingsDialog>(dialog), state, core, selectors = QPointer<QWidget>(selectors), status = QPointer<QLabel>(status), refreshEnabled, execute]
                (const QString &id, bool ok, const QString &problem) {
                    if (!state->preparing || id != core) return;
                    state->preparing = false; owner->releaseFileOperationBusy(state->source);
                    owner->m_versionManager->setProperty("instanceUpdatePreparedId", QVariant());
                    if (!guard) return;
                    if (selectors) selectors->setEnabled(true);
                    if (!ok || AppPaths::minecraftDir() != state->root) {
                        if (!ok) state->core.clear();
                        state->finished = true;
                        state->problem = ok ? QObject::tr("游戏目录已改变，未继续更新。") : problem;
                        if (status) status->setText(state->problem);
                        refreshEnabled(); return;
                    }
                    owner->m_versionManager->clearVersionJsonCache();
                    if (guard->property("upgradeAssessmentReading").toBool()) {
                        state->finished = true;
                        state->problem = QObject::tr("核心已准备。模组检查完成后，点击开始更新继续。");
                        refreshEnabled(); return;
                    }
                    QTimer::singleShot(0, guard, execute);
                });
            QTimer::singleShot(900000, vm, [vm, core] {
                if (vm->property("instanceUpdatePreparedId").toString() == core) vm->cancelActiveDownload();
            });
            if (state->loader.isEmpty()) vm->installNamedVanilla(state->core, state->game);
            else vm->installNamedLoader(state->core, state->game, loaderKind(state->loader), state->loaderVersion, installerJava);
        });
    populate();
}
