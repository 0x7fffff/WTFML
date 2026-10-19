#pragma once

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <functional>

namespace InstanceUpgrade {
// Worker-only disk transaction. The caller locks both instances and prepares
// candidateDir() with core files already named for instanceId.
enum class Stage { Checking, Snapshot, VerifySnapshot, RetainOriginal, Publish, Verify, Done };
struct Request {
    QString gameDir;
    QString instanceId;
    QString transactionId;
    QString label;
    // Optional operation quota, can only reduce the measured free-space budget.
    qint64 spaceBudgetBytes = -1;
    QByteArray originalFingerprint{};
};
struct Result {
    bool ok = false;
    bool rolledBack = false;
    bool rollbackAvailable = false;
    QString problem;
    QString snapshotZip;
    int disabledMods = 0;
    bool requiresReload = false;
};
struct Core {
    QJsonObject mergedJson;
    QString clientJar;
    QString gameVersion;
    QString loaderName;
    QString loaderVersion;
    bool disableOldMods = true;
};
QString transactionDir(const Request &request);
QString candidateDir(const Request &request);
QString dependentInstanceProblem(const QString &gameDir, const QString &instanceId);
// Preserves all original user files and identity, replaces the merged core and
// client jar only. The target instance's saves/mods/config are never imported.
Result prepareAndCommit(Request request, const Core &core,
                        const std::function<void(Stage)> &progress = {},
                        const std::function<bool()> &cancelled = {});
Result commitPrepared(const Request &request,
                      const std::function<void(Stage)> &progress = {},
                      const std::function<bool()> &cancelled = {});
// Restores the retained original by renaming; preserves the replaced version as
// well. Never deletes an existing directory to make a rollback succeed.
Result rollback(const Request &request);
// Only returns journals in this game's private transaction directory for this id.
QStringList recoverableTransactions(const QString &gameDir, const QString &instanceId);
QStringList recoverInterrupted(const QString &gameDir);
} // namespace InstanceUpgrade
