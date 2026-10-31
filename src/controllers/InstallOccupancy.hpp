/**
 * @file InstallOccupancy.hpp
 * @brief 「这一批下载是谁的」—— 界面自己记的那四档实例占用的唯一出处。
 *
 * 四档（导入整合包 / 带名字的新实例 / 补全文件 / 首启资源预取）走的都是**同一条**
 * `versionDownloadFinished(id, ok, msg)` 终态信号（数据层那边"一次点击 = 一个终态"的
 * 约定，见 ImportController::importModpackFromFile 那段）。界面靠"这一发讲的 id 是哪一档
 * 记下的"来决定收尾说什么话、任务中心那一行叫什么 —— 所以这四笔状态在好几处被读。
 *
 * 以前它们是主窗口上四个散着的 QString，读的地方各自再写一遍判据：三段 OR（"是不是正在
 * 改它"）在三个文件里各有一份，优先级链（导入 > 安装 > 补全 > 预取）在任务账那边写了两
 * 遍（标题一遍、目标一遍）。那些判据之间只要有一处漏了某一档，症状就是"装着的实例还能
 * 点启动"或者"这一批认错了自己是哪一类"。所以这里把**问法**收拢：
 *
 * - 谁算"正在改这个实例"（`modifying()`）、哪些实例"现在不能启动"（`instanceIds()`）、
 *   这一批该叫什么（`active()`）、终态该按哪一档说话（`clearFor()`），都只在本文件定义。
 * - 记状态走 `setBatch()`，不让人直接动某一格 —— "先记 id、再置忙"那个顺序（见
 *   InstallPageController 里那段 NOTE）写出来就看得见是哪一档。
 */

#pragma once

#include <QSet>
#include <QString>

class InstallOccupancy final {
public:
    /// 四档，加上"哪一档都不是"。
    enum class Batch { None, Import, Install, Repair, Prefetch };

    /// 现在占着的那一档 + 是哪个实例。
    struct Active {
        Batch batch = Batch::None;
        QString instanceId;
        bool valid() const { return batch != Batch::None; }
    };

    /// 记上 / 换掉某一档（`instanceId` 为空 = 那一档松开了）。
    ///
    /// NOTE: 同一时刻只该有一档非空 —— 三条入口（导入 / 安装 / 补全）各有"已经有别的批次
    /// 在跑"的闸，预取那条也再看一眼另外三档（见 maybePrefetchAssetDirector）。真有两档撞
    /// 在一起时按下面的优先级说话，而不是让它们互相打架。
    void setBatch(Batch batch, const QString &instanceId) {
        switch (batch) {
        case Batch::Import: m_import = instanceId; break;
        case Batch::Install: m_install = instanceId; break;
        case Batch::Repair: m_repair = instanceId; break;
        case Batch::Prefetch: m_prefetch = instanceId; break;
        case Batch::None: break;  // "哪一档都不是"没有要记的东西
        }
    }
    void clearBatch(Batch batch) { setBatch(batch, QString()); }

    QString instanceId(Batch batch) const {
        switch (batch) {
        case Batch::Import: return m_import;
        case Batch::Install: return m_install;
        case Batch::Repair: return m_repair;
        case Batch::Prefetch: return m_prefetch;
        case Batch::None: break;
        }
        return QString();
    }
    bool is(Batch batch, const QString &instanceId) const {
        return !instanceId.isEmpty() && this->instanceId(batch) == instanceId;
    }

    /// 现在说话的那一档。优先级：导入 > 安装 > 补全 > 预取 —— 就是表里这个顺序，
    /// 和收尾那一段 if 链同序（`DownloadCompletionController::onFinished`）。
    Active active() const {
        for (const Batch batch : kOrder) {
            const QString id = instanceId(batch);
            if (!id.isEmpty()) {
                return {batch, id};
            }
        }
        return {};
    }

    /// 这个实例**正在被改**（导入 / 安装 / 补全 —— 重写的就是它自己的目录）。
    ///
    /// **预取不算**：它写的是共享的 assets/asset_director，实例目录一个字都不动；但预取
    /// 期间这个实例照样不能启动（那边问的是 `instanceIds()`，它含预取）。
    bool modifying(const QString &instanceId) const {
        return !instanceId.isEmpty()
               && (instanceId == m_import || instanceId == m_install || instanceId == m_repair);
    }
    bool anyModifying() const {
        return !m_import.isEmpty() || !m_install.isEmpty() || !m_repair.isEmpty();
    }

    /// 四档里所有非空的实例 id（并进 `installingVersionIds()` 那份"现在不能启动"）。
    QSet<QString> instanceIds() const {
        QSet<QString> ids;
        for (const Batch batch : kOrder) {
            const QString id = instanceId(batch);
            if (!id.isEmpty()) {
                ids.insert(id);
            }
        }
        return ids;
    }

    /// 收尾：把**和这个 id 对得上**的档全清掉，返回刚才最高优先级的那一档（一条终态信号
    /// 该按哪一档说话）。对不上的返回 `Batch::None`。
    ///
    /// NOTE: 清的时候不挑档。真有两档撞了同一个 id（不该发生），两档都得松开 —— 只松一档
    /// 的话另一档会永远占着，这个实例从此再也启动不了。
    Batch clearFor(const QString &instanceId) {
        if (instanceId.isEmpty()) {
            return Batch::None;
        }
        Batch highest = Batch::None;
        for (const Batch batch : kOrder) {
            if (this->instanceId(batch) != instanceId) {
                continue;
            }
            setBatch(batch, QString());
            if (highest == Batch::None) {
                highest = batch;
            }
        }
        return highest;
    }

private:
    /// 优先级表的唯一出处（`active()` / `instanceIds()` / `clearFor()` 都按它走）。
    static constexpr Batch kOrder[] = {Batch::Import, Batch::Install, Batch::Repair,
                                       Batch::Prefetch};

    QString m_import;
    QString m_install;
    QString m_repair;
    QString m_prefetch;
};
