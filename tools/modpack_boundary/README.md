# 2.1.7 真实受限文件 / 指定卷验收

手动构建目标 `wtfml_modpack_boundary`（verification 配置）。不加入 CTest，不在 CI 自动联网或写外部磁盘。
先把 Qt/MinGW 运行库加入 PATH。测试只能使用全新、独立目录；不接受已有目录作为新现场。

```powershell
cmake --build build/verification-mingw13 --target wtfml_modpack_boundary --parallel 6
build/verification-mingw13/tests/wtfml_modpack_boundary.exe --root H:/WTFML-Acceptance-20261025/新现场
```

固定样本为 CurseForge 项目1121856 / 文件5813216。保存实网项目/文件元数据，要求
`allowModDistribution=false` 且 downloadUrl=null，通过生产 VersionManager 解析和 DownloadManager
下载并校验大小/SHA-1；官方无key请求必须明确401/403。

随后用真实文件的已校验字节在本机缓慢回放，在同一指定卷验证取消、强制结束下载进程、
新进程重新获取完整文件。回放是为了可重复命中中断，不是公网速度或物理盘吞吐基准。
report.json 保留逐项结果，日志不包含API key；此阶段不运行模组、不等同完整整合包安装。

## 真实导入事务

在上一步创建且带 owned.json 的现场内：

```powershell
build/verification-mingw13/tests/wtfml_modpack_boundary.exe --root H:/.../现场 --import --cancel 300
build/verification-mingw13/tests/wtfml_modpack_boundary.exe --root H:/.../现场 --import --crash
build/verification-mingw13/tests/wtfml_modpack_boundary.exe --root H:/.../现场 --import
```

生成只含规范manifest的最小CF包（1.20.1 / Fabric 0.16.14 / 上述真实文件），所有核心、加载器、
库、资源和模组均由生产导入器获取，不伪造核心或完成凭据。第二步在活跃下载阶段直接退出86，
跳过析构；第三步重新解析同一包、重新进入实际导入状态机。

取消发生在目标目录创建前时，允许目录尚不存在；若目录已建立则必须保留未完成标记，不能有成功meta。
最终必须终态成功、报告complete、meta与清单绑定阶段凭据存在、importing消失，且真实受限JAR大小/SHA-1匹配。
使用独立的 `RestrictedBoundaryVerified` 实例；工具将清单SHA-256传入生产导入器，不能省略指纹后宣称阶段凭据通过。
540秒预算到期请求取消；系统I/O不返回仍是操作系统边界，外层运行应另设900秒上限。
不自动删除现场；不读取其它启动器密钥，也不把测试包/模组二进制发布到仓库。
