/**
 * @file AgentLauncherToolsTests.cpp
 * @brief 启动器工具：只读自动、完整清单/写操作审批、拒绝不改状态、诊断结果经投影、
 *        `settings.get` 不外发路径、备份创建/恢复走审批。
 */

#include "Support.hpp"

#include "agent/AgentLauncherTools.hpp"
#include "agent/AgentTool.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

namespace {

Agent::ToolCall callFor(const QString &name, const QJsonObject &arguments = {}) {
    Agent::ToolCall call;
    call.id = QStringLiteral("call-1");
    call.name = name;
    call.arguments = arguments;
    return call;
}

/// 假宿主：只记调用，不碰真盘；每个方法的返回值都能逐格摆。
class FakeHost final : public AgentLauncherHost {
public:
    QList<AgentDiagnosticProjection::InstanceFacts> instances;
    StartupDiagnosis::Report startupReport;
    QList<AgentHostModEntry> modEntries;
    QList<AgentHostBackupRef> backupEntries;
    QString settingsProblem;
    QJsonObject settingsValues;

    bool created = false;
    bool restored = false;
    bool renamed = false;
    QString lastSettingKey;
    QJsonValue lastSettingValue;
    QString lastPage;

    bool instanceList(QList<AgentDiagnosticProjection::InstanceFacts> *items,
                      QString *problem) override {
        Q_UNUSED(problem);
        *items = instances;
        return true;
    }
    bool instanceFacts(const QString &instanceId,
                       AgentDiagnosticProjection::InstanceFacts *facts,
                       QString *problem) override {
        Q_UNUSED(problem);
        for (const auto &facts0 : instances) {
            if (facts0.instanceId == instanceId) {
                *facts = facts0;
                return true;
            }
        }
        *facts = instances.value(0);
        return !facts->instanceId.isEmpty();
    }
    bool startupDiagnosis(const QString &instanceId, StartupDiagnosis::Report *report,
                          QString *problem) override {
        Q_UNUSED(instanceId);
        Q_UNUSED(problem);
        *report = startupReport;
        return true;
    }
    bool crashFacts(const QString &instanceId, GameExitDiagnosis::Facts *facts,
                    QStringList *hints, QString *problem) override {
        Q_UNUSED(instanceId);
        Q_UNUSED(problem);
        *facts = GameExitDiagnosis::Facts{};
        hints->clear();
        return true;
    }
    bool logTail(const QString &instanceId, int lines, QString *text, QString *relativeSource,
                 QString *problem) override {
        Q_UNUSED(instanceId);
        Q_UNUSED(lines);
        Q_UNUSED(problem);
        *text = QStringLiteral("password = hunter2\n[12:00:00] [main/INFO]: 启动完成");
        *relativeSource = QStringLiteral("logs/latest.log");
        return true;
    }
    bool crashReports(const QString &instanceId, QList<AgentHostCrashReport> *items,
                      QString *problem) override {
        Q_UNUSED(instanceId);
        Q_UNUSED(problem);
        items->clear();
        AgentHostCrashReport report;
        report.fileName = QStringLiteral("crash-2026-01-01_12.00.00-client.txt");
        report.modifiedAt = QDateTime::currentDateTimeUtc();
        report.bytes = 1024;
        report.headText = QStringLiteral("java.lang.OutOfMemoryError");
        items->append(report);
        return true;
    }
    bool integrity(const QString &instanceId, VersionIntegrity::Report *report,
                   QString *problem) override {
        Q_UNUSED(instanceId);
        Q_UNUSED(problem);
        report->checked = 100;
        return true;
    }
    bool mods(const QString &instanceId, QList<AgentHostModEntry> *items,
              QString *problem) override {
        Q_UNUSED(instanceId);
        Q_UNUSED(problem);
        *items = modEntries;
        return true;
    }
    bool setModEnabled(const QString &instanceId, const QString &fileName, bool enabled,
                       QString *previous, QString *problem) override {
        Q_UNUSED(instanceId);
        Q_UNUSED(enabled);
        Q_UNUSED(problem);
        renamed = true;
        *previous = fileName;
        return true;
    }
    bool settings(AgentHostSettings *settings, QString *problem) override {
        Q_UNUSED(problem);
        settings->values = settingsValues;
        settings->writableKeys = {QStringLiteral("memory.xmx")};
        return true;
    }
    bool applySetting(const QString &key, const QJsonValue &value, QJsonObject *applied,
                      QString *problem) override {
        Q_UNUSED(applied);
        Q_UNUSED(problem);
        lastSettingKey = key;
        lastSettingValue = value;
        return true;
    }
    bool javaVersions(QList<int> *majors, QString *problem) override {
        Q_UNUSED(problem);
        *majors = {8, 17, 21};
        return true;
    }
    bool backups(QList<AgentHostBackupRef> *items, QString *problem) override {
        Q_UNUSED(problem);
        *items = backupEntries;
        return true;
    }
    bool planBackup(const QString &instanceId, const QStringList &scopes,
                    AgentHostBackupPlan *plan, QString *problem) override {
        Q_UNUSED(instanceId);
        Q_UNUSED(problem);
        plan->scope = scopes.join(QStringLiteral("+"));
        plan->relativePaths = {QStringLiteral("config/a.toml")};
        plan->bytes = 128;
        plan->saveLocation = QStringLiteral("backups/maintenance");
        return true;
    }
    bool createBackup(const QString &sessionId, const QString &instanceId,
                      const AgentHostBackupPlan &plan, QString *backupId,
                      QString *problem) override {
        Q_UNUSED(sessionId);
        Q_UNUSED(instanceId);
        Q_UNUSED(plan);
        Q_UNUSED(problem);
        created = true;
        *backupId = QStringLiteral("backup-1");
        return true;
    }
    bool restoreBackup(const QString &sessionId, const QString &backupId,
                       QString *preRestoreBackupId, QString *problem) override {
        Q_UNUSED(sessionId);
        Q_UNUSED(backupId);
        Q_UNUSED(problem);
        restored = true;
        *preRestoreBackupId = QStringLiteral("snapshot-1");
        return true;
    }
    bool upgradeInspect(const QString &instanceId, QJsonObject *data, QString *problem) override {
        Q_UNUSED(instanceId);
        Q_UNUSED(problem);
        *data = QJsonObject{{QStringLiteral("targets"), QJsonArray{}}};
        return true;
    }
    bool openPage(const QString &page, const QString &instanceId, QString *problem) override {
        Q_UNUSED(instanceId);
        Q_UNUSED(problem);
        lastPage = page;
        return true;
    }
};

AgentToolContext contextFor() {
    AgentToolContext context;
    context.sessionId = QStringLiteral("session-1");
    context.instanceId = QStringLiteral("1.20.1-Fabric");
    context.instanceName = QStringLiteral("我的整合包");
    context.gameRootId = QStringLiteral("root-1");
    context.rootPath = QStringLiteral("C:/game/versions/1.20.1-Fabric");
    context.providerHost = QStringLiteral("api.example.com");
    return context;
}

QString approvalIdOf(const AgentToolResult &result) {
    return result.approval.value(QStringLiteral("approvalId")).toString();
}

}  // namespace

void runAgentLauncherToolsTests() {
    FakeHost host;
    AgentDiagnosticProjection::InstanceFacts facts;
    facts.instanceId = QStringLiteral("1.20.1-Fabric");
    facts.instanceName = QStringLiteral("我的整合包");
    facts.gameVersion = QStringLiteral("1.20.1");
    facts.loaderName = QStringLiteral("Fabric");
    facts.modCount = 2;
    host.instances.append(facts);

    StartupDiagnosis::Finding finding;
    finding.severity = StartupDiagnosis::Severity::Problem;
    finding.title = QStringLiteral("没有指定 Java");
    finding.detail = QStringLiteral("读的是 D:\\mc\\java\\bin\\java.exe");
    host.startupReport.findings.append(finding);
    host.startupReport.problems = 1;

    AgentHostModEntry mod;
    mod.fileName = QStringLiteral("sodium.jar");
    mod.enabled = true;
    mod.bytes = 1024;
    host.modEntries.append(mod);
    AgentHostModEntry disabled;
    disabled.fileName = QStringLiteral("old.jar.disabled");
    disabled.enabled = false;
    host.modEntries.append(disabled);

    AgentHostBackupRef backup;
    backup.backupId = QStringLiteral("backup-1");
    backup.owner = QStringLiteral("agent");
    backup.label = QStringLiteral("backup.create：config");
    host.backupEntries.append(backup);

    host.settingsValues = QJsonObject{{QStringLiteral("memory.xmx"), 4096},
                                      {QStringLiteral("java.path"),
                                       QStringLiteral("C:/Java/bin/java.exe")}};

    AgentLauncherTools tools(&host, [](QString *) { return contextFor(); });

    // ---------- 只读：实例清单 ----------
    AgentToolResult result = tools.invoke(callFor(QStringLiteral("instance.list")));
    check(result.status == AgentToolResult::Status::Ok, "启动器工具：instance.list 默认自动执行");
    check(result.data.value(QStringLiteral("count")).toInt() == 1, "启动器工具：instance.list 给总数");
    check(!result.data.contains(QStringLiteral("instances")),
          "启动器工具：instance.list 默认不带名称清单");

    result = tools.invoke(callFor(QStringLiteral("instance.list"),
                                 QJsonObject{{QStringLiteral("detail"), QStringLiteral("full")}}));
    check(result.status == AgentToolResult::Status::ApprovalRequired,
          "启动器工具：完整实例清单要逐次确认");
    {
        const QString approvalId = approvalIdOf(result);
        const AgentToolResult resolved = tools.resolveApproval(approvalId, false);
        check(resolved.error.kind == Agent::ErrorKind::ApprovalDenied,
              "启动器工具：拒绝完整清单后不交付数据");
    }
    result = tools.invoke(callFor(QStringLiteral("instance.list"),
                                 QJsonObject{{QStringLiteral("detail"), QStringLiteral("full")}}));
    {
        const QString approvalId = approvalIdOf(result);
        const AgentToolResult resolved = tools.resolveApproval(approvalId, true);
        check(resolved.status == AgentToolResult::Status::Ok,
              "启动器工具：批准后才交付完整清单");
        check(resolved.data.value(QStringLiteral("instances")).toArray().size() == 1,
              "启动器工具：批准后清单里有那一项");
    }

    // ---------- 未锁定实例 ----------
    AgentLauncherTools noLock(&host, [](QString *) { return AgentToolContext{}; });
    result = noLock.invoke(callFor(QStringLiteral("instance.context")));
    check(result.status == AgentToolResult::Status::Error, "启动器工具：未锁定实例时拒绝");
    check(result.data.value(QStringLiteral("needsInstanceLock")).toBool(),
          "启动器工具：回 NeedsInstanceLock 标记");

    // ---------- 诊断投影 ----------
    result = tools.invoke(callFor(QStringLiteral("diagnostics.startup")));
    check(result.status == AgentToolResult::Status::Ok, "启动器工具：启动诊断自动执行");
    const QString startupText = QString::fromUtf8(
        QJsonDocument(result.data).toJson(QJsonDocument::Compact));
    check(!startupText.contains(QStringLiteral("D:\\mc")), "启动器工具：诊断结果不含本机路径");
    check(result.data.value(QStringLiteral("evidence")).toArray().size() == 1,
          "启动器工具：诊断带一条证据");

    // ---------- 日志：自动执行 + 凭据遮挡 ----------
    result = tools.invoke(callFor(QStringLiteral("logs.tail"), QJsonObject{{QStringLiteral("lines"), 20}}));
    check(result.status == AgentToolResult::Status::Ok, "启动器工具：日志结尾自动执行");
    const QString logContent = result.data.value(QStringLiteral("content")).toString();
    check(!logContent.contains(QStringLiteral("hunter2")), "启动器工具：日志里的口令被遮挡");
    check(result.data.value(QStringLiteral("autoReadNotice")).toBool(),
          "启动器工具：自动读取带“可能发送给模型服务”的告知");

    // ---------- 崩溃报告清单 ----------
    result = tools.invoke(callFor(QStringLiteral("crash.list")));
    check(result.status == AgentToolResult::Status::Ok, "启动器工具：崩溃报告概况自动执行");
    result = tools.invoke(callFor(QStringLiteral("crash.list"),
                                  QJsonObject{{QStringLiteral("detail"), QStringLiteral("full")}}));
    check(result.status == AgentToolResult::Status::ApprovalRequired,
          "启动器工具：崩溃报告文件名清单要确认");
    tools.cancelPending();

    // ---------- 模组：概况自动、清单确认 ----------
    result = tools.invoke(callFor(QStringLiteral("mods.list")));
    check(result.status == AgentToolResult::Status::Ok, "启动器工具：模组概况自动执行");
    check(result.data.value(QStringLiteral("enabled")).toInt() == 1,
          "启动器工具：模组启用数正确");
    check(result.data.value(QStringLiteral("disabled")).toInt() == 1,
          "启动器工具：模组停用数正确");
    check(!result.data.contains(QStringLiteral("names")), "启动器工具：概况不带文件名");
    result = tools.invoke(callFor(QStringLiteral("mods.list"),
                                  QJsonObject{{QStringLiteral("detail"), QStringLiteral("full")}}));
    check(result.status == AgentToolResult::Status::ApprovalRequired,
          "启动器工具：完整模组文件名要确认");
    tools.cancelPending();

    // ---------- 模组启停：必须审批，拒绝不改盘 ----------
    host.renamed = false;
    result = tools.invoke(callFor(QStringLiteral("mods.setEnabled"),
                                  QJsonObject{{QStringLiteral("fileName"), QStringLiteral("sodium.jar")},
                                              {QStringLiteral("enabled"), false}}));
    check(result.status == AgentToolResult::Status::ApprovalRequired,
          "启动器工具：启停模组要审批");
    {
        const AgentToolResult denied = tools.resolveApproval(approvalIdOf(result), false);
        check(denied.error.kind == Agent::ErrorKind::ApprovalDenied, "启动器工具：拒绝启停");
        check(!host.renamed, "启动器工具：拒绝后没有改任何文件");
    }
    result = tools.invoke(callFor(QStringLiteral("mods.setEnabled"),
                                  QJsonObject{{QStringLiteral("fileName"), QStringLiteral("sodium.jar")},
                                              {QStringLiteral("enabled"), false}}));
    {
        const AgentToolResult approved = tools.resolveApproval(approvalIdOf(result), true);
        check(approved.status == AgentToolResult::Status::Ok, "启动器工具：批准后执行启停");
        check(host.renamed, "启动器工具：批准后宿主被调用");
    }
    result = tools.invoke(callFor(QStringLiteral("mods.setEnabled"),
                                  QJsonObject{{QStringLiteral("fileName"), QStringLiteral("sodium.jar")},
                                              {QStringLiteral("enabled"), true}}));
    check(result.status == AgentToolResult::Status::Error,
          "启动器工具：状态没变时拒绝（已经是启用）");

    // ---------- 设置：读不外发路径、写要审批 ----------
    result = tools.invoke(callFor(QStringLiteral("settings.get")));
    check(result.status == AgentToolResult::Status::Ok, "启动器工具：settings.get 自动执行");
    const QJsonObject values = result.data.value(QStringLiteral("values")).toObject();
    check(!values.contains(QStringLiteral("java.path")), "启动器工具：settings.get 不返回 java.path");
    check(values.value(QStringLiteral("memory.xmx")).toInt() == 4096,
          "启动器工具：settings.get 返回非路径字段");

    host.lastSettingKey.clear();
    result = tools.invoke(callFor(QStringLiteral("settings.set"),
                                  QJsonObject{{QStringLiteral("key"), QStringLiteral("memory.xmx")},
                                              {QStringLiteral("value"), QStringLiteral("6144")}}));
    check(result.status == AgentToolResult::Status::ApprovalRequired,
          "启动器工具：settings.set 要审批");
    check(result.approval.value(QStringLiteral("summary")).toString().contains(QStringLiteral("6144")),
          "启动器工具：审批卡写出新值");
    {
        const AgentToolResult approved = tools.resolveApproval(approvalIdOf(result), true);
        check(approved.status == AgentToolResult::Status::Ok, "启动器工具：批准后写入设置");
        check(host.lastSettingKey == QStringLiteral("memory.xmx"), "启动器工具：写入的是那个键");
        check(host.lastSettingValue.toInt() == 6144, "启动器工具：写入的是新值");
    }
    result = tools.invoke(callFor(QStringLiteral("settings.set"),
                                  QJsonObject{{QStringLiteral("key"), QStringLiteral("world.root")},
                                              {QStringLiteral("value"), QStringLiteral("x")}}));
    check(result.status == AgentToolResult::Status::Error, "启动器工具：白名单外的设置键被拒");
    result = tools.invoke(callFor(QStringLiteral("settings.set"),
                                  QJsonObject{{QStringLiteral("key"), QStringLiteral("memory.xmx")},
                                              {QStringLiteral("value"), QStringLiteral("12")}}));
    check(result.status == AgentToolResult::Status::Error, "启动器工具：不合理的内存值被拒");

    // ---------- 备份 ----------
    result = tools.invoke(callFor(QStringLiteral("backup.list")));
    check(result.status == AgentToolResult::Status::Ok, "启动器工具：backup.list 自动执行");
    check(result.data.value(QStringLiteral("count")).toInt() == 1, "启动器工具：列出备份条数");

    host.created = false;
    result = tools.invoke(callFor(QStringLiteral("backup.create"),
                                  QJsonObject{{QStringLiteral("reason"), QStringLiteral("改配置前")}}));
    check(result.status == AgentToolResult::Status::ApprovalRequired,
          "启动器工具：backup.create 必须审批");
    {
        const AgentToolResult denied = tools.resolveApproval(approvalIdOf(result), false);
        check(denied.error.kind == Agent::ErrorKind::ApprovalDenied, "启动器工具：拒绝备份");
        check(!host.created, "启动器工具：拒绝后没有创建备份");
    }
    result = tools.invoke(callFor(QStringLiteral("backup.create"),
                                  QJsonObject{{QStringLiteral("reason"), QStringLiteral("改配置前")}}));
    {
        const AgentToolResult approved = tools.resolveApproval(approvalIdOf(result), true);
        check(approved.status == AgentToolResult::Status::Ok, "启动器工具：批准后创建备份");
        check(host.created, "启动器工具：宿主被要求创建备份");
        check(approved.data.value(QStringLiteral("owner")).toString() == QStringLiteral("agent"),
              "启动器工具：备份 owner=agent");
    }

    host.restored = false;
    result = tools.invoke(callFor(QStringLiteral("backup.restore"),
                                  QJsonObject{{QStringLiteral("backupId"), QStringLiteral("backup-1")}}));
    check(result.status == AgentToolResult::Status::ApprovalRequired,
          "启动器工具：backup.restore 必须审批");
    {
        const AgentToolResult approved = tools.resolveApproval(approvalIdOf(result), true);
        check(approved.status == AgentToolResult::Status::Ok, "启动器工具：批准后恢复");
        check(host.restored, "启动器工具：宿主被要求恢复");
        check(approved.data.value(QStringLiteral("preRestoreBackupId")).toString() ==
                  QStringLiteral("snapshot-1"),
              "启动器工具：恢复前留了快照");
    }
    result = tools.invoke(callFor(QStringLiteral("backup.restore"),
                                  QJsonObject{{QStringLiteral("backupId"), QStringLiteral("nope")}}));
    check(result.status == AgentToolResult::Status::Error, "启动器工具：未知备份 id 被拒");

    // ---------- 只读检查与打开页面 ----------
    result = tools.invoke(callFor(QStringLiteral("upgrade.inspect")));
    check(result.status == AgentToolResult::Status::Ok, "启动器工具：upgrade.inspect 只读自动");
    result = tools.invoke(callFor(QStringLiteral("launcher.openPage"),
                                  QJsonObject{{QStringLiteral("page"), QStringLiteral("logs")}}));
    check(result.status == AgentToolResult::Status::Ok, "启动器工具：openPage 打开页面");
    check(host.lastPage == QStringLiteral("logs"), "启动器工具：页面名传到宿主");
    result = tools.invoke(callFor(QStringLiteral("launcher.openPage"),
                                  QJsonObject{{QStringLiteral("page"), QStringLiteral("settings")}}));
    check(result.status == AgentToolResult::Status::Error,
          "启动器工具：未知页面被 schema 拒绝");

    // ---------- schema / 未知参数 ----------
    result = tools.invoke(callFor(QStringLiteral("diagnostics.startup"),
                                  QJsonObject{{QStringLiteral("instanceId"), QStringLiteral("other")}}));
    check(result.status == AgentToolResult::Status::Error,
          "启动器工具：模型传入 instanceId 被拒（只认会话锁定）");
    result = tools.invoke(callFor(QStringLiteral("no.such.tool")));
    check(result.status == AgentToolResult::Status::Error, "启动器工具：未知工具被拒");
}
