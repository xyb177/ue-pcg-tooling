# UE5 PCG Circle Legacy

`feature/pcg-circle-legacy` 是一个 Unreal Engine C++ / PCG 互操作实验项目，目标是把场景中的圆形几何组件接入 UE5 PCG 管线，让 PCG Graph 可以直接读取 `CircleComponent` ，并输出兼容现有 `PolyLine` 工作流的数据。

这个分支偏“引擎功能验证”和“架构取舍记录”：它既保留了源码集成方案，也提供了更容易迁移的独立插件方案。

## 项目重点

- 新增 Blueprint 可添加的 `UCircleComponent`，支持半径、分段数、完整角度、闭环、调试绘制等参数。
- 提供 PCG 节点 `Get Circle Data` 和 `Get Ellipse Data`，从 Actor 组件中读取解析几何。
- 输出 `UPCGCircleData` / `UPCGEllipseData`，并继承 `UPCGPolyLineData`，可以继续接入现有 PCG PolyLine / Spline 消费节点。
- 保留 Actor Tag 与 Component Tag，便于在 PCG 流程中继续做过滤和分组。
- 同时保留 source-mode 与 plugin-mode 两种实现路径，方便比较“改引擎源码”和“插件互操作”的维护成本。

## 目录结构

```text
Plugins/CircleComponentPCG/
  CircleComponentPCG.uplugin
  Source/
    CircleComponentPCG/          # Runtime 组件：CircleComponent / EllipseComponent
    CircleComponentPCGEditor/    # 编辑器详情面板定制
    CircleComponentPCGInterop/   # PCG 数据类型与 Get Data 节点

Engine/Plugins/Experimental/PCGInterops/PCGCircleInterop/
  Source/PCGCircleInterop/       # 早期独立 Circle -> PCG 互操作实验
```

## 核心功能

### CircleComponent

`UCircleComponent` 是一个可放入 Actor / Blueprint 的圆形组件，提供类似 SplineComponent 的几何查询能力：

- 按角度、归一化角度或距离获取位置、切线、法线和 Transform。
- 支持圆弧与闭环圆。
- 缓存采样点、切线、法线和弧长表，便于 PCG 或其他系统重复读取。
- 支持运行时与编辑器中的调试绘制。


### PCG 互操作

插件提供一个新的PCG 节点：

- `Get Circle Data`

节点会从选中的 Actor 中解析对应组件，并输出 `EPCGDataType::PolyLine`。这样可以复用 UE 现有 PCG 生态里的 PolyLine / Spline 节点，而不必为每个下游节点重新适配圆数据。

## 安装与启用

推荐使用独立插件方式：

1. 将 `Plugins/CircleComponentPCG` 复制到目标 UE 项目的 `Plugins` 目录，或保留在当前引擎源码工作区中。
2. 在项目中启用 UE 自带的 `PCG` 插件。
3. 启用 `CircleComponentPCG` 插件。
4. 重新生成项目文件并编译 `UnrealEditor`。
5. 在 Blueprint 中通过 `Add Component` 添加 `CircleComponent` 。
6. 在 PCG Graph 中使用 `Get Circle Data`读取组件几何。

## 构建示例

在 Windows 环境下可使用：

```bat
Engine\Build\BatchFiles\Build.bat UnrealEditor Win64 Development -NoHotReloadFromIDE
```

如果只验证插件模块，可按需指定模块：

```bat
Engine\Build\BatchFiles\Build.bat UnrealEditor Win64 Development -Module=CircleComponentPCG -NoHotReloadFromIDE
Engine\Build\BatchFiles\Build.bat UnrealEditor Win64 Development -Module=CircleComponentPCGInterop -NoHotReloadFromIDE
```

## 使用示例

典型流程：

1. 创建或打开一个 Actor Blueprint。
2. 添加 `CircleComponent`，设置 `Radius`、`Segments`、`Full Angle` 等参数。
3. 在关卡中放置该 Actor。
4. 创建 PCG Graph，并添加 `Get Circle Data` 节点。
5. 将输出连接到支持 `PolyLine` 的 PCG 节点，例如路径采样、边界生成或 Surface From Spline 类流程。


## 设计取舍

这个项目重点不是“单独做一个圆形绘制组件”，而是验证一种更完整的 PCG 输入扩展方式。

实现过程中比较了三种路线：

| 路线 | 优点 | 代价 |
| --- | --- | --- |
| 修改 PCG 核心源码 | 调用链短，节点可直接进入内置体系 | 侵入性强，升级 UE 时冲突风险高 |
| 独立插件互操作 | 易迁移、易回收、风险小 | 需要维护额外插件模块 |
| 新增完全独立的 CircleData 类型体系 | 能保留更多解析语义 | 下游节点适配成本更高 |

当前分支保留插件互操作方案作为主要展示路径，同时保留源码集成痕迹作为架构对比材料。

## 注意事项

- 不建议在同一个构建中同时启用源码内置版本和独立插件版本，避免类名、节点名或符号重复。
- `PolyLine` 输出可以很好地接入现有 PCG 节点，但会把“真圆”的解析语义转换成可采样路径语义。

