#include "Support.hpp"
int MwJavaSelectionChecks(const QDir &root) {
    {
        // 造一个"盘上钉着一个手动 Java"的局面再看新窗口认不认。用**钉死的路径**而不是
        // "本机扫到的某个 Java"：这台机器上扫不扫得到 Java 不该决定这条断言跑不跑 ——
        // 那样别的机器上会静默跳过，等于没测。路径不需要真的存在，界面只是照实显示它。
        const QString pinnedJava =
            root.filePath(QStringLiteral(".minecraft/java/pinned/bin/java.exe"));
        {
            GlobalJavaSettings seed;
            seed.setJavaPath(pinnedJava);
            seed.setAutoSelectJava(false);
            seed.save();
        }
        {
            LauncherSettings seed;
            seed.setDownloadConcurrency(6);
            seed.setDownloadSpeedLimit(1024 * 1024);
            seed.setProxyMode(DownloadProxy::Mode::Manual);
            seed.setProxyManualText(QStringLiteral("127.0.0.1:7890"));
        }
        {
            MainWindow w5;
            w5.show();
            QApplication::processEvents();
            QLabel *label5 = w5.findChild<QLabel *>(MainWindowUI::javaPathLabelName());
            QComboBox *source5 = comboByObjectName(&w5, MainWindowUI::downloadSourceComboName());
            QComboBox *conc5 =
                comboByObjectName(&w5, MainWindowUI::downloadConcurrencyComboName());
            check(label5 != nullptr && source5 != nullptr, "第五个窗口的设置页控件找得到");
            if (label5 != nullptr && source5 != nullptr) {
                check(label5->text().contains(QStringLiteral("手动指定")) &&
                          label5->text().contains(pinnedJava),
                      "说明文字照实说是「手动指定」+ 那条路径", label5->text());
                check(source5->currentData().toInt() == int(DownloadSource::Mirror),
                      "上一节存下的镜像源，冷启动就选上了（读盘那条路也走了 syncDownloadSource）",
                      QString::number(source5->currentData().toInt()));

                // ---- 并发数 ----
                // 这一节量的是**冷启动那条路**：盘上写着 6 ⇒ 下拉框里四档都在、选中的是 6、
                // 而且**真正发请求的那一层也拿到 6 了**（少了最后那半句，界面显示 6、下载
                // 还是按 24 条一起飞 —— 不报错、也看不出哪儿没接上）。
                check(conc5 != nullptr, "第五个窗口里那颗「并发数」下拉框找得到");
                if (conc5 != nullptr) {
                    check(conc5->count() == DownloadConcurrency::presets().size(),
                          "★ 四档都在框里（正好的那个数，不多不少）",
                          QStringLiteral("框里有 %1 项").arg(conc5->count()));
                    check(conc5->currentData().toInt() == 6,
                          "★★ 盘上写着的 6，冷启动就选上了（applySettingsToUi 灌的那一条路）",
                          QString::number(conc5->currentData().toInt()));
                    VersionManager *vm5 = w5.findChild<VersionManager *>();
                    check(vm5 != nullptr &&
                              vm5->downloads()->maxConcurrentDownloads() == 6,
                          "★★ 而且**真发请求的那一层**也拿到 6 了（syncDownloadConcurrency "
                          "在构造里跑过；漏了它 = 界面显示 6、实际还是按默认那一档飞）",
                          vm5 != nullptr
                              ? QString::number(vm5->downloads()->maxConcurrentDownloads())
                              : QStringLiteral("拿不到 VersionManager"));
                    // 当场改一档：盘上要跟着变、数据层也要跟着变（改完立刻生效那条路）。
                    const int twelve = conc5->findData(12);
                    if (twelve >= 0 && vm5 != nullptr) {
                        conc5->setCurrentIndex(twelve);
                        QApplication::processEvents();
                        QSettings written;
                        check(written.value(QStringLiteral("Download/Concurrency")).toInt() == 12 &&
                                  vm5->downloads()->maxConcurrentDownloads() == 12,
                              "★★ 当场拨到 12：盘上那一位和数据层那一档**一起变**（少了任何一样"
                              "都是「改了没生效」或「下次启动又变回去」）",
                              QStringLiteral("盘上 %1 / 数据层 %2")
                                  .arg(written.value(QStringLiteral("Download/Concurrency")).toInt())
                                  .arg(vm5->downloads()->maxConcurrentDownloads()));
                    } else {
                        check(false, "框里找得到「12」那一档（四档都在）");
                    }
                }

                // ---- 速度上限 ----
                // 和并发数同一个形状：盘上写着 1 MiB/s ⇒ 框里那几档都在、选中的是它、
                // 而且发请求那一层也拿到同一个数（漏了最后那半句的表现是「界面显示限了、
                // 实际满速跑」，不报错、也看不出哪儿没接上）。
                QComboBox *speed5 =
                    comboByObjectName(&w5, MainWindowUI::downloadSpeedLimitComboName());
                check(speed5 != nullptr, "第五个窗口里那颗「速度上限」下拉框找得到");
                if (speed5 != nullptr) {
                    check(speed5->count() == DownloadSpeedLimit::presets().size(),
                          "★ 预设那几档都在框里（正好的那个数，不多不少）",
                          QStringLiteral("框里有 %1 项").arg(speed5->count()));
                    check(speed5->currentData().toLongLong() == 1024 * 1024,
                          "★★ 盘上写着的 1 MB/s，冷启动就选上了（applySettingsToUi 灌的那一条路）",
                          QString::number(speed5->currentData().toLongLong()));
                    VersionManager *vm5s = w5.findChild<VersionManager *>();
                    check(vm5s != nullptr &&
                              vm5s->downloads()->speedLimitBytesPerSecond() == 1024 * 1024,
                          "★★ 而且**真发请求的那一层**也拿到 1 MB/s 了（漏了它 = 界面显示限了、"
                          "实际还是满速）",
                          vm5s != nullptr
                              ? QString::number(vm5s->downloads()->speedLimitBytesPerSecond())
                              : QStringLiteral("拿不到 VersionManager"));
                    // 当场改一档：盘上要跟着变、数据层也要跟着变。
                    const int twoM = speed5->findData(int(2 * 1024 * 1024));
                    if (twoM >= 0 && vm5s != nullptr) {
                        speed5->setCurrentIndex(twoM);
                        QApplication::processEvents();
                        QSettings written;
                        check(written.value(QStringLiteral("Download/SpeedLimitBytes"))
                                      .toLongLong() == 2 * 1024 * 1024 &&
                                  vm5s->downloads()->speedLimitBytesPerSecond() ==
                                      2 * 1024 * 1024,
                              "★★ 当场拨到 2 MB/s：盘上那一位和数据层那一个值**一起变**",
                              QStringLiteral("盘上 %1 / 数据层 %2")
                                  .arg(written.value(QStringLiteral("Download/SpeedLimitBytes"))
                                           .toLongLong())
                                  .arg(vm5s->downloads()->speedLimitBytesPerSecond()));
                    } else {
                        check(false, "框里找得到「2 MB/s」那一档（预设都在）");
                    }
                }

                // ---- 代理 ----
                // 盘上写着「手动 + 127.0.0.1:7890」⇒ 框选中「手动」、地址那一格露着并回填，
                // 而且 **`QNetworkProxy::applicationProxy()` 真的换成了那个 HTTP 代理**
                // （少了最后那半句 = 界面显示手动、请求还是走系统那条路）。
                QComboBox *proxy5 =
                    comboByObjectName(&w5, MainWindowUI::downloadProxyComboName());
                QLineEdit *proxyEdit5 =
                    w5.findChild<QLineEdit *>(MainWindowUI::downloadProxyManualEditName());
                check(proxy5 != nullptr && proxyEdit5 != nullptr,
                      "第五个窗口里「代理」那颗下拉框和手动地址那一格都找得到");
                if (proxy5 != nullptr && proxyEdit5 != nullptr) {
                    check(proxy5->count() == DownloadProxy::modes().size(),
                          "★ 三档都在框里（正好的那个数，不多不少）",
                          QStringLiteral("框里有 %1 项").arg(proxy5->count()));
                    check(proxy5->currentData().toInt() == int(DownloadProxy::Mode::Manual),
                          "★★ 盘上写着的「手动」，冷启动就选上了",
                          QString::number(proxy5->currentData().toInt()));
                    check(proxyEdit5->text() == QStringLiteral("127.0.0.1:7890") &&
                              !proxyEdit5->isHidden(),
                          "★★ 手动地址回填了，而且那一格在「手动」档是露着的",
                          proxyEdit5->text());
                    QNetworkProxy applied = QNetworkProxy::applicationProxy();
                    check(applied.type() == QNetworkProxy::HttpProxy &&
                              applied.hostName() == QStringLiteral("127.0.0.1") &&
                              applied.port() == 7890,
                          "★★ **进程上的代理**真的换成了 127.0.0.1:7890（HTTP）",
                          QStringLiteral("type=%1 host=%2 port=%3")
                              .arg(int(applied.type()))
                              .arg(applied.hostName())
                              .arg(applied.port()));
                    // 当场拨到「直连」：盘上跟着变、进程上的代理跟着变、地址格子收起来。
                    const int directIndex =
                        proxy5->findData(int(DownloadProxy::Mode::Direct));
                    if (directIndex >= 0) {
                        proxy5->setCurrentIndex(directIndex);
                        QApplication::processEvents();
                        QSettings written;
                        applied = QNetworkProxy::applicationProxy();
                        check(written.value(QStringLiteral("Download/ProxyMode")).toString()
                                      == QStringLiteral("direct") &&
                                  applied.type() == QNetworkProxy::NoProxy &&
                                  proxyEdit5->isHidden(),
                              "★★ 当场拨到「直连」：盘上、进程上的代理、地址那一格的显隐"
                              "**一起变**",
                              QStringLiteral("盘上 %1 / 代理类型 %2 / 地址格藏了 %3")
                                  .arg(written.value(QStringLiteral("Download/ProxyMode"))
                                           .toString())
                                  .arg(int(applied.type()))
                                  .arg(proxyEdit5->isHidden() ? 1 : 0));
                    } else {
                        check(false, "框里找得到「直连」那一档（三档都在）");
                    }
                }

                // 切回「自动」：**不**清掉记着的路径 —— 那个字段同时是"上次挑中的 Java"
                // 的缓存，清掉的话每次启动都要重扫一遍本地 Java。走的是 onJavaUseAuto()
                // 里那对 setter 的**同一个对象**（那颗按钮只在「Java 管理」那一页上，
                // 而这一节验的是冷启动读盘，不是那一页 —— 那一页在 T26 里整套验）。
                GlobalJavaSettings *java5 = w5.findChild<GlobalJavaSettings *>();
                check(java5 != nullptr, "拿得到第五个窗口里那份 Java 设置");
                if (java5 != nullptr) {
                    java5->setAutoSelectJava(true);
                    java5->save();
                }
                QApplication::processEvents();
                GlobalJavaSettings after;
                check(after.autoSelectJava() && after.javaPath() == pinnedJava,
                      "切回「自动」后 autoSelect 变真，但记着的路径留着",
                      QString("auto=%1 path=%2")
                          .arg(after.autoSelectJava() ? 1 : 0)
                          .arg(after.javaPath()));
            }
        }
        // 把 Java 档恢复成"自动、没有路径"，免得给后面留下一个手动的设定。
        // （下载源那份留着 Mirror：这节本来就是靠它验的，而且沙盒的 ini 最后会整个删掉。）
        {
            GlobalJavaSettings reset;
            reset.setAutoSelectJava(true);
            reset.setJavaPath(QString());
            reset.save();
        }
        {
            LauncherSettings reset;
            reset.setDownloadConcurrency(DownloadConcurrency::kDefault);
            // 速度上限同理：留个 2 MB/s 的话后面真下东西的几节会慢得莫名其妙。
            reset.setDownloadSpeedLimit(DownloadSpeedLimit::kUnlimited);
            // 代理同理：删掉「手动」那一档并把进程代理还回默认，否则后面真联网的
            // 那几节会全部往 127.0.0.1:7890 发请求（那儿没人接，一律超时）。
            reset.setProxyMode(DownloadProxy::kDefaultMode);
            reset.setProxyManualText(QString());
            QNetworkProxy defaultProxy;
            defaultProxy.setType(QNetworkProxy::DefaultProxy);
            QNetworkProxy::setApplicationProxy(defaultProxy);
        }
    }
    return 0;
}


