/**
 * @file AgentSkillsTests.cpp
 * @brief Stage 1A 只有“自由对话”技能，白名单为空；没有工作流引擎。
 */

#include "Support.hpp"

#include "agent/AgentSkill.hpp"

void runAgentSkillsTests() {
    const QList<AgentSkill> skills = AgentSkillRegistry::builtins();
    check(skills.size() == 1, "技能：Stage 1B 只有一个内置技能", QString::number(skills.size()));
    const AgentSkill general = AgentSkillRegistry::find(QStringLiteral("general"));
    check(general.id == QStringLiteral("general"), "技能：general 可查");
    check(general.name == QStringLiteral("自由对话"), "技能：名字是自由对话");
    check(general.allowedTools.isEmpty(), "技能：Stage 1A 工具白名单为空");
    check(general.systemPrompt.contains(QStringLiteral("工具")) &&
              general.systemPrompt.contains(QStringLiteral("用户确认")),
          "技能：系统提示与实际下发的文件工具一致（可读、写入须确认）");
    check(AgentSkillRegistry::contains(QStringLiteral("general")), "技能：注册表 contains");
    check(!AgentSkillRegistry::contains(QStringLiteral("diagnose")),
          "技能：Stage 2 工作流尚未注册");
    check(AgentSkillRegistry::find(QStringLiteral("missing")).id.isEmpty(),
          "技能：未知 id 返回空结构");
}
