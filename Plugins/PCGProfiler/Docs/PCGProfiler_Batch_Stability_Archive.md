# PCGProfiler 批量测试稳定性问题归档

## 1. 问题现象
- 批量执行同一关卡时，`components / executed_nodes / harvested_events` 出现交替波动（典型 17/39）。
- JSON 文件体积在相邻轮次“一大一小”交替。
- 自动化流程比手动操作更容易拿不到稳定 node 数据。
- 部分轮次出现过早结束，或看似结束后仍有后续资源变化。

## 2. 根因分析
- 采集范围过宽：早期 Harvest 遍历 `GEngine` 下所有 `Editor/PIE/Game` world，容易混入非目标 world 数据。
- 组件集合漂移：每轮未固定组件快照，受到子关卡/流式加载/运行时变化影响，导致轮间统计口径不一致。
- 轮间尾任务串扰：上一轮异步尾任务可能进入下一轮统计窗口。
- 批量前状态门槛不足：仅靠单一 idle 判定不够，缺少“连续 idle + 组件计数稳定”联合判定。
- 自动化与手动执行节奏差异：脚本串行调用时，异步任务与主线程调度窗口不同于人工步骤，暴露 race 问题。

## 3. 已实施修复

### 3.1 批量前稳定门槛
- 连续 N 次（当前 N=3）判定 `IsRunIdle=true`。
- `GetActivePCGComponentCount()==0` 才累计稳定 tick。
- 组件总数需在稳定窗口内（当前 3 秒）保持不变。
- 预启动 gate 设最大等待时间（当前 120 秒），超时则终止批次并告警。

### 3.2 轮间冷却
- 每轮之间固定冷却（当前 3 秒，可调），隔离前一轮尾部异步任务。

### 3.3 固定 world 上下文
- 批次开始时锁定目标 world path。
- 后续每轮强制在该 world 执行；若检测到 world 漂移，继续等待而非直接启动下一轮。

### 3.4 极致一致性模式（Purge）
- 批跑默认启用 `CleanupAllPCGComponents(true)` 后再 `GenerateAllPCGComponents(true)`。
- 牺牲部分耗时，换取更强轮间一致性。

### 3.5 采集作用域隔离（关键修复）
- Run 启动时记录：
  - `OneClickWorldPath`
  - `OneClickComponentScopeKeys`（目标 world 下组件快照）
- Harvest 时仅收集：
  - 目标 world 内事件
  - 组件快照中的事件
- 避免其他 world/组件污染本轮统计，显著降低 17/39 交替问题。

## 4. 关键代码位置
- Subsystem 头文件：
  - `Plugins/PCGProfiler/Source/PCGProfiler/Public/PCGProfilerSubsystem.h`
- Subsystem 实现：
  - `Plugins/PCGProfiler/Source/PCGProfiler/Private/PCGProfilerSubsystem.cpp`
- Tools 菜单与批跑入口：
  - `Plugins/PCGProfiler/Source/PCGProfiler/Private/PCGProfilerModule.cpp`

## 5. 当前使用建议
- 回归基准/大规模批量：使用“严格模式”（锁定 world + 组件快照 + purge + 稳定门槛）。
- 日常探索调试：后续可提供“自动模式”（不锁 world）以提升灵活性。
- 若目标是可比较基线数据，优先严格模式。

## 6. 验证清单
- 日志应出现：
  - `OneClick started ... world=... scoped_components=...`
  - `Harvest ... scoped_world=... scoped_components=...`
- 同一批次多轮中，`scoped_world` 应一致。
- `scoped_components` 在同一场景应基本稳定（除非场景本身配置变化）。
- JSON 的 `node_aggregates` 数量与 `harvested_events` 不应再出现固定交替振荡。

## 7. 已知权衡
- 锁定 world/组件会降低自动适配性，但提升回归可比性。
- Purge 会增加单轮耗时，但提升一致性。
- 因此建议提供模式开关：`Strict / Auto / Hybrid`。

## 8. 后续可选增强
- 将 `idle ticks / stable window / cooldown / purge` 做成菜单可配置项。
- 增加 batch 级汇总报告：波动率、回归阈值判定、pass/fail 输出。
- UI 展示批次内每轮关键统计（component count, events, p95, thread ratio）。
