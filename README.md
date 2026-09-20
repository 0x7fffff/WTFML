# Wonderful Tremendous Fantastic Minecraft Launcher

这是一个基于 C++23 和 Qt6 的 Minecraft 启动器原型项目。

## 目标

- 采用模块化架构实现启动、登录、管理、设置、下载、GUI 各部分。
- 支持 Windows 平台。
- 使用 Qt Widgets 构建主界面和日志显示。

## 项目结构

- `src/`：源代码目录
- `src/launcher/`：启动流程与 JVM 参数管理
- `src/auth/`：登录、令牌保存与读取
- `src/manage/`：文件夹和版本管理
- `src/settings/`：全局 Java 设置
- `src/download/`：完整性校验模块
- `ui/`：Qt Designer UI 文件
- `CMakeLists.txt`：构建配置

## 分支 to do

- 添加、管理、删除文件夹列表
- 调整 Java 相对路径，并增添手动添加路径功能
- 修复下载 Java 解压后文件不存在问题
- 修改 ui 使已下载版本和全部版本列表分离

## 当前功能

- 启动器主窗口和日志区已实现。
- 微软登录入口已添加（默认打开登录网页）。
- Java 参数生成、进程输出和崩溃检测框架已搭建。
- 文件夹管理与版本列表显示已实现基础逻辑。
- Java 配置加载/保存和完整性校验骨架已完成。
- 各版本的隔离与离线启动（natives 链路已打通，1.18.2 及以下同样可用）
- 自动读取 version.json 的 javaVersion，优先复用本机已有运行时，找不到才下载
- 旧格式 natives 在启动时按需解压，并自动补上 -Djava.library.path
- 首次启动某版本时写入 options.txt，界面语言跟随系统

## to do list(急)

- 解决下载完成的提示出现多个的问题
- 1.18.2及以下版本的启动（链路已打通并离线实测，待从 UI 实际测试）
- 完善目前没什么用的FolderManager
- 补充 Microsoft OAuth2 登录回调与令牌刷新
- 登录令牌的加密储存和调取
- 资源索引被 Mojang 修订后，UI 没有刷新单个版本资源的入口（见到 version.json 就拒绝重下）
- 只刷新资产索引、资源本身都齐全时 allDownloadsFinished 不会发射（onAssetIndexDownloaded 不检查完成）
- 正版启动与皮肤等信息的拉取
- 分离UI中已下载版本和版本总列表
- 增加完整性校验、下载修复和 crash 报告导出。

## to do list(future)
- GUI的优化
- 更详细注释的添加
- 文件下载/解压进度的显示
- 启动器的卡顿优化
