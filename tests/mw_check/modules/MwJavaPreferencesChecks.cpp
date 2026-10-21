#include "Support.hpp"
int MwJavaPreferencesChecks(const bool settingsWired, MainWindow &window, QPushButton * &javaManageButton, QLabel * &javaLabel, QCheckBox * &javaWarn, QComboBox * &memoryMode, QLabel * &memoryHint, QSpinBox * &maxSpin, QComboBox * &behaviourCombo, QComboBox * &sourceCombo, QPlainTextEdit * &jvmEdit, QCheckBox * &customCheck, QCheckBox * &realCheck, QCheckBox * &loaderCheck, QCheckBox * &loaderVersionCheck, MemorySlider * &maxSlider) {
    if (settingsWired) {
        GlobalJavaSettings javaProbe;   // 同一份沙盒 ini 的另一个读者
        LauncherSettings settingsProbe;

        // ---- Java 版本 ----
        // 起这一格是颗按钮。它**只是通往另一页的一扇门**：界面上一个条目
        // 都不摆（四行在那一页上），所以这里能验的只有"门在不在、写着什么"。
        check(javaManageButton->text() == QStringLiteral("Java 管理…"),
              "设置页那一格是「Java 管理…」那颗按钮", javaManageButton->text());
        check(javaManageButton->isEnabled(),
              "它开窗时就是可点的（挡在后面的只有「已经有一页开着」那一条闸，不是默认禁用）");
        check(javaLabel->text().contains(QStringLiteral("自动")),
              "说明文字说的是「自动」这一档", javaLabel->text());
        check(javaProbe.warnVersionMismatch(), "盘上记的是「提醒」（默认值）");
        check(javaWarn->isChecked(), "开窗时「实例指定的 Java 版本不符时提醒」是勾上的");
        {
            // 反过来：盘上关掉之后，开窗必须是**不勾**（证明上面那条不是硬编码）
            GlobalJavaSettings off;
            off.setWarnVersionMismatch(false);
            MainWindow wOff;
            wOff.show();
            QApplication::processEvents();
            QCheckBox *warnOff =
                byObjectName<QCheckBox>(&wOff, MainWindowUI::javaWarnMismatchCheckName());
            check(warnOff != nullptr && !warnOff->isChecked(),
                  "盘上关掉之后，新窗口里那个勾选框是没勾的（界面显示的就是盘上存的）");
            off.setWarnVersionMismatch(true);
        }

        // ---- 内存：一根条 + 一个数字框 + 自动/手动下拉框 ----
        // 开窗默认是盘上那个档位（新配置 = 手动档）：条和数字框能改，显示的就是盘上
        // 那个数。下面既断言"和盘上一致"，也断言几条与机器无关的不变量 —— 具体给多少
        // MB 随盘上变，这里刻意不写死。自动档那一路在 M 节单独量。
        check(memoryMode->count() == 2 &&
                  memoryMode->itemText(0) == QStringLiteral("自动分配") &&
                  memoryMode->itemText(1) == QStringLiteral("手动分配"),
              "「内存分配」最右边那颗下拉框只有自动分配 / 手动分配两项",
              QString("%1 项").arg(memoryMode->count()));
        check(!memoryMode->currentData().toBool(),
              "没存过时默认是手动分配（老用户的行为不变）");
        check(maxSpin->isEnabled() && maxSlider->isEnabled(),
              "手动档下条和数字框开窗就是能改的");
        {
            // NOTE: 条里的 value() 是**第几格**（见 MemorySlider.hpp），不是 MB —— 要拿它
            // 和数字框比，得先按同一张档位表换算回来。那张表是产品那个纯函数给的
            // （`valueSteps`），测试里不另写一份：另写一份只能证明"我抄对了"。
            const QList<int> steps = MemorySlider::valueSteps(256, maxSpin->maximum());
            const auto sliderMb = [&steps](QSlider *slider) {
                return steps.value(slider->value(), -1);
            };
            const SystemMemory::Usage usage = SystemMemory::read();
            check(usage.totalMb > 0, "这一条要能读到本机物理内存（读不到就只好退回内置上限）",
                  QString::number(usage.totalMb));
            if (usage.totalMb > 0) {
                check(qint64(maxSpin->maximum()) <= usage.totalMb,
                      "「最大内存」的上限收在本机物理内存上（不含超出）",
                      QString("上限 %1 MB / 物理 %2 MB")
                          .arg(maxSpin->maximum())
                          .arg(usage.totalMb));
                check(maxSpin->maximum() % 256 == 0,
                      "上限往下取到 256 那一档（条上的档位就是这个步长，不然拖到头和打得进去的数不一样）",
                      QString::number(maxSpin->maximum()));
                check(steps.constLast() == maxSpin->maximum() &&
                          steps.constFirst() == maxSpin->minimum(),
                      "条的两头就是数字框的上下限（两件东西说的是同一个数）",
                      QString("条 %1..%2 / 框 %3..%4")
                          .arg(steps.constFirst()).arg(steps.constLast())
                          .arg(maxSpin->minimum()).arg(maxSpin->maximum()));
                check(sliderMb(maxSlider) == maxSpin->value(),
                      "开窗灌值之后条和数字框停在同一个数上",
                      QString("条 %1 / 框 %2").arg(sliderMb(maxSlider)).arg(maxSpin->value()));
            }
        }
        check(!javaProbe.autoMemory(), "盘上记的是手动档（默认档）");
        // 手动档下开窗**不重算**这个数 ⇒ 界面和盘上必须是逐位相同的同一个数。
        check(maxSpin->value() == javaProbe.xmx(),
              "手动档下最大内存框显示的就是盘上存的那一个数（开窗不重算）",
              QString("界面 %1，盘上 %2").arg(maxSpin->value()).arg(javaProbe.xmx()));
        check(maxSpin->value() >= maxSpin->minimum() && maxSpin->value() <= maxSpin->maximum() &&
                  maxSpin->value() % 256 == 0,
              "最大内存在框自己的范围内，且是 256 的整数倍（条的档位就是这个步长）",
              QString::number(maxSpin->value()));
        // 「最小内存」那一格从界面上撤掉了，但**它照样发给 JVM** ⇒ 两条
        // 不变量得换个地方钉：① 盘上那对必须是自洽的（Xms 是 Xmx 四分之一那一档，
        // 而且绝不超过 Xmx —— JVM 会拒绝相反的组合）；② 它现在多少**在界面上看得到**
        // （「分配方式」那一行那句说明的最后一句），否则"启动器到底给了多少 -Xms"
        // 在整个界面上一个字都找不到。
        check(javaProbe.xms() == GlobalJavaSettings::suggestXmsMb(javaProbe.xmx()),
              "盘上那对是自洽的：最小内存就是最大内存四分之一那一档",
              QString("%1/%2").arg(javaProbe.xms()).arg(javaProbe.xmx()));
        check(javaProbe.xms() <= javaProbe.xmx(),
              "最小内存不会超过最大内存（JVM 会拒绝这种组合）",
              QString("%1/%2").arg(javaProbe.xms()).arg(javaProbe.xmx()));
        check(memoryHint->text().contains(QStringLiteral("系统总内存")) &&
                  memoryHint->text().contains(QStringLiteral("可用")) &&
                  memoryHint->text().contains(QStringLiteral("拖着改")),
              "手动档说明文字讲清了这台机器有多少内存、并说明这个数怎么改",
              memoryHint->text());
        check(memoryHint->text().contains(QStringLiteral("最小内存由启动器定")) &&
                  memoryHint->text().contains(QStringLiteral("不用管")),
              "说明文字把撤掉的那一项说全了（它还是发给 JVM 的）", memoryHint->text());

        // ---- 启动后 ----
        check(behaviourCombo->count() == 4 &&
                  behaviourCombo->currentData().toInt() == int(settingsProbe.launcherBehaviour()),
              "「启动后」四条档位，选中的那条就是盘上的档位",
              QString("count=%1 data=%2")
                  .arg(behaviourCombo->count())
                  .arg(behaviourCombo->currentData().toInt()));
        // 默认必须是「不变」：用户刚打开启动器就被它自己收起来，是最招人烦的一种默认值。
        check(behaviourCombo->currentData().toInt() == int(LauncherBehaviour::Unchanged),
              "没存过时是「不变」",
              QString::number(behaviourCombo->currentData().toInt()));

        // ---- 下载源 ----
        check(sourceCombo->count() == 2 &&
                  sourceCombo->currentData().toInt() == int(settingsProbe.downloadSource()),
              "「下载源」两条，选中的那条就是盘上的源",
              QString("count=%1 data=%2")
                  .arg(sourceCombo->count())
                  .arg(sourceCombo->currentData().toInt()));
        // 默认必须是官方源：镜像源是第三方服务，不能替用户默认选它。
        check(sourceCombo->currentData().toInt() == int(DownloadSource::Official),
              "没存过时是官方源", QString::number(sourceCombo->currentData().toInt()));

        // ---- JVM 参数 ----
        check(jvmEdit->toPlainText() == javaProbe.extraJvmArgs(),
              "JVM 参数框的正文就是盘上那串", jvmEdit->toPlainText());

        // ---- 版本名格式 ----
        {
            const VersionNameOptions options = settingsProbe.versionNameOptions();
            check(customCheck->isChecked() == options.showCustomName &&
                      realCheck->isChecked() == options.showRealVersion &&
                      loaderCheck->isChecked() == options.showLoaderName &&
                      loaderVersionCheck->isChecked() == options.showLoaderVersion,
                  "四个「版本名格式」勾选框和盘上的开关一一对上",
                  QString("自定义名=%1 版本名=%2 加载器名=%3 加载器版本=%4")
                      .arg(customCheck->isChecked() ? 1 : 0)
                      .arg(realCheck->isChecked() ? 1 : 0)
                      .arg(loaderCheck->isChecked() ? 1 : 0)
                      .arg(loaderVersionCheck->isChecked() ? 1 : 0));
            check(customCheck->isChecked() && realCheck->isChecked() &&
                      loaderCheck->isChecked() && !loaderVersionCheck->isChecked(),
                  "默认档是 自定义名✓ 版本名✓ 加载器名✓ 加载器版本✗");
        }

        // ---- 「设置」页在选项卡里的下标 ----
        // MainWindow::onMainTabChanged() 拿这个常量当"第一次切到设置页才扫 Java"的判据。
        // 它和真实下标对不上时，那条懒加载要么永不触发（设置页里的 Java 列表永远是空的），
        // 要么每次切页都触发一遍 —— 两种都不会报错，只会让人觉得"这启动器有点卡"。
        QTabWidget *tabs = window.findChild<QTabWidget *>(MainWindowUI::tabWidgetName());
        QWidget *settingsTab = window.findChild<QWidget *>(MainWindowUI::settingsTabName());
        check(tabs != nullptr && settingsTab != nullptr &&
                  tabs->indexOf(settingsTab) == MainWindowUI::settingsTabIndex(),
              "settingsTabIndex() 说的就是「设置」页真实的下标",
              tabs == nullptr ? QStringLiteral("<没有选项卡>")
                              : QString::number(tabs->indexOf(settingsTab)));

        // ---- 切到设置页那一下的懒加载 ----
        // 切过去会跑一次 scanLocalJavaVersions()（要逐份认主版本，所以带节流），结果进
        // MainWindow 里那份 m_localJavaVersions。★ 它**不往界面上摆任何东西**了（那个
        // 组合框没有了），所以这一节能验的只剩"切过去这一下不出事、说明文字照旧"。真正
        // 读那份扫描结果的是「Java 管理」那一页（T26 那一节整套验过）。
        if (tabs != nullptr) {
            tabs->setCurrentIndex(MainWindowUI::settingsTabIndex());
            QApplication::processEvents();
            check(javaLabel->text().contains(QStringLiteral("自动")),
                  "切过去扫完之后，说明文字还是「自动」那一档（扫盘不该顺手改设置）",
                  javaLabel->text());
            // 切回主页：底栏右下角是按"当前在不在主页"算的，别把窗口丢在别的页上，
            // 后面的节（以及将来加在后面的节）才好有个确定的起点。
            tabs->setCurrentIndex(MainWindowUI::homeTabIndex());
            QApplication::processEvents();
        }
    }
    return 0;
}


