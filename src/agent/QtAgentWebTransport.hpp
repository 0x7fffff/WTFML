/**
 * @file QtAgentWebTransport.hpp
 * @brief `AgentWebTransport` 的 Qt 实现：直连 + pinned-IP + 手动重定向（见 .hpp）。
 */

#pragma once

#include "AgentWebTransport.hpp"

#include <QObject>

class QNetworkAccessManager;

class QtAgentWebTransport : public QObject, public AgentWebTransport {
    Q_OBJECT

public:
    explicit QtAgentWebTransport(QObject *parent = nullptr);

    void resolve(const QString &host, ResolveCallback done) override;
    void get(const AgentHttpRequest &request, ResponseCallback done) override;

private:
    QNetworkAccessManager *m_manager = nullptr;
};
