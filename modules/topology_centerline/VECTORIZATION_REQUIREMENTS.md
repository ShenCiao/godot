# Centerline 像素图矢量化需求

## 1. 文档目的

本文面向后续接手算法的人类矢量化专家，定义本项目认为什么是“好的”线稿 centerline 矢量化，并归纳需要重点防止的反直觉结果。

“符合人类直觉”本身不是可完整量化的目标。同一幅像素图可能存在多种合理的 stroke 分解，尤其是在粗交点、粘连、遮挡和低分辨率区域。因此，本项目不试图用单一误差函数替代人的判断，而是先明确一组不可接受的失败模式、必须保持的拓扑不变量，以及可辅助人工评估的量化指标。

目前的矢量化算法几乎无法使用。

## 2. 输入、输出与使用场景

当前入口是 Godot 模块 `topology_centerline` 中的 `TopologyCenterlineVectorizer`：

- `vectorize(source, params)`：接受 `Image` 或 `Texture2D`。
- `vectorize_image(image, params)`。
- `vectorize_texture(texture, params)`。

每条输出 stroke 是一个 Dictionary：

- `positions`：图像像素坐标系中的 polyline 点。
- `radii`：与 `positions` 等长的半径数组。

公开参数：

- `threshold`：覆盖率阈值。设为 `0` 时保留完整灰度梯度信息。
- `max_thickness`：输出半径上限，`0` 表示不限制。
- `despeckling`：按四连通面积删除小前景分量。
- `polyline_max_error`：曲线拟合允许的最大几何误差，单位为像素。
- `polyline_max_segment_length`：输出 segment 的最大长度，`0` 表示不限制。

这些结果不仅用于显示，还要用于后续 arrangement 构建。因此，交点不能只是视觉上靠近；参与同一 junction 的 polyline 必须包含数值上相同的显式交点。

## 3. 总体质量定义

一个好的结果应当同时满足以下四点：

1. **拓扑正确**：连接、断开、分支和环与输入线稿表达的结构一致。
2. **stroke 归属合理**：每条视觉线路只被覆盖一次，junction 两侧按照整体走势形成自然的 continuation。
3. **几何自然**：中心线位于人眼感知的笔画中部，交点位置符合各分支的来向，曲线没有像素阶梯和局部突起。
4. **表示简洁**：没有冗余短枝、重复 stroke 和无意义密集采样，同时保留所有必要的拓扑锚点。

四者存在冲突时，优先级为：

1. 不制造错误连接或错误断开。
2. 不重复覆盖视觉线路，不产生伪分支。
3. 保持 junction 的位置、分支配对和切线走势自然。
4. 在固定拓扑锚点的前提下平滑、拟合和简化。
5. 调整采样密度、半径细节和性能。

平滑或简化不得以牺牲拓扑为代价。

## 4. 必须保持的拓扑不变量

### 4.1 前景连接关系

- 输入中确实连续的细线不能因为局部只有一个像素宽而断开。
- 水平、垂直和对角的一像素 neck 都应保持连续。
- 相互靠近但没有接触的 stroke 不得被桥接。
- 被白色间隙分开的端点不得因为距离小而连接。
- 同属一个较大前景分量，也不代表任意两处可以直连。例如 U 形线条内部的白槽不能被横跨。
- 自接触或细 neck 形成的环必须保留，不能被 MST、剪枝或拟合阶段打开。

本项目用八连通理解前景的像素连续性，但对角移动必须避免不合理的 corner cutting；背景白槽和显式间隙仍然具有结构意义。

### 4.2 junction 必须成为共享图节点

- T、Y、X 及非对称 junction 的每个真实分支都必须到达同一个共享矢量节点。
- 所有参与 polyline 在该处必须含有完全相同的 `positions` 样本。
- 不接受两条曲线仅仅相交于 segment 内部、端点彼此接近，或者依靠半径覆盖看起来连接。
- junction 锚点在平滑和曲线拟合中必须固定。允许优化周围控制点，但不能移动或删除共享节点。

### 4.3 每条视觉线路只能被解释一次

- 不允许两条起点不同的 stroke 沿同一分支继续，重复覆盖同一视觉线路。
- junction 的每条 branch 必须恰好被一个输出路径使用。
- 环路、平行路径和 dummy node 的内部表示不能泄漏成重复输出。
- route pairing 应当是图上的全局一致决策，不能由各分支独立贪心后产生重叠。

### 4.4 剪枝不能删除真实结构

- 叶枝剪枝不能删除一个前景分量的最后一条有效路径。
- 不能仅凭“只有 1 至 3 个点”删除 stroke。短 stroke 可能是真实的细节、短分支或低分辨率连接。
- 短 stroke 只有在拓扑上冗余、被其他路径覆盖，或明确属于 junction 内部的离散化碎片时才可删除。

## 5. junction 的几何与 stroke 配对

### 5.1 交点位置应符合整体走势

交点不应简单选择“离现有中心线最近的像素点”。对于一条弯曲或斜插入横线的 T junction，目标点应综合考虑：

- 各分支远离 junction 后的稳定切线方向。
- 分支中心线向交汇区域的趋势外推。
- 粗 junction 前景区域的视觉中心。
- 交点移动后各分支需要产生的弯曲能量。

尤其不能忽略支线的来向，把它生硬拉到主干最近点。该做法会在支线末端产生凸起、折弯或短钩。

### 5.2 continuation 应由趋势决定

- X junction 通常应把方向最连续的两侧配成两条穿越 stroke。
- T junction 的主干应保持穿越，支线终止于共享交点。
- Y junction 可能存在语义歧义，但仍应选择整体曲率最自然、不会重复覆盖 branch 的最简分解。
- 配对判断应使用 junction 外稳定区域的切线，而不是使用交点内部受粗像素团影响的局部方向。

这类规则应通过统一的图优化、方向场或曲率代价表达，而不是为 T、X、Y 分别堆叠图形特判。

### 5.3 junction 周围必须平滑但不能“铺平”

允许 junction 本身存在不可微的多分支结构，但每条进入和离开 junction 的 stroke 应当保持自身走势平滑。不可接受的结果包括：

- 某条对角线在 X junction 处形成 Ω 形或回钩。
- 一条 stroke 被另一条 branch 的像素团横向拉拽。
- 为了填满粗交点而生成多条杂乱短 stroke。
- junction 中心周围出现只有两三个点的碎枝。
- 为强制共享交点而在主干或支线上制造一个额外突起 segment。

正确做法是先确定共享拓扑节点和 branch pairing，再以固定锚点、切线连续和曲率最小为约束优化周边几何。

## 6. polyline 平滑、简化与采样

### 6.1 必须消除像素阶梯

直接跟随像素骨架会产生密集的水平、垂直和对角阶梯。输出不应只是原始像素锯齿的高密度 polyline 表达。

平滑和拟合应利用较长范围的走势，而不是逐点平均。适合的目标包括局部线性或多项式拟合、曲率正则、方向场引导，以及固定拓扑节点的 constrained fairing。

### 6.2 简化程度必须可控

- `polyline_max_error` 控制允许偏离中心线证据的程度。
- `polyline_max_segment_length` 控制 segment 和 points 的密度。
- 参数变密或变疏时，几何采样可以变化，但 stroke 数、连接关系和 junction 坐标不应变化。
- 简化不能越过 junction、端点、强曲率点和其他 mandatory points。
- 不能为了减少点数把弯曲支线变成直线，也不能因参数较密重新暴露像素阶梯。

### 6.3 平滑范围

平滑属于核心矢量化结果的一部分，应在原生算法侧完成。调用侧可以做显示层插值，但不应负责修复 junction、连接关系、重复路径或 mandatory points，否则不同调用方会得到不一致的拓扑。

## 7. 半径要求

半径表示局部 stroke 的视觉半宽。目前半径证据来自私有的 Tahoma2D centerline 后端，其普通线段半径质量可作为基准保留。

- 半径应沿 stroke 平滑变化，不随 polyline 简化产生尖峰。
- junction 内部的粗像素团不能把某条 branch 的中心线拉向另一条 branch。
- 半径估计和拓扑定位应解耦。半径可以描述共享节点附近的粗细，但不能用“圆盘相交”替代显式拓扑交点。
- `max_thickness` 只应裁剪输出半径，不应改变中心线图。

## 8. 已观察到的反直觉失败模式

后续算法必须主动覆盖以下风险：

1. **重复走线**：多条 stroke 共用同一 branch，视觉线路被画两次。
2. **junction 拉拽**：X/T/Y 交点附近出现 Ω 形、回钩、横向偏移或突起。
3. **交点放置偏移**：目标点靠近主干，却不符合斜入或弯入支线的趋势。
4. **交点碎枝**：粗 junction 周围生成只有少数点的独立短 stroke。
5. **铺平交点**：为了覆盖交点像素生成多余 stroke，而不是形成一个简洁共享节点。
6. **像素阶梯外泄**：输出点过密，完整保留原图锯齿。
7. **过度简化**：点数减少后曲线失去弯曲走势，或真实 junction 不再显式相交。
8. **细线断裂**：一像素线、对角线或 thin neck 在聚类、MST 或剪枝后消失。
9. **错误桥接**：近邻 stroke、白色 gap 或同一分量内的白槽被连接。
10. **环路损坏**：细 neck loop、自接触 loop 被打开、合并或重复输出。
11. **端点丢失**：stroke 只覆盖局部，未延伸到视觉端点，或被半径支持范围提前截断。
12. **半径污染几何**：junction 的大半径改变中心线方向或共享节点位置。
13. **参数改变拓扑**：只调整采样密度，却导致 stroke 数、branch pairing 或交点发生变化。

## 9. 评估与验收方法

### 9.1 必须保留的 fixture 类型

自动测试至少覆盖：

- 对称和非对称 T、Y、X junction。
- 弯曲、斜入和粗线 junction。
- near-touching strokes。
- 水平与对角 thin neck。
- 完整一像素对角线。
- 同一前景分量中的白槽。
- 一像素白 gap。
- thin-neck loop 和 self-touching loop。
- 不同 `polyline_max_error`、`polyline_max_segment_length` 下的拓扑稳定性。
- 半径与 Tahoma2D 基准的偏差。

### 9.2 可量化的代理指标

以下指标有助于发现问题，但不能单独定义视觉质量：

- 输入与输出的前景分量、环和 junction degree 是否一致。
- 每个 raster junction 是否对应唯一共享矢量坐标。
- 每条 branch 被输出路径使用的次数是否恰好为一次。
- 输出端点到视觉端点的距离。
- junction 外稳定切线与输出切线的夹角。
- 曲线对源 stroke corridor 的最大偏离和回退量。
- 拟合误差、segment 最大长度、点密度和曲率尖峰。
- 极短 stroke 的数量、长度及其是否提供独立拓扑信息。
- 半径误差和沿弧长的变化平滑度。

像素重建误差或 Hausdorff distance 也只能作为辅助。一条重复 stroke、错误配对的 X junction，可能仍有很低的重建误差，却明显不符合人的 stroke 理解。

### 9.3 人工视觉审查

关键 fixture 应同时检查：

- 原图、中心线和带半径重建结果的叠加图。
- junction 的放大图及各 branch 的远端走势。
- polyline 点和 mandatory points 的可视化。
- 每条 stroke 单独着色后的 branch 使用情况。

人工审查重点不是“是否穿过所有黑像素”，而是一个画线的人是否可能用这些 stroke 自然地重画输入。

## 10. 算法设计约束与建议

- 不使用针对某个 fixture 坐标、某种字母形状或 T/X/Y 名称的暴力 `if/else`。
- 不在 C# 或调用侧修补核心拓扑错误。
- 不以固定点数阈值无条件删除短 stroke。
- 不让平滑、RDP 类简化或半径后处理移动 junction anchor。
- 不以局部最近点替代 branch 趋势外推。

更合适的总体方向是：先从像素证据构建嵌入式 stroke graph，固定其连通性和 junction 节点；再以全局 branch pairing、切线连续、曲率、公平性、源图支持和表示稀疏性组成统一目标，优化 junction 位置与曲线。Tahoma2D 的半径结果可继续作为独立宽度证据；Topology-Driven Vectorization 和 PolyVector Flow 中关于 junction、方向场和全局连续性的思想更适合用于决定几何与配对。

## 11. 当前基线

当前 smoke 已覆盖上述主要拓扑风险，并要求：

- T、Y、X 等 junction 输出自然的最简 stroke 分解。
- 一像素对角线和 thin neck 不断裂。
- 一像素 gap 和白槽不被桥接。
- junction 在所有参与 polyline 中使用相同坐标。
- 半径基准与 Tahoma2D 参考保持接近。

1080p 性能目前仍是秒级。性能优化应继续进行，但不得通过减少拓扑分析、跳过 junction 约束或放宽连接正确性来换取速度。视觉和拓扑正确性高于固定耗时门槛。

## 12. 参考论文与项目

### 12.1 主要论文

1. Gioacchino Noris, Alexander Hornung, Robert W. Sumner, Maryann Simmons, and Markus Gross. **Topology-Driven Vectorization of Clean Line Drawings**. *ACM Transactions on Graphics*, 32(1), 1–11, 2013. DOI: [10.1145/2421636.2421640](https://doi.org/10.1145/2421636.2421640)

   这是当前 topology pipeline 的主要理论来源。当前实现参考了其中的 gradient-based pixel clustering、绘图拓扑提取、global/local MST、centerline 平滑，以及通过 reverse drawing 推断 junction stroke configuration 的总体分阶段结构。论文特别强调 junction 不能只依靠固定局部邻域判断，这也是本文档中“用远端走势而非交点内部最近点决定配对”的主要依据。

   当前本地副本：`C:\Users\Ciao\Downloads\Topology-Driven_Vectorization_of_Clean_Line_Drawings.pdf`。

2. Ivan Puhachov, William Neveu, Edward Chien, and Mikhail Bessmeltsev. **Keypoint-Driven Line Drawing Vectorization via PolyVector Flow**. *ACM Transactions on Graphics*, 40(6), Article 266, 17 pages, 2021. DOI: [10.1145/3478513.3480529](https://doi.org/10.1145/3478513.3480529)

   当前实现主要借鉴其问题分解和 junction 几何思想：先区分 endpoint、junction 和 sharp corner 等 keypoints，先确定 topology，再通过 drawing-aligned directions 处理 Y、X、T junction 周围的方向歧义，最后优化曲线几何。本文档关于稳定切线、branch pairing、方向场和“先拓扑、后几何”的要求受此论文影响。

   当前代码没有完整复现该论文的神经网络 keypoint detector、frame field 求解器或 PolyVector Flow 数值优化，因此不能把当前模块描述成该论文的完整实现。

   当前本地副本：`C:\Users\Ciao\Downloads\polyvector_flow.pdf`。

### 12.2 参考项目与代码

1. **Tahoma2D** — [tahoma2d/tahoma2d](https://github.com/tahoma2d/tahoma2d)

   当前实现直接使用从 Tahoma2D centerline vectorizer 提取并适配的私有后端，代码位于 `thirdparty/tahoma2d_centerline`。它负责提供可靠的局部半径证据；拓扑图、junction placement、reverse drawing、polyline 平滑和简化仍由 `topology_centerline` 模块负责。

   该第三方代码按 BSD 3-Clause License 使用，具体版权与许可见 [Tahoma2D license](../../thirdparty/tahoma2d_centerline/LICENSE.txt)。修改或重新同步上游代码时必须保留许可声明。

2. **Tahoma2D centerline 算法本地研究工程** — `C:\dev\tahoma2d-vectorization-algorithm`

   该目录是开发期间用于独立阅读、提取和对照 Tahoma2D centerline 行为的参考工程。它不属于 Godot 运行时 module，也不是当前构建依赖。接手者可用它追踪 Tahoma2D 的 skeletonization、junction recovery、polygonization 和 stroke conversion 流程，并与 `thirdparty/tahoma2d_centerline` 中的适配版本核对。

3. **Godot arrangement_2d centerline API** — `modules/arrangement_2d/centerline_vectorizer.h`

   当前 Godot API 的输入形式、参数字典和 `positions`/`radii` 输出约定最初以该接口为兼容参考。它是接口和下游 arrangement 集成参考，不是当前 topology 算法的理论来源。后续更换内部算法时，应继续保持 `TopologyCenterlineVectorizer` 的公开数据契约，或明确迁移 arrangement 调用方。
