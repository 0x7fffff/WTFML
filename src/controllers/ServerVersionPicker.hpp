#pragma once
// 服务端表单的异步版本选择接线；页面只接收列表，不负责联网。
#include "NewServerPage.hpp"
#include "net/ControlGet.hpp"
#include "manage/VersionManager.hpp"
#include "download/ServerInstall.hpp"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QPointer>
#include <QTimer>

class ServerVersionPicker final : public QObject {
public:
    ServerVersionPicker(NewServerPage *page, VersionManager *versions) : QObject(page), m_page(page), m_versions(versions) {
        m_network = new QNetworkAccessManager(this);
        connect(page, &NewServerPage::fieldsChanged, this, [this] { schedule(); });
        connect(page, &NewServerPage::versionsRetryRequested, this, [this] {
            m_cache.remove(m_page->flavorToken()); m_key.clear(); schedule();
        });
        connect(versions, &VersionManager::loaderVersionsFetched, this,
                [this](const QString &game, VersionManager::LoaderKind kind, bool ok,
                       const LoaderMeta::LoaderVersionList &list, const QString &) {
            if (game != m_page->gameVersion() || kind != loaderKind(m_page->flavorToken()) ||
                !needsLoader(m_page->flavorToken())) return;
            m_page->setVersionChoices(false, ok ? ordered(list) : QStringList(),
                                     ok ? tr("暂无可用版本") : tr("获取失败，请刷新重试"));
        });
        schedule();
    }
private:
    static VersionManager::LoaderKind loaderKind(const QString &token) {
        if (token == "forge") return VersionManager::LoaderKind::Forge;
        if (token == "neoforge") return VersionManager::LoaderKind::NeoForge;
        return VersionManager::LoaderKind::Fabric;
    }
    static bool needsLoader(const QString &token) { return token == "fabric" || token == "forge" || token == "neoforge"; }
    static QStringList ordered(const LoaderMeta::LoaderVersionList &list) {
        QStringList result;
        for (bool stable : {true, false}) for (const auto &item : list)
            if (item.stable == stable && !result.contains(item.version)) result.append(item.version);
        return result;
    }
    void schedule() {
        if (m_scheduled) return;
        m_scheduled = true;
        QTimer::singleShot(0, this, [this] { m_scheduled = false; refresh(); });
    }
    void refresh() {
        const QString token = m_page->flavorToken(), game = m_page->gameVersion();
        const QString key = token + "/" + game;
        if (key == m_key) return;
        m_key = key;
        delete m_request.data();
        m_request = nullptr;
        if (needsLoader(token)) {
            m_page->setVersionsLoading(false);
            m_versions->requestLoaderVersions(game, loaderKind(token));
        }
        if (token != "fabric" && token != "quilt") return;
        if (m_cache.contains(token)) { m_page->setVersionChoices(true, m_cache.value(token)); return; }
        m_page->setVersionsLoading(true);
        QNetworkRequest request(QUrl(token == "fabric" ? ServerInstall::fabricInstallerListUrl()
                                                        : ServerInstall::quiltInstallerMetadataUrl()));
        request.setTransferTimeout(15000);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        m_request = RequestBudget::ControlGet::start(this, m_network, request, 15000,
            [this, token, key](QNetworkReply *reply, const QString &queueError) {
            if (key != m_key) return;
            m_request = nullptr;
            if (!reply) { m_page->setVersionChoices(true, {}, queueError); return; }
            const QByteArray body = reply->isOpen() ? reply->readAll() : QByteArray();
            const bool ok = reply->error() == QNetworkReply::NoError && body.size() <= 2*1024*1024;
            reply->deleteLater();
            if (key != m_key) return;
            const QStringList list = !ok ? QStringList() : ordered(token == "fabric"
                ? ServerInstall::listFabricInstallerVersions(QJsonDocument::fromJson(body).array())
                : ServerInstall::listQuiltInstallerVersions(body));
            if (!list.isEmpty()) m_cache.insert(token, list);
            m_page->setVersionChoices(true, list, list.isEmpty() ? tr("获取失败或无可用版本，请刷新重试") : QString());
        });
    }
    NewServerPage *m_page;
    VersionManager *m_versions;
    QNetworkAccessManager *m_network;
    QPointer<RequestBudget::ControlGet> m_request;
    QHash<QString, QStringList> m_cache;
    QString m_key;
    bool m_scheduled = false;
};
