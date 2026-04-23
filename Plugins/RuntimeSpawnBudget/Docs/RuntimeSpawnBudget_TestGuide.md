# RuntimeSpawnBudget 测试指南

本文档用于验证 `RuntimeSpawnBudget` 插件的核心能力：

- 生成 / 销毁预算调度
- 对象池复用
- `ActorClass` 维度细分统计
- 蓝图异步 Spawn
- Slate 实时调试面板
- 运行时压测闭环

适用项目：

- `F:\Unreal Projects\ElectricDreamsEnv`
- 其他已启用 `RuntimeSpawnBudget` 的 UE5 项目

---

## 1. 测试目标

建议按以下顺序验证：

1. 插件能正常编译并加载
2. 自动化测试通过
3. 压测 Actor 能在关卡中触发批量请求
4. 队列、对象池、`ActorClass` 统计能够产生真实数据
5. Slate 调试面板能展示实时状态
6. A/B 对比能输出可用于简历和面试的结果

---

## 2. 前置条件

### 2.1 环境要求

- UE5.7 或兼容版本
- Visual Studio 2022
- 项目可正常打开编辑器
- 插件 `RuntimeSpawnBudget` 已启用

### 2.2 关键路径

插件路径：

```text
F:\UnrealEngine-5.7.3-release\Plugins\RuntimeSpawnBudget
```

测试项目路径示例：

```text
F:\Unreal Projects\ElectricDreamsEnv
```

---

## 3. 构建验证

### 3.1 构建插件

在引擎根目录执行：

```powershell
Engine\Build\BatchFiles\RunUAT.bat BuildPlugin `
  -Plugin="F:\UnrealEngine-5.7.3-release\Plugins\RuntimeSpawnBudget\RuntimeSpawnBudget.uplugin" `
  -Package="F:\UnrealEngine-5.7.3-release\tmp\RuntimeSpawnBudget_Package" `
  -TargetPlatforms=Win64
```

### 3.2 预期结果

- 命令返回 `Result: Succeeded`
- 生成插件二进制
- 没有 UHT 或链接错误

---

## 4. 在项目中启用插件

### 4.1 检查 `.uproject`

确认项目已启用插件：

```json
{
  "Plugins": [
    {
      "Name": "RuntimeSpawnBudget",
      "Enabled": true
    }
  ]
}
```

### 4.2 打开项目

使用 UE 编辑器打开项目，例如：

```text
F:\Unreal Projects\ElectricDreamsEnv\ElectricDreamsEnv.uproject
```

---

## 5. 自动化测试

### 5.1 运行测试

在项目目录或引擎目录执行：

```powershell
Engine\Binaries\Win64\UnrealEditor-Cmd.exe "F:\Unreal Projects\ElectricDreamsEnv\ElectricDreamsEnv.uproject" `
  -unattended -nop4 -nosplash -NullRHI `
  -ExecCmds="Automation RunTests RuntimeSpawnBudget; Quit" `
  -TestExit="Automation Test Queue Empty"
```

### 5.2 现有测试项

建议关注以下测试：

- `RuntimeSpawnBudget.Queue.Defaults`
- `RuntimeSpawnBudget.Policy.FromConfig`
- `RuntimeSpawnBudget.Pool.Initialization`
- `RuntimeSpawnBudget.Metrics.WindowStats`
- `RuntimeSpawnBudget.Metrics.ActorClassStats`

### 5.3 预期结果

- 所有测试通过
- 没有崩溃或超时
- 日志中没有 `Error` / `Ensure` / `Fatal`

---

## 6. 最小压测场景

### 6.1 场景目标

最小压测场景的目标是把以下链路真正跑起来：

- 批量 Spawn 请求
- 批量 Destroy 请求
- 对象池命中
- `ActorClass` 聚合统计
- 异步 Spawn 完成回调

### 6.2 推荐做法

你可以用两种方式开始：

1. 直接把 `ARSBPressureSpawnerActor` 拖进关卡
2. 在关卡蓝图或调试逻辑里调用 `URSBSpawnBudgetSubsystem::SpawnPressureActor`

### 6.3 关键参数

建议先用下面这一组：

- `SpawnActorClass`：选择一个简单、可复用的 `AActor` 子类
- `BurstSize`：`16`
- `BurstCount`：`8`
- `BurstIntervalSeconds`：`0.25`
- `bUseAsyncSpawn`：`true`
- `bDestroySpawnedActorsAfterDelay`：`true`
- `DestroyDelaySeconds`：`2.0`

### 6.4 预期现象

- 启动后能看到队列开始变化
- Debug 面板里 `PendingAsyncCount` 非 0
- 若对象池可复用，`PoolHitRate` 会逐渐出现数值
- `ActorClass Top` 会开始出现样本

---

## 7. Slate 调试面板

### 7.1 打开面板

在编辑器中打开 `Runtime Spawn Budget` 的 nomad tab。

### 7.2 面板内容

面板会显示：

- Spawn 队列深度
- Destroy 队列深度
- Pending Async 数量
- Last Frame 指标
- Window 指标
- Top `ActorClass` 统计

### 7.3 重点看什么

建议优先关注：

- `SpawnProcessed`
- `DestroyProcessed`
- `DroppedRequests`
- `P95QueueDelay`
- `P95SpawnTime`
- `PoolHitRate`
- `ActorClass Top`

---

## 8. A/B 对比测试

### 8.1 对比目标

用同一张地图、同一批对象、同一压力参数，比较：

1. 直接 `Spawn/Destroy`
2. 走 `RuntimeSpawnBudget`

### 8.2 观察指标

建议至少记录这 4 项：

- `P95 Frame Time`
- `P95 Queue Delay`
- `Pool Hit Rate`
- `Dropped Requests`

### 8.3 记录方式

建议每轮测试记录为一份单独文件，文件名带时间戳：

```text
RuntimeSpawnBudget_YYYYMMDD_HHMMSS.csv
RuntimeSpawnBudget_YYYYMMDD_HHMMSS.json
```

### 8.4 简历可用结论示例

- “在固定压测场景下，队列延迟 P95 下降 XX%”
- “对象池命中率达到 XX%”
- “高峰期帧尖峰次数下降 XX%”

---

## 9. 运行时压测步骤

### 9.1 推荐流程

1. 打开测试地图
2. 放入 `ARSBPressureSpawnerActor`
3. 指定 `SpawnActorClass`
4. 开启 `bUseAsyncSpawn`
5. 运行 PIE 或 Standalone
6. 观察 Debug 面板
7. 结束后记录统计结果

### 9.2 进阶流程

如果你想做更正式的验证：

1. 先跑一轮直出 `Spawn/Destroy`
2. 再跑一轮 `RuntimeSpawnBudget`
3. 保持压力参数完全一致
4. 比较输出指标

---

## 10. 常见问题

### 10.1 面板里没有数据

可能原因：

- 插件未启用
- 当前 world 不是测试关卡
- `RuntimeSpawnBudget` 子系统未初始化
- 压测 Actor 没有开始执行

### 10.2 `ActorClass Top` 一直为空

可能原因：

- 没有真正触发 Spawn
- 使用的 `ActorClass` 无效
- 场景里请求被预算或队列门限拦住了

### 10.3 异步 Spawn 没有回调

可能原因：

- `WorldContextObject` 无效
- 压测 Actor 生命周期被结束
- 请求被队列上限或超时规则丢弃

### 10.4 统计数值看起来不稳定

建议：

- 增大 `BurstCount`
- 固定测试相机或测试路径
- 每组跑 3 次以上取均值
- 统一测试机器和运行模式

---

## 11. 推荐测试矩阵

建议按下面顺序做：

1. `Queue Defaults` 测试
2. `Pool Initialization` 测试
3. `Metrics WindowStats` 测试
4. `Metrics ActorClassStats` 测试
5. 关卡内最小压测
6. A/B 对比压测

---

## 12. 最终交付物

建议最终保留以下材料：

1. 测试日志
2. A/B 数据表
3. Debug 面板截图
4. 结论文档
5. 简历项目描述

---

## 13. 建议结论模板

你可以在最后用下面这类句式总结测试结果：

```text
RuntimeSpawnBudget 在固定压测场景下可稳定控制生成/销毁开销，
能够按 ActorClass 聚合统计耗时，并通过对象池显著提升复用率。
在运行时压力下，队列延迟与帧尖峰均可量化追踪，适合用于引擎性能治理。
```

---

## 14. 压测场景搭建清单

这一节用于在 `ElectricDreamsEnv` 或其他 UE5 项目里快速搭一个可复现的测试关卡。

### 14.1 关卡准备

建议使用一个空场景或者低干扰场景，避免额外系统噪音影响结果。

清单如下：

- 新建或选择一个测试地图
- 确保场景里没有过多自动运行逻辑
- 关闭无关 AI、特效和复杂流送逻辑
- 尽量固定相机位置或固定移动路径

### 14.2 必放对象

至少放入以下内容：

1. `ARSBPressureSpawnerActor`
2. 一个简单可复用的 `AActor` 子类
3. 可选的对象池预热目标

### 14.3 `ARSBPressureSpawnerActor` 推荐参数

第一轮建议这样设置：

- `SpawnActorClass`：选择一个普通 Actor 或轻量蓝图 Actor
- `PoolKey`：留空或直接用类名
- `BurstSize`：`16`
- `BurstCount`：`8`
- `BurstIntervalSeconds`：`0.25`
- `bUseAsyncSpawn`：`true`
- `bDestroySpawnedActorsAfterDelay`：`true`
- `DestroyDelaySeconds`：`2.0`

如果想提高压力，可以逐步提高：

- `BurstSize` 到 `32`
- `BurstCount` 到 `16`
- 将 `BurstIntervalSeconds` 降到 `0.1`

### 14.4 蓝图搭建方式

如果你更习惯蓝图，可以按这个顺序接：

1. 在关卡蓝图中获取 `RuntimeSpawnBudget` 子系统
2. 调用 `SpawnPressureActor`
3. 或者直接拖入 `ARSBPressureSpawnerActor`
4. 设置它的公开参数
5. 运行 PIE 后观察调试面板

### 14.5 压测运行步骤

建议按以下顺序执行：

1. 打开测试关卡
2. 启动调试面板
3. 运行 PIE 或 Standalone
4. 等待压力 Actor 触发批量请求
5. 观察队列、异步和统计变化
6. 结束后记录结果

### 14.6 记录截图建议

建议保留以下截图或录屏：

- 压测开始前的空面板
- 压测进行中的队列变化
- `ActorClass Top` 出现样本后的面板
- 压测结束后的统计汇总

---

## 15. 面试可讲的测试结论模板

下面这些句子可以直接改成你自己的结果版本，用在简历、面试和项目总结里。

### 15.1 项目目标型

- “我做的不是单纯的 Spawn 封装，而是一个可验证的运行时生成/销毁治理层。”
- “这个系统把高频 Actor 生命周期请求从业务代码里抽出来，统一做预算调度、对象池复用和统计回归。”

### 15.2 结果型

- “在固定压测场景下，系统能够稳定限制每帧生成/销毁上限，避免瞬时尖峰。”
- “我把性能数据按 ActorClass 做了细分统计，能直接看出不同类型对象的 Spawn 成本差异。”
- “通过对象池和预算调度，队列延迟、池命中率和帧尖峰都能量化跟踪。”

### 15.3 工程型

- “这个项目的价值不只是功能本身，而是形成了从请求、调度、执行到统计的完整闭环。”
- “我把验证入口也补齐了，关卡里放一个压测 Actor 就能跑出真实数据。”
- “它更像一个 runtime 性能治理模块，而不是简单的 demo。”

### 15.4 面试深挖回答模板

如果面试官问“这个项目具体解决了什么问题”，可以这样答：

> 我主要解决的是高频 Spawn/Destroy 带来的帧尖峰问题。我的做法不是简单地延迟执行，而是统一把请求收进子系统，按预算执行、按优先级调度，并通过对象池减少重复创建成本。同时我还补了统计和可视化入口，所以它不仅能跑，还能测、能对比、能回归。

如果面试官问“这个项目怎么验证有效”，可以这样答：

> 我做了一个最小压测场景，使用 `ARSBPressureSpawnerActor` 批量触发请求，再对比直接 Spawn/Destroy 和预算系统两种方案。验证时重点看 `P95 Queue Delay`、`Pool Hit Rate`、`ActorClass` 分组耗时和最后一帧的处理数量，这样能直接判断系统是否真的在收敛尖峰。

