# wonderful tremendous fantastic minecraft launcher

这是一个基于 C++17 和 Qt6 的 Minecraft 启动器原型项目。

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

## 构建方法

确保已安装 Qt6 和支持 CMake 的编译器。

```sh
mkdir build
cd build
cmake -DCMAKE_PREFIX_PATH="<Qt6安装路径>" ..
cmake --build .
```

## 当前功能

- 启动器主窗口和日志区已实现。
- 微软登录入口已添加（默认打开登录网页）。
- Java 参数生成、进程输出和崩溃检测框架已搭建。
- 文件夹管理与版本列表显示已实现基础逻辑。
- Java 配置加载/保存和完整性校验骨架已完成。
- 1.18.2及以上版本的隔离与离线启动(不含1.18.2)
- 自动读取version.json文件分析需要的java版本，自动下载java(1.18.2+)

## to do list(急)

- 把项目升级到CPP23
- 把XML编写的UI换成人类能看懂的CPP
- 给已有文件添加注释，规范详见CONTRIBUTING_COMMENT.md
- 解决下载完成的提示出现多个的问题
- 1.18.2及以下版本的启动
- 完善目前没什么用的FolderManager
- 补充 Microsoft OAuth2 登录回调与令牌刷新
- 登录令牌的加密储存和调取
- 正版启动与皮肤等信息的拉取
- 实现真实的 Minecraft 启动参数与版本解析。
- 增加完整性校验、下载修复和 crash 报告导出。

## to do list(future)
- GUI的优化
- 更详细注释的添加
- 文件下载/解压进度的显示
- 启动器的卡顿优化
