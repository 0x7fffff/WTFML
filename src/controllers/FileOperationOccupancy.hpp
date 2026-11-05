/**
 * @file FileOperationOccupancy.hpp
 * @brief 「后台正在搬这个实例的文件吗」—— 引用计数那一份的**唯一出处**。
 *
 * 【为什么是计数，不是布尔】
 *
 * 压同一个实例的可能不止一趟：模组迁移、复制实例、「迁移」、「升级副本」、还原都走这一份
 * 名单。先结束的那一趟不能把另一趟还压着的"忙"摘掉 —— 摘掉的后果是那个实例在后台还在
 * 写盘时就能点启动 / 删除了。所以记的是"此刻还压着几趟"，归零才真的摘。
 *
 * 【为什么单独一层】
 *
 * 这对状态以前是主窗口上的两个成员 + 两条方法，判据（计数归零才摘、没记过就放开要当没
 * 发生、空 id 不算）没有断言盯得上：验证工程驱动不了主窗口那一半。挪到这里以后它是纯值
 * 类型，`tests/core_modules/FileOperationOccupancyTests.cpp` 的 FO 段直接钉。
 *
 * 进 `installingVersionIds()` 的那一份仍然由主窗口拼（还有"正在还原"和 `busyVersionId()`
 * 两个来源）；这一层只管它自己那一段。
 */
#pragma once

#include <QHash>
#include <QSet>
#include <QString>

class FileOperationOccupancy final {
public:
    /// 压一层（空 id 不算：调用方还没定下是谁）。
    void mark(const QString &instanceId) {
        if (instanceId.isEmpty()) {
            return;
        }
        ++m_counts[instanceId];
    }

    /**
     * @brief 放一层。
     *
     * 没记过就忽略：那说明调用方配错了一对（多放了一次），把计数变成负数会让下一次
     * `mark()` 从 -1 加到 0、于是"忙"永远摘不掉。宁可不记，也不要留下一个再也修不好的数。
     */
    void release(const QString &instanceId) {
        if (instanceId.isEmpty()) {
            return;
        }
        auto it = m_counts.find(instanceId);
        if (it == m_counts.end()) {
            return;
        }
        if (--it.value() > 0) {
            return;
        }
        m_counts.erase(it);
    }

    bool contains(const QString &instanceId) const { return m_counts.contains(instanceId); }

    /// 现在压着的实例（喂给 `installingVersionIds()` 那一段）。
    QSet<QString> instanceIds() const {
        QSet<QString> ids;
        for (auto it = m_counts.constBegin(); it != m_counts.constEnd(); ++it) {
            ids.insert(it.key());
        }
        return ids;
    }

    bool isEmpty() const { return m_counts.isEmpty(); }

    /// 这个实例此刻压着几趟（只给断言与日志用；判"忙不忙"用 `contains()`）。
    int countFor(const QString &instanceId) const { return m_counts.value(instanceId, 0); }

private:
    QHash<QString, int> m_counts;
};
