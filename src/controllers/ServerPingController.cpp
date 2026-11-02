#include "ServerPingController.hpp"

#include "InstanceSettingsDialog.hpp"

#include <QIcon>
#include <QPixmap>

#include <utility>

#include "util/ImageDecode.hpp"

ServerPingController::ServerPingController(Host host, QObject *parent)
    : QObject(parent), m_host(std::move(host)), m_pinger(new ServerPing::Pinger(this)) {
    connect(m_pinger, &ServerPing::Pinger::oneFinished, this,
            &ServerPingController::oneFinished);
    connect(m_pinger, &ServerPing::Pinger::allFinished, this,
            &ServerPingController::allFinished);
}

InstanceSettingsDialog::ServerRowStatus ServerPingController::rowFromCache(
    const ServerStatusCache::Entry &entry) {
    InstanceSettingsDialog::ServerRowStatus row;
    row.tested = true;
    row.fromCache = true;
    row.online = entry.online;
    row.timedOut = entry.timedOut;
    row.latencyMs = entry.latencyMs;
    row.playersOnline = entry.playersOnline;
    row.playersMax = entry.playersMax;
    row.versionName = entry.versionName;
    row.samplePlayers = entry.samplePlayers;
    row.motdRaw = entry.motdRaw;
    row.motd = ServerPing::cleanMotd(entry.motdRaw);
    if (!entry.iconPng.isEmpty()) {
        // 图标是**服务器给的字节**（缓存里那一份也一样）：走 ImageDecode 而不是
        // `QPixmap::loadFromData` —— 那一层带尺寸上限，挡得住"声明 6 万乘 6 万、
        // 实际只有几十个字节"的解压炸弹。
        const QImage icon = ImageDecode::decode(entry.iconPng);
        if (!icon.isNull()) row.icon = QIcon(QPixmap::fromImage(icon));
    }
    return row;
}

QHash<int, ServerStatusCache::Entry> ServerPingController::cachedEntries(
    const QStringList &addresses) {
    QHash<int, ServerStatusCache::Entry> result;
    for (int i = 0; i < addresses.size(); ++i) {
        const auto entry = ServerStatusCache::load(addresses.at(i));
        if (entry.valid) result.insert(i, entry);
    }
    return result;
}

QList<InstanceSettingsDialog::ServerRowStatus> ServerPingController::cachedRows(
    const QStringList &addresses) {
    const auto cached = cachedEntries(addresses);
    QList<InstanceSettingsDialog::ServerRowStatus> rows;
    rows.reserve(addresses.size());
    for (int i = 0; i < addresses.size(); ++i) {
        rows.append(cached.contains(i) ? rowFromCache(cached.value(i))
                                       : InstanceSettingsDialog::ServerRowStatus{});
    }
    return rows;
}

void ServerPingController::ping(InstanceSettingsDialog *dialog, const QString &versionId) {
    if (dialog == nullptr || !m_host.serverList) return;
    const ServerList servers = m_host.serverList(versionId);
    if (servers.isEmpty()) return;
    QStringList addresses;
    for (const auto &server : servers) addresses.append(server.second);
    if (m_run.active) {
        if (m_run.addresses == addresses) {
            m_run.dialog = dialog;
            pushRows();
            dialog->setServerPinging(true);
            return;
        }
        m_run.dialog = nullptr;
        m_run.pendingAddresses = addresses;
        m_run.pendingDialog = dialog;
        m_run.pendingVersionId = versionId;
        dialog->setServerStatuses(cachedRows(addresses));
        dialog->setServerPinging(true);
        return;
    }
    begin(dialog, versionId, addresses);
}

void ServerPingController::begin(InstanceSettingsDialog *dialog, const QString &versionId,
                                 const QStringList &addresses) {
    Run run;
    run.dialog = dialog;
    run.versionId = versionId;
    run.addresses = addresses;
    run.cached = cachedEntries(addresses);
    run.active = true;
    m_run = run;
    pushRows();
    if (dialog != nullptr) dialog->setServerPinging(true);
    m_pinger->start(addresses);
}

void ServerPingController::pushRows() {
    if (m_run.dialog == nullptr) return;
    QList<InstanceSettingsDialog::ServerRowStatus> rows;
    rows.reserve(m_run.addresses.size());
    for (int i = 0; i < m_run.addresses.size(); ++i) {
        InstanceSettingsDialog::ServerRowStatus row;
        if (m_run.fresh.contains(i)) {
            const auto status = m_run.fresh.value(i);
            row.tested = true;
            row.online = status.ok;
            row.error = status.error;
            row.timedOut = status.timedOut;
            row.latencyMs = status.latencyMs;
            row.playersOnline = status.playersOnline;
            row.playersMax = status.playersMax;
            row.versionName = status.versionName;
            row.samplePlayers = status.samplePlayers;
            row.motd = status.motd;
            row.motdRaw = status.motdRaw;
            if (!status.iconPng.isEmpty()) {
                // 同 rowFromCache() 那一段：外部字节先过 ImageDecode 的尺寸上限。
                const QImage icon = ImageDecode::decode(status.iconPng);
                if (!icon.isNull()) row.icon = QIcon(QPixmap::fromImage(icon));
            }
        } else if (m_run.cached.contains(i)) {
            row = rowFromCache(m_run.cached.value(i));
        }
        rows.append(row);
    }
    m_run.dialog->setServerStatuses(rows);
}

void ServerPingController::oneFinished(int index, const ServerPing::Status &status) {
    if (!m_run.active) return;
    const QString address = m_run.addresses.value(index);
    if (address.isEmpty()) return;
    ServerStatusCache::Entry entry;
    entry.valid = true;
    entry.motdRaw = status.motdRaw;
    entry.iconPng = status.iconPng;
    entry.online = status.ok;
    entry.timedOut = status.timedOut;
    entry.latencyMs = status.latencyMs;
    entry.playersOnline = status.playersOnline;
    entry.playersMax = status.playersMax;
    entry.versionName = status.versionName;
    entry.samplePlayers = status.samplePlayers;
    ServerStatusCache::save(address, entry);
    m_run.fresh.insert(index, status);
    pushRows();
}

void ServerPingController::allFinished() {
    if (m_run.dialog != nullptr) m_run.dialog->setServerPinging(false);
    m_run.active = false;
    if (m_run.pendingAddresses.isEmpty()) return;
    const QStringList addresses = m_run.pendingAddresses;
    InstanceSettingsDialog *dialog = m_run.pendingDialog;
    const QString versionId = m_run.pendingVersionId;
    m_run.pendingAddresses.clear();
    m_run.pendingDialog = nullptr;
    m_run.pendingVersionId.clear();
    if (dialog != nullptr) begin(dialog, versionId, addresses);
}
