/**
 * @file AgentSkill.cpp
 * @brief 内置技能清单（编译进程序，不从远程下载）。
 */

#include "AgentSkill.hpp"

QList<AgentSkill> AgentSkillRegistry::builtins() {
    AgentSkill general;
    general.id = QStringLiteral("general");
    general.name = QStringLiteral("自由对话");
    general.description = QStringLiteral("没有实例工具权限的普通对话；Stage 1A 只发送消息与系统提示。");
    general.systemPrompt = QStringLiteral(
        "你是 WTFML 启动器内置的实例维护助手。当前是自由对话模式，没有任何文件或实例工具权限。"
        "不要假装读取过文件、日志或实例；不知道就说不确定。"
        "任何凭据、令牌、账户信息都不要索要或复述。");
    general.allowedTools = {};
    general.readOnly = true;
    return {general};
}

bool AgentSkillRegistry::contains(const QString &id) {
    for (const AgentSkill &skill : builtins()) {
        if (skill.id == id) return true;
    }
    return false;
}

AgentSkill AgentSkillRegistry::find(const QString &id) {
    for (const AgentSkill &skill : builtins()) {
        if (skill.id == id) return skill;
    }
    return {};
}
