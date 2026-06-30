# 自定义 Godot 构建 — 游戏仓配置指南

本文件说明使用**自定义 Godot 引擎构建**的游戏仓库（C# 项目）需要做的一次性配置，
以及 C++/C# 协同开发时的版本同步约定。

## 背景：为什么需要配置

自定义引擎修改了 C++ API，官方 nuget.org 上的 `GodotSharp` / `Godot.NET.Sdk` 包**不匹配**，
必须使用引擎自带、随编辑器一起分发的包。这些包的版本号形如 `4.6.2-ciallo.g<sha>`，
在 nuget.org 上不存在，因此：

- 不会误用官方包（版本号对不上）。
- 编辑器和它自带的 C# API 包是**一对一**绑定的。
- 游戏仓用 `global.json` 锁定依赖哪一对。

编辑器在每次 build 前会**自动**在项目根写一份 `NuGet.Config`。它会把 Godot 相关包
（`Godot.NET.Sdk` / `GodotSharp` / `Godot.SourceGenerators`）锁到当前编辑器自带的 `nupkgs/`
文件夹，同时保留 nuget.org 给普通第三方包使用，所以你不需要手动配置任何 NuGet 源。
发布 CI 会把这几个 `.nupkg` 显式写进 `bin/GodotSharp/Tools/nupkgs/` 并在打包前校验存在。

## 一次性配置（每个游戏仓做一次）

> 新版自定义编辑器创建或迁移 C# 项目时，会自动生成 `global.json` 和 `NuGet.Config`。
> 已存在的游戏仓请按下面模板检查一次。

### 1. 添加 global.json（锁定 Godot.NET.Sdk 版本）

在游戏仓根目录创建 `global.json`：

```json
{
  "msbuild-sdks": {
    "Godot.NET.Sdk": "4.6.2-ciallo.gXXXXXXXXX"
  }
}
```

`gXXXXXXXXX` 换成你当前使用的编辑器构建对应的 git 短 sha
（编辑器「关于」信息、或 Release 名里都能看到）。

随后确保 `.csproj` 第一行不带 SDK 版本号，让 `global.json` 统一决定：

```xml
<!-- 改前 -->
<Project Sdk="Godot.NET.Sdk/4.6.2">
<!-- 改后 -->
<Project Sdk="Godot.NET.Sdk">
```

### 2. 忽略自动生成的 NuGet.Config

`NuGet.Config` 由编辑器按**本机**编辑器路径生成（绝对路径），不应进版本库。
在游戏仓 `.gitignore` 加：

```gitignore
# 由自定义 Godot 编辑器自动生成，指向本机编辑器自带的 NuGet 包，路径与机器相关。
NuGet.Config
```

## 日常工作流

### 纯 C# 开发者（不碰 C++）

1. `git pull` 游戏仓。
2. 看 `global.json` 里要求的版本（`gXXXXXXXXX`）。
3. 从 Release 下载对应的编辑器构建，解压后用它打开项目。
4. 正常 build。全程不需要碰 NuGet。

> 如果编辑器版本和 `global.json` 对不上，`dotnet build` 会在 **restore 阶段**
> 直接报「找不到 `Godot.NET.Sdk` 版本 `4.6.2-ciallo.gXXXXXXXXX`」，提示你去下对应编辑器——
> 而不是编译到一半报「找不到某个方法」让人困惑。

### C++/C# 同时改的开发者

当你给引擎加了新 C++ API 并要在 C# 里调用时：

1. 引擎仓：改 C++ → 本地跑 glue + `build_assemblies.py --push-nupkgs-local <你的本地源>`
   （或直接用 VSCode 的 `Build: Windows Debug Gen Glue` 任务），本地联调。
2. 就绪后在引擎仓打 tag（如 `v4.6.2-ciallo.3`），CI 产出编辑器构建 `g<sha>`。
3. 游戏仓：在**同一个 commit** 里
   - 提交用到新 API 的 C# 代码，
   - 把 `global.json` 的版本改成上面那个 `g<sha>`。

> **关键约定：用到新 API 的 C# 改动，必须和 `global.json` 的版本提升在同一个 commit。**
> 这样「C# 代码」和「它需要的引擎版本」在 git 历史里绑死，任何人拉下来都不会出现
> 代码与 API 版本错位的情况。

## 几个机制各自的职责

| 机制 | 来源 | 作用 |
|---|---|---|
| 编辑器相对 exe 的约定路径 | Godot 引擎（C++） | 让自带的 nupkgs 跟着编辑器走，运行时按 exe 位置定位 |
| `NuGet.Config`（自动生成） | dotnet 内置 | 让 Godot 包只从编辑器自带文件夹还原，普通包仍可走 nuget.org |
| `global.json` | dotnet 内置 | 全仓统一锁定 `Godot.NET.Sdk` 版本 |
| 唯一版本号 `g<sha>` | CI 设 `GODOT_VERSION_STATUS` | 保证找到的包一定匹配当前编辑器，且不命中旧缓存 |
