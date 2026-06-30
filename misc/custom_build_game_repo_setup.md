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

这类开发者会在引擎仓本地编译引擎、产出**尚未发布**的 C# 包，在游戏仓里联调新 API。
本地产出的包版本号同样是 `g<sha>`，但**它对应的是你本机的引擎构建，不是 Release**，
因此必须走一个**本地 NuGet 源**，与纯 C# 开发者消费 Release 的路径区分开。

#### 一次性：建立本地 NuGet 源

本地引擎构建会把 C# 包打进它自己的 `bin/GodotSharp/Tools/nupkgs/` 目录，这个目录
**就是**你的本地 NuGet 源——编辑器自动生成的 `NuGet.Config` 正是指向它（自带 nupkgs 目录），
所以本地源和自动配置天然一致，不需要另建目录、也不要手改 `NuGet.Config`。

> 不要把本地源指到 `bin/GodotSharp/Tools/nupkgs/` 之外的随意目录。编辑器每次 build 会
> **重写** `NuGet.Config` 并 `clear` 掉所有源，只保留「自带 nupkgs 目录 + nuget.org」，
> 你手加的源会被覆盖掉，restore 随之失败。

#### 联调循环

1. 引擎仓：改 C++ → 跑 glue + 把包推进本地源（即本地构建的自带 nupkgs 目录）：

   ```bash
   python modules/mono/build_scripts/build_assemblies.py \
     --godot-output-dir ./bin \
     --godot-platform=<windows|linuxbsd|macos> \
     --push-nupkgs-local ./bin/GodotSharp/Tools/nupkgs
   ```

   （或直接用 VSCode 的 `Build: Windows Debug Gen Glue` 任务。）
   `--push-nupkgs-local` 会顺带清掉全局缓存里的同名 `g<sha>` 包，避免命中旧内容。
2. 游戏仓：用这个本地构建的编辑器打开，`global.json` 填本地的 `g<sha>`，联调。
3. 就绪后在引擎仓打 tag（如 `v4.6.2-ciallo.3`），CI 产出**正式** Release 编辑器构建 `g<sha>`。
4. 游戏仓：在**同一个 commit** 里
   - 提交用到新 API 的 C# 代码，
   - 把 `global.json` 的版本改成 CI 那个正式 `g<sha>`。

> **关键约定：用到新 API 的 C# 改动，必须和 `global.json` 的版本提升在同一个 commit。**
> 这样「C# 代码」和「它需要的引擎版本」在 git 历史里绑死，任何人拉下来都不会出现
> 代码与 API 版本错位的情况。

#### 制度约束：本地产出的包**禁止**分发

本地源里的 `g<sha>` 包来自你的工作机，可能含有**未提交的改动**——版本号读的是 `HEAD` 的
sha，并不反映工作区是否 dirty。一旦这种包流出去，就会出现「同一个 `g<sha>` 版本号对应
两份不同内容」的情况，而 NuGet 全局缓存按版本号去重、先到先得，导致别人编出的结果
与你不一致、且无法从版本号察觉。因此：

- **不要**把本地源目录、本地产出的 `.nupkg`、或本地 `nupkgs/` 拷贝给任何人。
- **不要**把本地源指向团队共享目录 / 网盘 / 制品库。
- 需要别人也能用的新 API，**只能**通过引擎仓打 tag → CI 发布的正式 `g<sha>` 流转。
- 自己跨机器同步时也按「打 tag 走 CI」处理，不要手搬本地包。

> 一句话：本地源是**只读给自己联调用的临时产物**，对外分发的唯一合法来源是 CI Release。

## 几个机制各自的职责

| 机制 | 来源 | 作用 |
|---|---|---|
| 编辑器相对 exe 的约定路径 | Godot 引擎（C++） | 让自带的 nupkgs 跟着编辑器走，运行时按 exe 位置定位 |
| `NuGet.Config`（自动生成） | dotnet 内置 | 让 Godot 包只从编辑器自带文件夹还原，普通包仍可走 nuget.org |
| `global.json` | dotnet 内置 | 全仓统一锁定 `Godot.NET.Sdk` 版本 |
| 唯一版本号 `g<sha>` | CI 设 `GODOT_VERSION_STATUS` | 保证找到的包一定匹配当前编辑器，且不命中旧缓存 |
