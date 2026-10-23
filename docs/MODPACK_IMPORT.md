# 整合包导入：支持范围与安全边界

入口为管理/下载页的“导入整合包”，或拖放 ZIP / MRPACK；导入创建或恢复自己的未完成实例，不覆盖现有正常实例。

## 当前支持

- Modrinth：modrinth.index.json、下载文件和 overrides/client-overrides，客户端覆盖层后应用。
- CurseForge：manifest.json、项目/文件 ID 解析与指定覆盖目录；作者禁止第三方分发及失效文件单独说明。
- 简单 MultiMC / Prism：mmc-pack.json、instance.cfg 与游戏目录；明确 Minecraft 和受支持加载器版本后重新安装核心。
  当前组件白名单为 net.minecraft、org.lwjgl 和 Forge/Fabric/NeoForge/Quilt；Quilt 仍由现有安装能力检查拒绝。
  有完整本地声明式 patches 的包可转换额外组件，见下节；缺少补丁时仍不猜测专用启动配置。
- PCL 等包装 ZIP：内含一份 MRPACK 时解出后按同一套流程导入，不运行随包附带的启动器或脚本。
- 简单懒人包：直接游戏目录、单层包装目录，或启动器旁的 .minecraft；必须有可确认的版本及加载器版本。

归档扩展名不证明包类型。单个模组、只有 JAR 的模组合集、世界存档和服务端包不作为客户端整合包。
多个候选实例不按条目顺序猜选；缺少版本信息也不装“最新版”凑数。RAR / 7z 暂不支持，可自行解压后规范打 ZIP。

## 声明式启动补丁（GTNH 等）

支持清单组件对应的本地 patches：按 order 排序，检查身份、版本、依赖/冲突及 Java 版本交集，
转换主类、官方 mainJar、资源索引、库、+tweakers 与受限 +jvmArgs。未知字段/操作符明确拒绝。
补丁客户端须与刚获取的官方原版 SHA-1 一致，不安装重新打包的 Minecraft 客户端。

GTNH 2.8.4 Java 17-25 包的 RFB 主类、专用 ClassLoader、forgePatches 本地库和模块参数已接通；
LWJGL3 原生库翻译为标准 Maven classifier 及明确 CPU 架构，防止 x86 DLL 混入 x64。
包内库校验后原子写入 libraries；同名内容不同则拒绝覆盖。断点身份包含补丁与本地库内容摘要。
不启动来源启动器，不执行 instance.cfg 的前后置命令、脚本、javaagent 或任意 JVM 参数。
原版客户端与运行库沿用生产下载器；自定义运行配置单独保存，不继承会重新带入 LWJGL2 的原版库数组。

这不是所有 MultiMC 插件的完整兼容；缺失本地组件补丁、未实现操作符及不支持的 JVM 参数仍拒绝。
FTB、Technic 等专用清单没有独立适配器；仅在实际包含上述标准格式时走对应入口。
多实例、依赖启动脚本或复杂隔离布局的非标准懒人包暂不猜测转换。

游戏目录导入跳过 versions、libraries、assets、日志及已知启动器账号文件；核心及加载器走现有安装器。
不继承来源启动器的账号、任意 Java 路径和执行命令。压缩包路径检查、覆盖文件排除及目录锁仍生效。

## 验证

常规回归：tools/test.ps1 -Suite ci-core -Modules core-resources；主窗口导入接线在
tools/test.ps1 -Suite ci-integration -Modules window 的 MwModpackImportChecks 段验证。
可选设置 WTFML_TEST_PACK_ARCHIVES 为分号分隔的本地 ZIP 列表，附加只读样本检查；CI 不依赖本机下载目录。
WTFML_TEST_GTNH_ARCHIVE 可指定真实 GTNH ZIP，验证补丁转换、主类/Java 声明、架构筛选和测试目录本地库发布。
样本识别/清单解析通过不等于真实联网安装和游戏启动通过。原始归档与现有游戏目录不用于测试写入。

GTNH 2.8.4 已在独立工作区走通生产 VersionManager 的真实安装流程，解开 11972 个覆盖文件；
安装与文件完整性结果不代表已通过完整游戏启动、进入世界或所有 Java 版本验收。

## 启动兼容性

隔离实例的进程工作目录与 `--gameDir` 一致，包括 Java 8 的 ASCII 别名；共享库和资源仍用绝对路径。
旧共享根目录下由模组生成的 config 等文件不会被删除，也不会自动覆盖实例内的配置；必要时比对后迁移。
现代 Forge/NeoForge 的 BootstrapLauncher `ignoreList` 会补上继承链中实际客户端 JAR 文件名，
避免实例名与原版 JAR 名不同导致重复 Minecraft 模块；不更改实例身份，也不从 classpath 删除客户端资源。
分段探测返回 404 时会尝试一次完整 GET；仍检查 HTTP 状态、大小与 SHA1，真实缺失不会被接受。

## 分发边界

本适配器自行解析格式，不嵌入第三方启动器或复制其实现。用户提供包，启动器只在本机转换并安装，
发布包/仓库不包含 GTNH、Minecraft 客户端及模组二进制。此设计不构成对来源包许可的保证。
不要重新打包分发 Mojang 文件或修改版客户端；第三方模组/库分别遵守其许可和分发渠道限制。
若未来随启动器捆绑、修改或再分发 LGPL 等组件，应另行审核署名、许可、源码及其它适用义务。

格式依据：[Modrinth 规范](https://support.modrinth.com/en/articles/8802351-modrinth-modpack-format-mrpack)、
[GTNH LWJGL3ify 启动说明](https://github.com/GTNewHorizons/lwjgl3ify#client)。
