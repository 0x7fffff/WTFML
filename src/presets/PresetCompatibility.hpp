/**
 * @file PresetCompatibility.hpp
 * @brief 模组预设的**兼容检查**（coding plan §6.0 P2、§7.2）。
 *
 * 【这一层管什么】
 *
 * 一份模组预设里那串 `ModReference`（平台 + 稳定项目 ID），在"这次要装到
 * 游戏版本 X / 加载器 Y"的上下文里能不能拿到 —— 结论只有五种：
 *
 * · `Matched` 平台明确说有兼容发行版；
 * · `Missing` 平台明确说**没有**（或者这一条自己钉死的版本/加载器和这次对不上）；
 * · `Unknown` 网络失败 / 429 / 超时 —— ★ **不计入缺失**（§7.2 那条验收）；
 * · `Invalid` 引用本身坏了（平台认不出来 / 没有项目 id）—— 本地就能判，不联网；
 * · `Pending` 还没查 / 正在查。
 *
 * 【为什么先本地、再缓存、最后网络】
 *
 * §7.2 的顺序是硬的：弹窗先拿本地元数据立刻能看；能用"这一项钉死的版本/加载器"
 * 判掉的不要再问网络；缓存里**还没过期**的结论直接用；剩下的才排队查询。
 * 这样断网时弹窗照样打开，所有条目的结论是 `Unknown`，不会卡住、也不会谎报缺失。
 *
 * 【去重、并发、超时、旧回调】
 *
 * · 去重按 `ModReference::key()`（平台 + 项目 id）。同一项目出现在几类/几份预设里，
 * 这一次只问一遍；结果按**首次出现的次序**摆回去。
 * · 在途请求数封顶（`Options::maxInFlight`），多出来的排队。
 * · 每一项一条超时：超时就是 `Unknown`，**不**自动重试（重试交给下一次打开 / 用户再点）。
 * · `start()` / `cancel()` 都会让 generation +1；旧 generation 的迟到回调一律丢掉
 * （§7.2「generation/context token 拒绝旧回调」）。
 *
 * 【这一层不做什么】
 *
 * 不下载、不写盘、不认识控件、不拼安装计划。P5 的依赖/冲突/发布文件筛选在它们自己那层；
 * 这一层只回答"有没有"，所以**不能**把 `Matched` 当成"装得上"或"不会冲突"（§5 末段）。
 */

#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>

#include "PresetTypes.hpp"

namespace Presets {

// ---------------------------------------------------------------------------
// 一次检查的上下文
// ---------------------------------------------------------------------------

/// 查询上下文：游戏版本 + 加载器（+ 运行侧）。它进缓存键，所以**每一项都要稳定**。
struct CompatibilityContext {
    QString gameVersion;  ///< 例：`1.21.1`。★ 空 = 不知道，查询会变成 `Unknown`。
    QString loader;       ///< `fabric` / `forge` / `neoforge` / `quilt`；空 = 原版。
    bool clientSide = true; ///< 运行侧（服务端专用模组不该按客户端那套判）。

    /// 缓存 / 去重用的键。**唯一出处**：别处不要自己拼这几格。
    QString cacheKey() const;
};

/// 加载器名归一：大小写、`vanilla` / `原版` / `none` 都折成空串（= 原版）。
QString normalizeCompatibilityLoader(const QString &raw);

// ---------------------------------------------------------------------------
// 结论
// ---------------------------------------------------------------------------

enum class CompatibilityState {
    Pending,
    Checking,
    Matched,
    Missing,
    Unknown,
    Invalid,
};

QString compatibilityStateKey(CompatibilityState state);

/// 一条引用的结论（界面按 key 找回来，所以 key 一定要带上）。
struct CompatibilityResult {
    QString key;
    QString platform;
    QString projectId;
    /// 显示名（快照里记的；空的话界面退回 `projectId`）。
    QString name;
    bool required = true;
    CompatibilityState state = CompatibilityState::Pending;
    /// 人话原因（`Matched` 时为空）。
    QString detail;
    /// 这一条是不是缓存里的结论（界面上可以标"缓存"；不参与任何判断）。
    bool fromCache = false;
};

/// 一批引用的汇总（安装弹窗那行状态用）。
struct CompatibilitySummary {
    int total = 0;
    int matched = 0;
    int missing = 0;
    int unknown = 0;
    int invalid = 0;
    int pending = 0;        ///< `Pending` + `Checking`
    int requiredMissing = 0;

    int finished() const { return matched + missing + unknown + invalid; }
    bool isFinished() const { return pending == 0; }
    /// 状态行那句话（**唯一出处**：安装弹窗与验证工程读同一句）。
    QString text() const;
};

/**
 * @brief 本地就能判的那一档（§7.2「先做已知约束筛选」）。
 *
 * 判据只有三条：平台认不出来 / 没有项目 id / 这一条自己钉死的版本或加载器和上下文对不上。
 * 其余一律 `Pending`（要问平台或查缓存）。
 */
CompatibilityState localCompatibilityCheck(const ModReference &reference,
                                           const CompatibilityContext &context,
                                           QString *detail = nullptr);

// ---------------------------------------------------------------------------
// 平台查询（可注入：正式界面用真网络，验证工程用假提供方）
// ---------------------------------------------------------------------------

/**
 * @brief 查**一个平台项目**有没有兼容这次上下文的发行版。
 *
 * 实现只许回 `Matched` / `Missing` / `Unknown` 三种。调用方（`CompatibilityChecker`）
 * 已经做过去重和本地筛选，不会拿坏引用或钉死冲突的项来问。
 * `reply` **最多调用一次**；同步调用也允许（检查器会转成队列调用，不会重入）。
 */
class CompatibilityProvider {
public:
    using Reply = std::function<void(CompatibilityState, const QString &)>;
    virtual ~CompatibilityProvider() = default;
    virtual void query(const ModReference &reference, const CompatibilityContext &context,
                       Reply reply) = 0;
};

// ---------------------------------------------------------------------------
// 缓存
// ---------------------------------------------------------------------------

/**
 * @brief 一次进程内的结论缓存（§7.2「共享缓存」）。
 *
 * 键 = 上下文 + 项目（`keyFor()` 是唯一出处）。★ 只缓存 `Matched` / `Missing`：
 * `Unknown` 是"这次没问出来"，缓存它等于把一次断网变成十分钟的假结论。
 * 过期条目**不删**（`lookup(..., &stale)` 能看见"上次的结论以及多旧"），下次查询
 * 覆盖它 —— 原因是"过期标待刷新"要显示出来，删掉就没得说了。
 */
class CompatibilityCache {
public:
    struct Entry {
        CompatibilityState state = CompatibilityState::Pending;
        QString detail;
        qint64 stampMs = 0;
    };

    explicit CompatibilityCache(qint64 ttlMs = 10 * 60 * 1000);

    static QString keyFor(const ModReference &reference,
                          const CompatibilityContext &context);

    void setTtlMs(qint64 ttlMs) { m_ttlMs = ttlMs; }
    qint64 ttlMs() const { return m_ttlMs; }

    /// @param stale 非空时写"这一条过期了没有"（条目存在才有意义）。
    /// @return 条目在不在（过期的也在 —— 看 @p stale）。
    bool lookup(const QString &key, Entry *entry, bool *stale = nullptr) const;
    void insert(const QString &key, CompatibilityState state, const QString &detail,
                qint64 nowMs = 0);
    void clear() { m_entries.clear(); }
    int size() const { return static_cast<int>(m_entries.size()); }

private:
    QHash<QString, Entry> m_entries;
    qint64 m_ttlMs;
};

// ---------------------------------------------------------------------------
// 检查器
// ---------------------------------------------------------------------------

/**
 * @brief 把一串引用跑完（本地 → 缓存 → 网络），把结论按引用次序摆回来。
 *
 * 生命周期：`start()` 可反复调（每次都是新一代）；对象活着时关掉弹窗没关系 ——
 * 调用方用 `QObject::connect(..., &box, ...)` 那种带上下文的连接，控件没了连接自动断。
 */
class CompatibilityChecker : public QObject {
    Q_OBJECT

public:
    struct Options {
        /// 在途请求上限。4 是"弹窗里四类"那个量级；验证工程可以调小来钉并发。
        int maxInFlight = 4;
        /// 单项超时（毫秒）。到点按 `Unknown` 报，不重试。
        int timeoutMs = 12000;
        /// 缓存有效期（毫秒）。0 = 不读也不写缓存。
        qint64 cacheTtlMs = 10 * 60 * 1000;
    };

    /// @param cache 共享缓存（**不拥有**；空 = 不用缓存）。
    explicit CompatibilityChecker(CompatibilityCache *cache = nullptr,
                                  QObject *parent = nullptr);

    /// 平台查询实现（**不拥有**；空 = 每一项直接 `Unknown`）。
    void setProvider(CompatibilityProvider *provider);
    void setOptions(const Options &options);
    Options options() const { return m_options; }

    /// 开始新的一代（旧的一代立刻作废）。
    void start(const QList<ModReference> &references, const CompatibilityContext &context);
    /// 作废当前一代（不改变已经出来的结论；迟到的回调一律丢）。
    void cancel();

    int generation() const { return m_generation; }
    bool running() const { return m_running; }
    CompatibilitySummary summary() const;
    QList<CompatibilityResult> results() const;

signals:
    /// 每出一条 / 开始 / 缓存命中都会发一次（调用方按 summary 更新那一行）。
    void updated(const QList<Presets::CompatibilityResult> &results,
                 const Presets::CompatibilitySummary &summary);
    /// 这一代**全部**有结论了（每次 `start()` 至多一次）。
    void finished(const QList<Presets::CompatibilityResult> &results,
                  const Presets::CompatibilitySummary &summary);

private:
    struct Item {
        ModReference reference;
        QString key;
        CompatibilityState state = CompatibilityState::Pending;
        QString detail;
        bool required = true;
        bool fromCache = false;
        bool queued = false;
    };

    void resolveLocalAndQueue(const QList<ModReference> &references,
                              const CompatibilityContext &context);
    void pump();
    void beginQuery(int index);
    void applyResult(int index, CompatibilityState state, const QString &detail,
                     bool fromCache);
    void emitUpdate();
    void finishIfDone();
    CompatibilitySummary computeSummary() const;
    void dropLiveQueries();

    CompatibilityProvider *m_provider = nullptr;
    CompatibilityCache *m_cache = nullptr;
    Options m_options;
    CompatibilityContext m_context;
    QList<Item> m_items;
    QHash<QString, int> m_indexByKey;
    QQueue<int> m_queue;
    QSet<int> m_liveQueries;
    int m_nextQueryId = 1;
    int m_inFlight = 0;
    int m_generation = 0;
    bool m_running = false;
    bool m_finishedEmitted = false;
};

}  // namespace Presets
