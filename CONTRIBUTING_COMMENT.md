1. 文件头注释 (File Header)
要求： 每个 .hpp 和 .cpp 文件开头必须包含此块，说明文件的核心职责。

```cpp
/**
 * @file FileName.hpp (或 .cpp)
 * @brief 简述：例如“负责游戏启动参数的拼接与进程管理”
 * @author YourName / Agent
 * @date 2026-04
 * @note 备注：例如“针对 1.19 以下版本需注意 Native 库的手动解压逻辑”
 */
```

2. 核心逻辑注释 (Logic & Hacks)
要求：详细逻辑使用简体中文。特别是涉及到 Minecraft 特定版本的“坑点”或绕过的 Bug，必须说明原因。

原则： 不用逐行注释，但特殊情况处理要解释“为什么要这么写”。

示例：

```cpp
// 如果版本低于 1.19，旧版 LWJGL 无法识别 JAR 内的 native，
// 必须在此处将 DLL 解压到隔离目录的 natives 文件夹中。
if (gameVersion < "1.19") {
    extractNatives(targetPath);
    jvmArgs.append("-Djava.library.path=" + targetPath);
}
```
3. 类与接口注释 (API Documentation)
要求：使用英文，brief简要介绍，param参数说明，return返回值说明

示例：

```cpp
/**
 * @brief Starts the Minecraft game process.
 * @param config The launch configuration containing JVM and game arguments.
 * @return True if process started successfully, false otherwise.
 */
bool launchGame(const LaunchConfig& config);
```
4. 临时标记 (Workflow Tags)
要求： 使用标准前缀，方便全局搜索。

// TODO: 待办事项（如：// TODO: 实现 Java 25 的自动下载）。

// FIXME: 已知 Bug 需修复（如：// FIXME: 修复部分版本路径包含空格崩溃的问题）。

// NOTE: 重要提醒（如：// NOTE: 此处必须使用 UTF-8 编码读取 JSON）。

5. 格式化配合 (Integration)
所有注释应遵守根目录下 .clang-format 的 ReflowComments: true 规则。

建议使用自动规范化

编码格式： 强制使用 UTF-8。严禁在注释中使用任何会导致编译器报错的特殊字符。