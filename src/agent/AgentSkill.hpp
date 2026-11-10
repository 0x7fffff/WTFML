/**
 * @file AgentSkill.hpp
 * @brief Stage 1A/1B 的技能**结构与注册表骨架**：只有“自由对话”一档。
 *
 * NOTE: 这里**没有**工作流状态机 / 步骤执行 / 计划校验。Agent.md D3/D4 明确把
 * workflow engine 放到 Stage 2；现在先留类型和注册表，避免以后大改调用方。
 */

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

struct AgentSkill {
    QString id;
    QString name;
    QString description;
    QString systemPrompt;
    /// Stage 1A 一律为空（不调用任何工具）。
    QStringList allowedTools;
    bool readOnly = true;
};

class AgentSkillRegistry {
public:
    /// 编译进程序的技能；现在只有 `general`（自由对话）。
    static QList<AgentSkill> builtins();
    static bool contains(const QString &id);
    static AgentSkill find(const QString &id);
};
