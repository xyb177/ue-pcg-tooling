# PCGProfiler 实测问题复盘（2026-04-13）

## 1. 现象：自动化脚本执行后 UE 卡住
- 表现：脚本运行期间编辑器无法操作，PCG 资源变化也不及时可见。
- 原因：脚本在主线程执行阻塞等待（同步 sleep/长阻塞调用），影响了编辑器 tick 与异步任务推进。
- 解决：改为 editor tick 回调驱动的异步状态机；等待逻辑不阻塞主线程。

## 2. 现象：脚本“看起来等完了”，但数据为空或 node 很少
- 表现：JSON 导出成功，但 node/事件显著偏少或为 0。
- 原因：End+Export 在 PCG 异步任务真正结束前触发，采样窗口过早关闭。
- 解决：结束判定改为优先读取 C++ 子系统状态（IsRunIdle + ActiveCount），仅在不可用时回退。

## 3. 现象：Python 线程调用 WaitForRunComplete 报错
- 日志：Attempted to access Unreal API from outside the main game thread.
- 原因：UE API 需主线程访问，Python 后台线程直接调引擎对象会触发线程约束。
- 解决：不在 Python 子线程直接访问 UE 子系统；改为主线程 tick 轮询 IsRunIdle。

## 4. 现象：连续两轮结果差异大（components/nodes 波动）
- 表现：同一关卡多次运行，harvest 组件数与 executed_nodes 出现大幅变化。
- 原因：
  - 流送/子关卡状态在 run 前后变化，统计对象集合漂移。
  - “脚本起始扫描到的组件数”与“Harvest 结束时遍历组件数”是不同时间口径。
- 解决：
  - 测试时固定关卡加载状态与触发顺序。
  - 每轮显式 StartRun -> 测试 -> EndRun -> Export。
  - 对批量回归增加“对象集合一致性校验”（建议后续实现）。

## 5. 现象：state_api_seen=False，Python 组件状态始终读不到
- 原因：当前 Python 暴露层对部分 PCGComponent 运行态接口不可用/不稳定。
- 影响：纯 Python 组件状态轮询不可作为唯一结束判定依据。
- 解决：以 C++ 子系统状态为主，Python 状态仅做辅助。

## 6. 现象：为什么同一节点存在 input/output 等字段为 0
- 原因：
  - 不同节点类型并非都产生点数据或输出集合。
  - 节点某些调用路径可能是条件短路/缓存命中，单次样本为 0。
- 处理建议：
  - 结合 max/sum/avg/p95/cv 综合判断，不依赖单一 last 值。

## 当前落地状态
- 已有一键脚本：`Scripts/pcg_one_click_full_round.py`
- 已有停止脚本：`Scripts/pcg_one_click_stop.py`
- 一键流程：StartRun -> 全量 Clean/Generate -> 主线程轮询 C++ idle -> EndRun -> Export
- 新增 Tools 菜单入口（Editor）：`Tools -> PCG Profiler -> PCG Profiler One-Click Run`

## 推荐使用流程（当前版本）
1. 打开目标关卡并确认流送状态稳定。
2. 通过 Tools 菜单触发 One-Click Run。
3. 观察日志：应出现 `Finalize: stable_by_cpp_idle`。
4. 检查导出文件：`Saved/Profiling/PCG/*.json`。
5. 大规模回归时固定测试路径、相机、种子、并发配置。

## 后续建议（未实现）
- 在 JSON 中记录 run_start/run_end 的关卡与子关卡快照。
- 为批量测试增加“环境一致性门禁”（不一致则标记该轮无效）。
- 增加基线对比报告（时长/节点 p95/并行效率自动判定）。
