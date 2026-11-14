#include "Support.hpp"
#include "UiLanguage.hpp"

#include <QLocale>
#include <QSettings>
#include <QTranslator>

// 界面语言那一条链路：解析（纯函数）→ 装翻译 → 导航轨真的变英文。
// 产品的接线是 src/main.cpp 里那三行；这里验的是同一套函数和同一份资源。
int UiLanguageChecks(MainWindowUI &ui, QTabWidget *tabs) {
    std::printf("===== 界面语言 =====\n");
    using UiLanguage::resolve;

    // ---- 1. 解析：system 看系统 UI 语言，显式档不跟随系统 ----
    check(resolve(QStringLiteral("system"), {QStringLiteral("zh-CN")}) == QStringLiteral("zh_CN"),
          "system + 简中系统 → zh_CN");
    check(resolve(QStringLiteral("system"), {QStringLiteral("zh-TW")}) == QStringLiteral("zh_CN"),
          "system + 繁体系统 → zh_CN");
    check(resolve(QStringLiteral("system"), {QStringLiteral("en-US")}) == QStringLiteral("en"),
          "system + 英文系统 → en");
    check(resolve(QStringLiteral("system"), {}) == QStringLiteral("en"),
          "system + 系统没报语言 → en");
    check(resolve(QStringLiteral("en"), {QStringLiteral("zh-CN")}) == QStringLiteral("en"),
          "显式 en 不跟随中文系统");
    check(resolve(QStringLiteral("zh_CN"), {QStringLiteral("en-US")}) == QStringLiteral("zh_CN"),
          "显式 zh_CN 不跟随英文系统");
    check(resolve(QStringLiteral("乱写"), {QStringLiteral("en-US")}) == QStringLiteral("en"),
          "认不出来的档位按 system 算");

    // ---- 2. 中文这一档：导航轨就是源文（没装翻译）----
    check(tabs != nullptr, "选项卡在，能读导航文字");
    const auto navTexts = [](QTabWidget *tabWidget) {
        QStringList out;
        if (tabWidget == nullptr) return out;
        for (int i = 0; i < 5; ++i) out << tabWidget->tabText(i);
        return out;
    };
    check(navTexts(tabs) == QStringList({QStringLiteral("主页"), QStringLiteral("管理"),
                                        QStringLiteral("下载"), QStringLiteral("设置"),
                                        QStringLiteral("智能体")}),
          "zh_CN（默认、测试钉住的那一档）导航轨是中文", navTexts(tabs).join(QLatin1Char('/')));

    // ---- 3. 「语言」那一行：三个选项，itemData 就是落盘的记号 ----
    QComboBox *language = ui.getLanguageCombo();
    check(language != nullptr && language->objectName() == MainWindowUI::languageComboName(),
          "设置页有「语言」下拉框（按 objectName 取得回）");
    check(language != nullptr && language->count() == 3 &&
              language->itemData(0).toString() == QStringLiteral("system") &&
              language->itemData(1).toString() == QStringLiteral("zh_CN") &&
              language->itemData(2).toString() == QStringLiteral("en"),
          "三档顺序是 跟随系统 / zh_CN / en");
    check(language != nullptr && language->itemText(0) == QStringLiteral("跟随系统"),
          "中文界面下「跟随系统」写中文", language != nullptr ? language->itemText(0) : QString());
    check(language != nullptr && language->itemText(1) == QStringLiteral("简体中文") &&
              language->itemText(2) == QStringLiteral("English"),
          "另外两档各写各自语言的名字（不翻）");

    // ---- 4. 设成 en：读设置 → 解析 → 装翻译 → 新建的界面是英文 ----
    QSettings().setValue(QStringLiteral("General/Language"), QStringLiteral("en"));
    const QString resolved = resolve(UiLanguage::readSetting(), QLocale::system().uiLanguages());
    check(resolved == QStringLiteral("en"), "盘上设成 en 时解析结果是 en（不看系统语言）");

    QTranslator translator;
    check(UiLanguage::loadEnglish(&translator),
          "英文翻译在 exe 的资源里（:/i18n/wtfml_en.qm）");
    check(!translator.isEmpty(), "那一份 .qm 不是空的");
    qApp->installTranslator(&translator);
    {
        QMainWindow englishWindow;
        MainWindowUI englishUi(&englishWindow);
        QTabWidget *englishTabs = englishUi.getTabWidget();
        check(navTexts(englishTabs) == QStringList({QStringLiteral("Home"), QStringLiteral("Library"),
                                                    QStringLiteral("Download"), QStringLiteral("Settings"),
                                                    QStringLiteral("Agent")}),
              "装了英文翻译之后导航轨是 Home / Library / Download / Settings / Agent",
              navTexts(englishTabs).join(QLatin1Char('/')));
        check(englishUi.getLanguageCombo() != nullptr &&
                  englishUi.getLanguageCombo()->itemText(0) == QStringLiteral("Follow system"),
              "英文界面下「跟随系统」跟着翻成 Follow system");
    }
    qApp->removeTranslator(&translator);

    // 收尾：把沙盒里那一格改回测试钉的 zh_CN，别把后面的模块带到英文。
    QSettings().setValue(QStringLiteral("General/Language"), QStringLiteral("zh_CN"));
    QSettings().sync();
    return 0;
}
