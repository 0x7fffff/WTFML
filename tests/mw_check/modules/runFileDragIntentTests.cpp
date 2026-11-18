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
    // ---------- 旧版文件关联：本程序不再注册，只清自己写的那几条 ----------
    namespace FA = FileAssociations;
    const QStringList openable = FA::openableExtensions();
    check(openable.size() == 7 && openable.contains(QStringLiteral(".nbt"))
              && openable.contains(QStringLiteral(".litematic"))
              && openable.contains(QStringLiteral(".schematic"))
              && openable.contains(QStringLiteral(".schem"))
              && openable.contains(QStringLiteral(".mrpack"))
              && openable.contains(QStringLiteral(".jar"))
              && openable.contains(QStringLiteral(".zip")),
          "FA1 旧版能注册的七个后缀都在清理名单里",
          openable.join(QLatin1Char(' ')));
    check(FA::isOpenable(QStringLiteral(".jar")) && FA::isOpenable(QStringLiteral(".zip"))
              && !FA::isOpenable(QStringLiteral(".txt")),
          "FA2 .jar/.zip 仍能从命令行/拖放接住，其它后缀不接");
    check(SevenZip::isArchive(QStringLiteral("a.rar")) && SevenZip::isArchive(QStringLiteral("a.7z"))
              && !SevenZip::isArchive(QStringLiteral("a.zip")) && !SevenZip::isArchive(QStringLiteral("a.mrpack")),
          "FA4 只有 .rar/.7z 需要 7-Zip（zip/mrpack 走内建）");

    // 清理的断言在 `FileAssociations::runSelfTest()` 里（造旧版注册的样子、混一条别人的、
    // 跑两遍）。这一发只把测试根指到本进程专用的子键：**不进真实的 HKCU\Software\Classes**。
    const QByteArray previousRoot = qgetenv("WTFML_ASSOC_CLASSES_ROOT");
    const QString testRoot =
        QStringLiteral("HKEY_CURRENT_USER\\Software\\WTFMLSelfTestClasses\\cleanup-%1")
            .arg(QCoreApplication::applicationPid());
    qputenv("WTFML_ASSOC_CLASSES_ROOT", testRoot.toUtf8());
    const QString report = FA::runSelfTest();
    if (previousRoot.isNull()) {
        qunsetenv("WTFML_ASSOC_CLASSES_ROOT");
    } else {
        qputenv("WTFML_ASSOC_CLASSES_ROOT", previousRoot);
    }
    check(report.isEmpty(),
          "★★ FA5 旧版关联清理：只清本程序的、别人的原样留着、重复跑没有副作用", report);
}
