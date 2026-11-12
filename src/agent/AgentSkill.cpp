/**
 * @file AgentSkill.cpp
 * @brief 内置技能清单（编译进程序，不从远程下载）。
 */

#include "AgentSkill.hpp"

QList<AgentSkill> AgentSkillRegistry::builtins() {
    AgentSkill general;
    general.id = QStringLiteral("general");
    general.name = QStringLiteral("自由对话");
    general.description = QStringLiteral("对话 + 锁定实例内的受控文件工具（Stage 1B）。");
    // Stage 1B 起 AgentLoop 每轮都带上文件工具（fs.list / fs.read / fs.write / artifact.read），
    // 提示词要和这一点一致，否则模型会拒绝调用手上就有的工具。
    general.systemPrompt = QStringLiteral(
        "你是 WTFML 启动器内置的实例维护助手。你可以用提供的工具查看本会话关联实例里的目录与文本文件；"
        "写入只限配置文件，且每次写入都要用户确认。没有用工具读到的文件、日志或实例信息，不要假装读过；"
        "不知道就说不确定。任何凭据、令牌、账户信息都不要索要或复述。");
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
