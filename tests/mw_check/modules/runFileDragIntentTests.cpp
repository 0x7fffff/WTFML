#include "Support.hpp"

#include "InstanceFilesPage.hpp"
#include "../../../src/util/FileAssociations.hpp"
#include "../../../src/util/SevenZip.hpp"

void runFileDragIntentTests() {
    using Intent = InstanceFilesPage::DragIntent;
    check(InstanceFilesPage::dragIntent(2, 6, true, false) == Intent::None,
          "FD1 不到阈值：不拖出、不框选（点还是点）");
    check(InstanceFilesPage::dragIntent(20, 6, false, false) == Intent::None,
          "FD2 空白处直接拖动交给 Qt 的框选（这一层不接）");
    check(InstanceFilesPage::dragIntent(20, 6, true, false) == Intent::DragOut,
          "FD3 项上直接拖动 = 拖出文件");
    check(InstanceFilesPage::dragIntent(20, 6, true, true) == Intent::ToggleSelection
              && InstanceFilesPage::dragIntent(20, 6, false, true) == Intent::ToggleSelection,
          "FD4 Ctrl+拖动 = 框选取反（项上、空白处都算）");
    check(InstanceFilesPage::dragIntent(6, 6, true, false) == Intent::DragOut,
          "FD5 刚好到阈值就走（不是「必须超过」）");
    // ---------- 文件关联的策略判据（不写注册表，只看"允不允许"） ----------
    namespace FA = FileAssociations;
    const QStringList mcOnly = FA::mcOnlyExtensions();
    check(mcOnly.contains(QStringLiteral(".nbt")) && mcOnly.contains(QStringLiteral(".litematic"))
              && mcOnly.contains(QStringLiteral(".schematic"))
              && mcOnly.contains(QStringLiteral(".schem"))
              && mcOnly.contains(QStringLiteral(".mrpack")) && !mcOnly.contains(QStringLiteral(".zip")),
          "FA1 只有 MC 独有的三个后缀允许设默认");
    check(FA::isOpenable(QStringLiteral(".jar")) && FA::isOpenable(QStringLiteral(".zip"))
              && !FA::isOpenable(QStringLiteral(".txt")),
          "FA2 .jar/.zip 能出现在打开方式里，其它后缀不接");
    check(SevenZip::isArchive(QStringLiteral("a.rar")) && SevenZip::isArchive(QStringLiteral("a.7z"))
              && !SevenZip::isArchive(QStringLiteral("a.zip")) && !SevenZip::isArchive(QStringLiteral("a.mrpack")),
          "FA4 只有 .rar/.7z 需要 7-Zip（zip/mrpack 走内建）");
    check(!FA::registerDefault(QStringLiteral(".zip"), nullptr)
              && !FA::registerDefault(QStringLiteral(".jar"), nullptr),
          "FA3 .zip/.jar 被硬拦，不会抢默认打开方式");
}






