#include "Support.hpp"

// ---------------------------------------------------------------------------
// R2. NBT 读入的两个上限（R2）
//
// `NbtTree::parse()` 收的是**外部文件**（原理图、`servers.dat`、`level.dat`），里面的长度
// 和项数全是对方写的。`kMaxSize` 只管正文字节数，管不住"一个字节变成一个 Node"这条放大：
// List 的元素最少只占一个字节（空 Compound 就是收尾那一个 `TAG_End`），而一个 `Node`
// 是几十上百字节 —— 64 MiB 的坏文件按项数直接 `reserve` 就是十几 GB。
//
// 这一节钉两件事：
//   ① 元素总数过了头**必须拒绝**，而不是先把内存吃光；
//   ② 项数比"文件剩下的字节数 / 元素最少字节数"还大**必须拒绝**（List(Int) 那种：
//      项数 100、剩下的字节只够 25 项）。
// 顺带钉一条"没过头照样读得动"，免得把上限做成"什么都读不进来"。
// ---------------------------------------------------------------------------

namespace {

/// 一份 NBT：无名复合根 → 一格 `x`（List<type>，`count` 项，元素载荷是 `payload`）。
QByteArray nbtRootWithList(int elementType, qint32 count, const QByteArray &elements) {
    QByteArray listPayload;
    listPayload.append(static_cast<char>(elementType));
    listPayload.append(nbtInt(count));
    listPayload.append(elements);
    return nbtTag(0x0A, QByteArray(),
                  nbtCompoundBody({nbtTag(0x09, QByteArrayLiteral("x"), listPayload)}));
}

/// 一整格 List 的载荷：`count` 个**空复合标签**（每个正好一个字节 `TAG_End`）。
QByteArray emptyCompoundElements(qint32 count) { return QByteArray(count, '\0'); }

} // namespace

void runNbtLimitsTests() {
    section("R2. NBT 读入的上限：项数不能把单字节放大成一次十几 GB 的分配（R2）");

    // ---- R2-1 元素总数上限 ----------------------------------------------------
    //
    // 2,000,000 个空复合标签：正文字节数只有 2 MB（远在 64 MiB 之内），可 **Node 数**是
    // 200 万 —— 旧代码会一路读到底、吃掉 100+ MB 内存。新代码在 1,048,576 个那里就停。
    {
        const QByteArray raw = nbtRootWithList(0x0A, 2000000, emptyCompoundElements(2000000));
        NbtTree::Document document;
        QString problem;
        const bool parsed = NbtTree::parse(raw, &document, &problem);
        check(!parsed, "★ R2-1 200 万个元素的列表被拒绝（不是先把内存吃光）",
              QStringLiteral("%1 字节，%2").arg(raw.size()).arg(problem));
        check(!problem.isEmpty(), "R2-1 拒绝的时候说得出原因", problem);
    }

    // ---- R2-2 项数 × 元素最少字节 > 剩余字节 -----------------------------------
    //
    // List<Int> 写着 100 项、后面只剩 100 个字节：够 25 项、不够 100 项。旧代码拿"项数"
    // 当"字节数"去比（`need(count)`），这一下正好放过 —— 然后按 100 项 reserve、读到一半
    // 才断。新代码在 reserve 之前就按元素宽度算清楚。
    {
        const QByteArray raw = nbtRootWithList(0x03, 100, QByteArray(100, '\0'));
        NbtTree::Document document;
        QString problem;
        const bool parsed = NbtTree::parse(raw, &document, &problem);
        check(!parsed, "★ R2-2 List<Int> 项数 100、正文只够 25 项 -> 拒绝",
              QStringLiteral("%1").arg(problem));
    }

    // ---- R2-3 正常规模照样读得动 ----------------------------------------------
    {
        QByteArray elements;
        for (qint32 k = 0; k < 4096; ++k) {
            elements.append(nbtInt(k));
        }
        const QByteArray raw = nbtRootWithList(0x03, 4096, elements);
        NbtTree::Document document;
        QString problem;
        const bool parsed = NbtTree::parse(raw, &document, &problem);
        const NbtTree::Node *list = parsed ? NbtTree::field(&document.root, QStringLiteral("x")) : nullptr;
        check(parsed && list != nullptr && list->items.size() == 4096 &&
                  list->items.last().number == 4095,
              "R2-3 四千项的 List<Int> 照常读进来（上限没做成「什么都读不进来」）",
              problem);
    }
}
