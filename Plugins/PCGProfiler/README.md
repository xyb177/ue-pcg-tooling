# PCG Profiler

UE5 插件 · PCG 图节点级性能分析 · 非侵入式采集 · 诊断→回归闭环

## 解决的问题

PCG 图的执行是 UE5 的黑盒：哪些节点最耗时？瓶颈在 GameThread 还是 Worker？缓存命中率多少？多轮回归稳定吗？

PCGProfiler 接入 PCG 框架的 Execution Inspection API，在不修改 PCG 图的前提下采集节点粒度性能数据，输出结构化 JSON 报告与交互式 HTML 看板。

## 与 UE 内置工具对比

| 能力 | PCGProfiler | Unreal Insights | `stat PCG` | 手动打点 |
|------|:---------:|:-------------:|:--------:|:-----:|
| 节点级耗时 (P50/P95/CV) | ✅ | ⚠️ 单次 | ❌ | ⚠️ 手工 |
| 线程分布 (GT/Wk) | ✅ | ✅ | ❌ | ❌ |
| 跨轮稳定性和回归对比 | ✅ | ⚠️ 手动 | ❌ | ❌ |
| IO 规模 (点数) | ✅ | ❌ | ❌ | ❌ |
| 自动化批量采样 | ✅ | ❌ | ❌ | ❌ |
| 非侵入 (不改图) | ✅ | ✅ | ✅ | ❌ |
| 结构化报告导出 | ✅ JSON+HTML | ✅ Trace | ❌ | ❌ |

**定位：** 不替代 Insights，填补 PCG 级性能诊断和自动化回归的空白。

## 架构

```
┌─ UI 层 ────────────────────────────────────────┐
│  Slate Panel (Start/End/OneClick/Batch/Export)  │
│  + 节点表格 (排序/过滤) + 事件时间线              │
├─ 子系统层 ──────────────────────────────────────┤
│  UEngineSubsystem (UPCGProfilerSubsystem)        │
│    ├ StartRun → 启用 Inspection → EndRun → Harvest│
│    ├ RecordNodeEvent → NodeStats + NodeEvents    │
│    ├ OneClick / RunBatch → 自动回归采样          │
│    └ Export → JSON + HTML Dashboard        │
├─ 数据层 ────────────────────────────────────────┤
│  FPCGGraphExecutionInspection (引擎内置)          │
│    └ Timer / Stack / Pin 数据                    │
└─────────────────────────────────────────────────┘
```

## 核心功能

### 1. 节点级多维采集

| 维度 | 指标 |
|------|------|
| 耗时 | Total / Self / P50 / P95 / Min / Max / Avg / StdDev / CV |
| 阶段分解 | PrepareData / Execute / PostExecute / QueueWait |
| 线程分布 | GameThread / Worker（基于 `CanExecuteOnlyOnMainThread` 推断） |
| 缓存诊断 | CacheHit / CacheMiss / MissReason (input_change/version_change/unknown) |
| IO 规模 | Input/Output Count & Points (含 P95/CV) |
| 内存估算 | 进程级差分测量 + 点数估算回退 |

### 2. 采集工作流

| 模式 | 说明 |
|------|------|
| **手动** | Start Run → 触发 PCG 生成 → End Run → Export JSON |
| **One-Click** | 自动: StartRun → CleanupAll → GenerateAll(bForce) → WaitIdle → EndRun → Export |
| **RunBatch** | One-Click × N 迭代，含 cooldown|

### 3. 自动化回归

- `per_run_summary`: 每轮独立汇总（线程负载/缓存命中/并行效率评分） 
- DAG DP 关键路径提取


### 4. 数据质量机制

- 大整数精度保护：`_i64` 字符串回退字段
- 数据来源标注：`first_seen_time_source` / `thread_source` / `memory_measurement_mode`
- 缺失字段检测和稀疏样本说明
- 串流式 JSONL Chunk Flush (50k events 阈值) 防止 OOM

## 快速使用 (Editor)

**Panel 操作：**
1. Window → PCG Profiler Panel 打开面板
2. `Start Run` → 触发 PCG 生成 → `End Run` → `Export JSON`
3. 或直接 `One Click` 一键完成全流程
4. 或设定迭代次数后 `Run Batch` 批量采样

**Console 命令：**
```
PCGProfiler.StartRun RunName
PCGProfiler.EndRun
PCGProfiler.ExportJson
PCGProfiler.RunBatch 10
PCGProfiler.WaitForRunComplete
PCGProfiler.SetSamplingEnabled false
```

