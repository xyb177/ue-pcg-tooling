# RuntimeSpawnBudget

`RuntimeSpawnBudget` 是一个 UE5 运行时 `Spawn / Destroy` 预算调度原型插件。

它的目标是把高频 Actor 生命周期请求集中到一个 `UWorldSubsystem` 中，按帧预算、优先级和对象池策略分批处理，并提供可观察的队列、耗时和池化指标。

需要特别说明：启用本插件后，项目里原有的 `SpawnActor` / 蓝图 `Spawn Actor from Class` 不会被自动接管。只有主动调用 `EnqueueSpawn`、`EnqueueDestroy`、`SpawnActorAsync`，或者使用内置压测 Actor 发起的请求，才会进入本插件的调度逻辑。

## 当前定位

这是一个用于探索运行时 Actor 生成/销毁治理的工程原型，而不是已经完整接入某个大型项目的最终性能方案。

它适合用来验证：

- 高频 `Spawn / Destroy` 请求是否能被统一排队
- 每帧执行量是否能被预算限制
- 对象池是否能复用短生命周期 Actor
- 请求完成、失败、过期、队列拒绝等路径是否有统一通知
- 调试面板是否能展示插件内部状态


## 已实现功能

### 运行时子系统

- 基于 `UWorldSubsystem` 实现 `URSBSpawnBudgetSubsystem`
- 提供统一 Spawn 入口：`EnqueueSpawn`
- 提供统一 Destroy 入口：`EnqueueDestroy`
- 支持在插件启用后随 World 初始化
- 支持非游戏线程调用，并切回 Game Thread 后返回真实入队结果

### 预算调度

- Spawn 请求按队列延迟执行
- Destroy 请求按队列延迟执行
- 支持每帧 Spawn 数量上限
- 支持每帧 Destroy 数量上限
- 支持每帧 Spawn 时间预算
- 支持每帧 Destroy 时间预算
- 支持四档优先级：`Critical`、`High`、`Normal`、`Low`

### 对象池原型

- 支持按 `PoolKey` 或 `ActorClass` 建立对象池
- 支持 Spawn 前优先尝试从池中获取对象
- 支持 Destroy 时回收到对象池
- 支持对象池容量限制
- 支持池预热 `PrewarmPool`
- 支持空闲池清理
- 支持 `IRSBPoolableInterface` 回调：`OnPooledAcquire`、`OnPooledRelease`、`OnPooledReset`



### 压测入口

- 提供 `ARSBPressureSpawnerActor`
- 可配置 `SpawnActorClass`、`BurstSize`、`BurstCount`、`BurstIntervalSeconds`、`bUseAsyncSpawn`、`DestroyDelaySeconds`
- 可用于快速制造固定压力，观察队列、池命中率和处理量变化


### 调试面板

- 提供编辑器面板：`Runtime Spawn Budget`
- 可查看 Spawn / Destroy 队列长度
- 可查看 `PendingAsyncCount`、上一帧处理数量、丢弃请求数量
- 可查看队列延迟、Spawn Phase 耗时、Destroy Phase 耗时
- 可查看对象池命中率和按 `ActorClass` 聚合的 Spawn Cost

### 指标统计

- 统计队列等待时间
- 统计窗口级平均值、P95、P99
- 统计每帧 Spawn Phase 耗时
- 统计每帧 Destroy Phase 耗时
- 统计池命中率
- 统计按 `ActorClass` 聚合的单次 Spawn Cost




## 尚未完成或尚未充分验证

### 尚未接入真实项目 Spawn 链路

插件目前不会自动替换项目中的原生 `SpawnActor`。

还需要在真实项目中选择具体生成点，例如 Lyra 中的投射物生成、临时道具生成，或者 Electric Dreams 中可独立生成的环境 Actor，然后手动把该路径改为走 `EnqueueSpawn` 或 `SpawnActorAsync`。

### 尚未完成严格 A/B 性能验证

目前还缺少一套完整的 Direct Spawn 与 RuntimeSpawnBudget 对比报告。

理想验证应包含：

- 同一张地图
- 同一个 `ActorClass`
- 同一批量参数
- Direct `SpawnActor / DestroyActor`
- `RuntimeSpawnBudget` 调度路径
- UE `stat unit` / `stat game`
- Unreal Insights trace
- 插件面板指标

建议记录：

- Direct 组 P95 帧时间
- RuntimeSpawnBudget 组 P95 帧时间
- P95 Queue Delay
- Dropped Requests
- Pool Hit Rate
- ActorClass Avg / P95 Spawn Cost

### 尚未验证复杂 Actor 池化安全

对象池目前适合轻量、状态简单、生命周期可控的 Actor。

复杂 Actor 仍需额外验证组件状态、碰撞状态、Tick 状态、Owner 上下文、Gameplay Ability / Inventory 依赖，以及 BeginPlay / EndPlay 是否和池化模型冲突。

在验证完成前，不建议直接池化复杂 Pawn、Controller、Ability 相关 Actor。


### 尚未验证大型公开项目完整闭环

计划验证项目包括：

- `Lyra Starter Game`
- `Electric Dreams Environment`

其中 `Electric Dreams Environment` 是 Epic 官方 PCG 示例项目，更适合高压力环境验证；`Lyra Starter Game` 更适合做公开可复现的玩法项目验证。

目前还没有完成对这些项目真实生成链路的完整替换和测试报告。

## 使用方式

### 启用插件

将插件放入项目：

```text
<YourProject>/Plugins/RuntimeSpawnBudget
```

确认插件文件存在：

```text
<YourProject>/Plugins/RuntimeSpawnBudget/RuntimeSpawnBudget.uplugin
```

然后在 UE 编辑器中启用：

```text
Edit -> Plugins -> RuntimeSpawnBudget -> Enabled
```

重启编辑器并重新编译项目。

### C++ 调用示例

```cpp
FRSBSpawnRequest Request;
Request.ActorClass = TargetActorClass;
Request.Transform = SpawnTransform;
Request.Priority = ERSBRequestPriority::Normal;
Request.PoolKey = TargetActorClass ? TargetActorClass->GetFName() : NAME_None;
Request.bAllowPooling = true;

if (URSBSpawnBudgetSubsystem* Subsystem = World->GetSubsystem<URSBSpawnBudgetSubsystem>())
{
    const bool bAccepted = Subsystem->EnqueueSpawn(Request);
}
```

### 蓝图测试方式

最简单的手动测试方式：

1. 在地图中放置 `ARSBPressureSpawnerActor`
2. 设置 `SpawnActorClass`
3. 设置 `BurstSize`、`BurstCount`、`BurstIntervalSeconds`
4. 打开 `Window -> Runtime Spawn Budget`
5. 运行 PIE
6. 观察队列、处理数量、池命中率和 ActorClass 统计






