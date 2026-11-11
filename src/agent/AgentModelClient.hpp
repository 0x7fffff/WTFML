/**
 * @file AgentModelClient.hpp
 * @brief 模型客户端的抽象接口（Stage 1A 只有 OpenAI-compatible 实现）。
 *
 * 信号约定：
 * - `textDelta` / `reasoningDelta` 只带增量文本；
 * - `finished` 带完整回复与用量；
 * - `failed` 的 `Agent::Error` 负责分类、是否可重试、`Retry-After`；
 * - 取消/切换会话后旧 `requestId` 的回调由上层丢弃。
 */

#pragma once

#include "AgentTypes.hpp"

#include <QObject>

class AgentModelClient : public QObject {
    Q_OBJECT

public:
    explicit AgentModelClient(QObject *parent = nullptr) : QObject(parent) {}

    virtual void start(const Agent::ModelRequest &request) = 0;
    virtual void cancel(const QString &requestId) = 0;

    /// “测试连接”：只报告成功/HTTP 状态/耗时，不显示 key 或响应正文。
    virtual void testConnection(const Agent::ModelRequest &request) = 0;

signals:
    void textDelta(const QString &requestId, const QString &text);
    void reasoningDelta(const QString &requestId, const QString &text);
    void finished(const QString &requestId, const Agent::ModelReply &reply);
    void failed(const QString &requestId, const Agent::Error &error);
    void connectionTested(const QString &requestId, bool ok, int httpStatus,
                          const QString &summary);
};
