/**
 * @file IntegrityChecker.hpp
 * @brief 为 Minecraft 所处文件夹的完整性验证提供声明
 * @author LoserKugua
 * @date 2026-04-10
 * @note 检查版本目录是否存在，防止启动前缺少文件。
 */
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class IntegrityChecker : public QObject {
    Q_OBJECT

public:
    explicit IntegrityChecker(QObject *parent = nullptr);
    bool verifyIntegrity(const QString &folderPath, QStringList *errors = nullptr) const;
};
