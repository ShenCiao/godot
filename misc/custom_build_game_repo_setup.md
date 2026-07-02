# 自定义 Godot 构建 — 游戏仓配置指南

本文件说明使用**自定义 Godot 引擎构建**的游戏仓库（C# 项目）需要做的一次性配置，
以及 C++/C# 协同开发时的版本同步约定。

## 背景：为什么需要配置

自定义引擎修改了 C++ API，官方 nuget.org 上的 `GodotSharp` / `Godot.NET.Sdk` 包**不匹配**，
必须使用本引擎发布的 C# SDK 包。这些包的版本号形如 `4.6.2-ciallo.g<sha>`，在 nuget.org 上不存在。

这些包通过一个**公开的静态 NuGet 源**分发，托管在 GitHub Pages 上：

```
https://shenciao.github.io/godot/index.json
```

这个源是**匿名可读**的——不需要任何 PAT、token、登录，也不用 LFS。它由引擎仓的 Release CI
（`Sleet`）在每次打 tag 发布时自动生成并推送到 `gh-pages` 分支。

这样带来的好处：

- **纯 C# 开发者无需更换编辑器**即可正常开发——只要 `restore` 能从公开源拉到 csproj
  里写死的那个 `g<sha>` 包版本即可，编辑器 binary 只在发布大版本时才需要同步一次。
- C++ 与 C# 的改动**互不阻塞**：C++ 侧先 merge → CI 发布 nuget → C# 侧再 merge 并在同一
  commit 里 bump 引用的包版本。
- 不需要手改 `global.json`，不需要编辑器自动生成 `NuGet.Config`，也不会出现 restore 报错
  逼你升级编辑器的情况。

## 一次性配置（每个游戏仓做一次）

### 1. 添加 NuGet.Config，指向公开源

在游戏仓根目录创建 `NuGet.Config`（**提交进版本库**，团队共享）：

```xml
<?xml version="1.0" encoding="utf-8"?>
<configuration>
  <packageSources>
    <!-- 自定义 Godot 引擎的公开 SDK 源。匿名可读，无需认证。 -->
    <add key="ciallo-godot" value="https://shenciao.github.io/godot/index.json" />
  </packageSources>
</configuration>
```

> 这里**不要** `<clear/>`，保留默认的 nuget.org，让普通第三方包仍能正常还原。
> Godot 的 SDK 包只在这个 ciallo 源里存在，普通包只在 nuget.org 里存在，两者天然区分。

### 2. csproj 写死精确的 SDK 版本

`.csproj` 第一行写你当前依赖的精确版本：

```xml
<Project Sdk="Godot.NET.Sdk/4.6.2-ciallo.gXXXXXXXXX">
```

`gXXXXXXXXX` 换成你要用的引擎构建对应的 git 短 sha（见 Release 名 / 引擎「关于」信息）。
MSBuild 的 SDK 版本**不支持**通配或范围，必须是精确版本——这也正是我们要的：
版本写死在 git 里，谁拉下来都构建同一份。

## 日常工作流

### 纯 C# 开发者（不碰 C++）

1. `git pull` 游戏仓。
2. 正常 `dotnet build` / 用编辑器 build。restore 会从公开源自动拉取 csproj 里写死的
   `g<sha>` 包。全程**不需要**更换编辑器（除非涉及大版本编辑器 binary 同步）。

> 如果 restore 找不到 csproj 里写的版本，说明该版本还没发布到公开源，或 sha 抄错了。
> 检查 Release 页 / feed 里实际存在的版本号即可，不需要动编辑器。

### C++/C# 同时改的开发者

这类开发者在引擎仓本地编译引擎、产出**尚未发布**的 C# 包，在游戏仓里联调新 API。

#### 本地联调

1. 引擎仓：改 C++ → 跑 glue + 把包推进一个**本地临时目录**：

   ```bash
   python modules/mono/build_scripts/build_assemblies.py \
     --godot-output-dir ./bin \
     --godot-platform=<windows|linuxbsd|macos> \
     --push-nupkgs-local ./nupkgs_out
   ```

   （或直接用 VSCode 的 `Build: Windows Debug Gen Glue` 任务后再手动推包。）

2. 游戏仓：临时在 `NuGet.Config` 里**额外**加一条本地源指向上面的目录，
   并把 csproj 的版本改成本地构建的 `g<sha>`，联调：

   ```xml
   <add key="local-dev" value="/abs/path/to/godot/nupkgs_out" />
   ```

   > 这条本地源改动是**临时的，不要提交**。可以放进 `NuGet.Config` 后用 `git stash`
   > 或干脆在本机维护一份未跟踪的覆盖，联调完删掉。

#### 发布与合并（关键约定）

C++ 侧的新 API 要让别人也能用，**唯一合法路径**是引擎仓打 tag → CI 发布到公开源。

> **合并顺序约定（务必遵守）：**
> 1. **C++ 侧先 merge** 到引擎仓主分支。
> 2. 引擎仓**打 tag** → CI 产出正式 Release 编辑器 + 把 SDK 包 `g<sha>` 发布到公开源。
> 3. **C# 侧再 merge**，且该 merge **必须**在同一批改动里把 csproj 的
>    `Godot.NET.Sdk/4.6.2-ciallo.g<sha>` 版本 bump 成 CI 刚发布的那个 `g<sha>`。
>
> 这样「用到新 API 的 C# 代码」和「它需要的引擎版本」在 git 历史里绑死，任何人拉下来
> 都不会出现代码与 API 版本错位。

#### 制度约束：本地产出的包**禁止**分发

本地 `nupkgs_out` 里的 `g<sha>` 包来自你的工作机，可能含**未提交的改动**——版本号读的是
`HEAD` 的 sha，并不反映工作区是否 dirty。一旦流出，就会出现「同一个 `g<sha>` 版本号对应
两份不同内容」，而 NuGet 全局缓存按版本号去重、先到先得，导致别人的构建与你不一致且难以察觉。
因此：

- **不要**把本地包目录、`.nupkg`、或本地临时源拷贝给任何人 / 网盘 / 制品库。
- **不要**把公开源以外的任何源写进**提交的** `NuGet.Config`。
- 需要别人也能用的新 API，**只能**通过引擎仓打 tag → CI 发布到公开源流转。
- 跨机器自用也按「打 tag 走 CI」处理，不要手搬本地包。

> 一句话：本地包是**只给自己联调用的临时产物**，对外分发的唯一合法来源是公开源（CI 发布）。

## 几个机制各自的职责

| 机制 | 来源 | 作用 |
|---|---|---|
| 公开静态 NuGet 源 | 引擎仓 Release CI（Sleet → gh-pages） | 匿名分发 SDK 包，纯 C# 开发者免换编辑器即可还原 |
| 提交的 `NuGet.Config` | 游戏仓 | 让 Godot SDK 包从公开源还原，普通包仍走 nuget.org |
| csproj 写死 `Godot.NET.Sdk/4.6.2-ciallo.g<sha>` | 游戏仓 | 把依赖的引擎版本锁死在 git 里，全队构建一致 |
| 唯一版本号 `g<sha>` | CI 设 `GODOT_VERSION_STATUS` | 保证包一定匹配对应引擎构建，且不命中旧缓存 |
