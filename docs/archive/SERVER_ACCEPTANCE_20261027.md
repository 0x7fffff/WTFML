# 2.2.6 真实服务端验收（2026-10-27）

用户确认原版、Fabric、Forge；隔离安装运行与测试EULA接受沿用明确授权。
工具和安全边界见tools/server_boundary/README.md；不使用真实账号、已有服务端或存档，仅监听127.0.0.1。

三种真实1.20.1服务端均安装成功：Fabric0.16.14/installer1.0.1、Forge47.2.0、原版；Microsoft Java17。
现场为build/server-boundary-{vanilla,fabric,forge}-20261027-1000。
首次EULA拒绝、就绪后stop保存退出、再次启动强杀、端口占用及真实Java8不匹配均已验证。
最终report.json均ok=true；run-only重用的是本轮安装成功产物，保留初次报告，不冒充再次冷安装。
端口占用时原版/Fabric可能退出0，但生产状态仍须Crashed；强杀与启动失败不混同。

补测发现Fabric在UnsupportedClassVersionError后可能留下错误界面/线程而不退出：
首份build/server-matrix-fabric-20261027-1007/report.json因120秒超时判失败，保留原件。
产品现只在启动中识别明确的该致命Java错误，结束自己启动的失败进程，按异常启动报告；
不记成用户强杀、不匹配普通ERROR文字。实际Java8复测及子进程回归通过。

安装向导实际主窗口链：build/server-wizard-20261027-1028/wizard-install.json中
accepted/installed/screenshot均true；确实从MainWindow创建入口完成原版安装。
配置路径明确核对位于该现场settings下。自动审批首次拒绝可能触及真实默认设置的补丁，
未执行；改用显式INI路径、默认解析核对和独立组织名后才运行，不修改用户启动器配置。

三种现场ui-report.json均ok=true，保留wizard.png、console.png以及实际UI导出的日志。
生产控制台导出按钮通过Qt文件选择框执行（不是原生Windows对话框验收），内容来自真实正常停止日志，
附加测试secret验证导出脱敏。首次工具选文件名的时序错误报告保留在原版ui-report-first-path-oracle.json。
Fabric复核还发现向导关闭后getter使用isVisible丢失加载器参数；产品改为按栏位自身isHidden判断，
页面隐藏后仍保留适用参数、切换类型后不读取不适用参数。旧失败保留在Fabric的ui-report-before-wizard-fix.json。

属性编辑使用读取时的SHA-256基线：外部程序改动后拒绝保存并要求重新加载，不覆盖外部内容。
已有主窗口真实文件/界面回归覆盖这一保护。日志导出额外脱敏并核对完整写入，不保证外部服务端自行写的日志已被启动器改写。

仍单列的问题：并发重装的Forge现场build/server-matrix-forge-20261027-1007在内部
DOWNLOAD_MOJMAPS静默到300秒工具截止，未安装成功。与首次真实安装成功并列保留；
不能靠后续运行矩阵通过宣称该网络根因消失。服务端已补有界静默重试，尚需针对该具体现场复验。
不将本轮结果推广到所有服务端/游戏版本、冷缓存性能、原生文件对话框或公网玩家加入。

## 10 月 27～28 日收尾

原故障现场已核对固定 build 路径、旧失败报告和测试服务端元数据，补记 owned.json 的来源及旧报告 SHA-256 后重试。
`build/server-matrix-forge-20261027-1007/report.json` 现为 installed/ok=true，使用真实 Microsoft Java 17；
原失败另存为 report-before-run-only-*.json，installation.log 追加，未删除半成品。
`build/2.1-forge-original-retry-20261027-run2.*.log` 保留真实重试输出。

高速度下载模块另用实际 OS 子进程和缩短时钟验证：静默一次后重启成功、两次静默后唯一失败终态、
持续有输出仍受总截止约束、保留安装器、明确重新安装成功。生产默认仍为 3 分钟静默 / 10 分钟总截止。
故障位于外部 Java 内部下载，而不是 Qt 文件队列；瞬时网络首因没有被这次成功重试证明消失。
本轮收口的是有界失败、保留与可重试行为，不是声明已根治上游网络。

服务端恢复真窗口：`build/server-recovery-ui-20261027-final/wizard-install.json` 全部通过，
包括单一任务、暂停、销毁/重开主窗口、文件补齐后等待、第二次明确继续和三阶段截图；不启动服务端。
跨 OS 进程的文件恢复另由模块覆盖，两者不混同。详见 [下载恢复](DOWNLOAD_RECOVERY_ACCEPTANCE_20261027.md)。
