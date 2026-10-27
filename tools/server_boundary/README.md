# 真实服务端验收

手动目标 `wtfml_server_boundary`，不进入CTest或CI。不复用已有目录，不访问真实账号或存档。
三种样本固定为Minecraft 1.20.1：原版、Fabric 0.16.14/installer 1.0.1、Forge 47.2.0。
使用真实Java17；错误Java使用本机真实Java8。三种类型分别使用全新绝对目录。

```powershell
cmake --build build/verification-mingw13 --target wtfml_server_boundary --parallel 6
build/verification-mingw13/tests/mw_check/wtfml_server_boundary.exe --root D:/isolated-new-root --flavor fabric --java C:/path/to/java17.exe --wrong-java C:/path/to/java8.exe --accept-eula
```

`--accept-eula`只用于用户明确授权后的隔离验收，不改变产品中必须明确接受的门禁。
先实际验首次拒绝（必须在真实日志中发现EULA说明），然后仅监听127.0.0.1生成测试世界，
验证就绪、发送stop保存退出、再次启动后强杀、端口占用和错误Java均不得就绪。
端口占用即便官方进程返回0，生产状态也必须是Crashed。

安装阶段300秒，每次运行90秒，整趟预算最多750秒，失败保留原目录和日志，不自动清理重跑。
report.json逐项保留就绪/退出码/强杀事实；installation.log保留脱敏安装输出。
`--run-only`仅接受root/flavor绑定的owned.json现场，复用已安装的真实产物，先另存前次报告；
会在测试目录重设EULA门禁再运行上述矩阵，不能指向用户已有服务端。

`--wizard-install`用于全新原版现场：核对默认配置解析确实位于root/settings下，再用明确的INI路径
写入测试文件夹列表，通过真实MainWindow的创建按钮、向导和安装任务完成下载，保留截图与wizard-install.json。
配置未隔离时拒绝执行；不修改用户启动器设置。

`--ui-only`在owned现场使用生产向导和控制台导出按钮、真实服务端日志，验证关闭向导后参数仍可读、
导出文本脱敏并保存截图。这里使用Qt文件选择框，不冒充原生操作系统对话框验收。
以上均未验公网玩家加入，也不宣称所有游戏/加载器版本均兼容。
