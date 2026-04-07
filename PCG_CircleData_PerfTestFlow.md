# UE 5.7.3 PCG CircleComponent 性能测试流程

本文档说明这套 CircleComponent / PCG 性能测试是怎么跑起来的、用了哪些指令、每条指令的作用，以及实际测了哪些场景。

## 目标
比较三条路径的成本：
- core 内置实现
- plugin 互操作实现
- 直接 helper 转换基线

同时验证：
- 自动化测试可发现
- CircleComponent 到 PCG 输出链路可用
- 在异常情况下不会长时间空转

## 前置条件
测试能被发现的前提：
- [Engine/Plugins/PCG/PCG.uplugin](/F:/UnrealEngine-5.7.3-release/Engine/Plugins/PCG/PCG.uplugin) 需要可加载
- [Engine/Plugins/Experimental/PCGInterops/PCGCircleInterop/PCGCircleInterop.uplugin](/F:/UnrealEngine-5.7.3-release/Engine/Plugins/Experimental/PCGInterops/PCGCircleInterop/PCGCircleInterop.uplugin) 需要可加载
- [Engine/Config/BaseEngine.ini](/F:/UnrealEngine-5.7.3-release/Engine/Config/BaseEngine.ini) 需要把 `PCG`、`PCGEditor`、`PCGCircleInterop` 加到 `EditorTestModules`

这些配置的作用是让 headless automation 在启动后主动加载相关模块，否则测试会出现“列表里找不到”的情况。

## 测试代码入口
测试实现位于：
- [Engine/Plugins/PCG/Source/PCG/Private/Tests/Elements/PCGGetCircleDataTest.cpp](/F:/UnrealEngine-5.7.3-release/Engine/Plugins/PCG/Source/PCG/Private/Tests/Elements/PCGGetCircleDataTest.cpp)

当前包含两个自动化测试：
- `Plugins.PCG.CircleData.Correctness`
- `Plugins.PCG.CircleData.PerfLog`

## 使用到的指令

### 1. 编译
```powershell
Engine\Build\BatchFiles\Build.bat UnrealEditor Win64 Development -NoHotReloadFromIDE
```

作用：
- 重新编译 `UnrealEditor`
- 确保 PCG、PCGCircleInterop、测试代码都进入当前二进制
- 避免 Hot Reload 造成的注册不一致

### 2. 查看自动化测试列表
```powershell
& Engine\Binaries\Win64\UnrealEditor-Cmd.exe -Unattended -NoSplash -NullRHI -ExecCmds="Automation List;Quit" -AbsLog="F:\UnrealEngine-5.7.3-release\CircleDataList.log"
```

作用：
- 启动无界面 editor
- 列出当前 session 中可发现的 automation tests
- 用来确认 `Plugins.PCG.CircleData.*` 是否真的被注册和加载

关键结果：
- 先前未启用模块时，列表里找不到 `Plugins.PCG.CircleData.*`
- 修正模块加载后，列表能看到：
  - `Plugins.PCG.CircleData.Correctness`
  - `Plugins.PCG.CircleData.PerfLog`

### 3. 运行 CircleData 测试
```powershell
& Engine\Binaries\Win64\UnrealEditor-Cmd.exe -Unattended -NoSplash -NullRHI -ExecCmds="Automation RunTests Plugins.PCG.CircleData;Quit" -testexit="Automation Test Queue Empty" -AbsLog="F:\UnrealEngine-5.7.3-release\CircleDataRun2.log"
```

作用：
- 运行所有匹配 `Plugins.PCG.CircleData` 前缀的测试
- `Quit` 表示测试队列结束后退出
- `-testexit="Automation Test Queue Empty"` 表示队列空了就结束进程
- `-AbsLog=...` 指定完整日志路径，方便后续抓取性能输出和结果

## 每个参数的作用
- `-Unattended`：无人值守模式，不弹交互式对话框
- `-NoSplash`：不显示启动 splash
- `-NullRHI`：不初始化图形渲染，降低 headless 测试成本
- `-ExecCmds="..."`：给 editor 注入 automation 控制命令
- `Automation List`：列出可发现测试
- `Automation RunTests <pattern>`：按名字模式执行测试
- `Quit`：测试结束后退出 editor
- `-testexit="Automation Test Queue Empty"`：自动化队列为空时退出
- `-AbsLog="..."`：指定绝对日志文件，便于固定抓取结果

## 测试场景

### 场景 1：Correctness
测试名：
- `Plugins.PCG.CircleData.Correctness`

覆盖内容：
- core 节点输出是否可执行
- plugin 节点输出是否可执行
- helper 转换是否成功
- `SplineData` 点数是否一致
- `ClosedLoop` 是否保持闭环

目的：
- 验证 core / plugin / helper 的结果一致性
- 确认 CircleComponent 的圆数据可以稳定进入 PCG 输出链路

### 场景 2：PerfLog
测试名：
- `Plugins.PCG.CircleData.PerfLog`

覆盖内容：
- core 路径执行耗时
- plugin 路径执行耗时
- helper 纯转换耗时

测试配置：
- 先 warmup 4 次
- 再 measurement 16 次
- 对每条路径都加了最大执行步数上限，避免异常长跑

目的：
- 比较三条路径的相对成本
- 用日志形成可复述、可展示的性能数据

## 当前实测结果
最近一次成功跑通的性能日志为：

```text
core=9.793 ms total (612.050 us/iter) | plugin=10.021 ms total (626.306 us/iter) | helper=0.074 ms total (4.612 us/iter) | iter=16
```

结论：
- helper 最快，因为它只做几何转换
- core 和 plugin 很接近，说明框架层成本远高于纯转换成本
- 测试本身很快，真正耗时主要是 editor 启动和 automation 初始化

## 产物日志
- 列表日志：[CircleDataList3.log](/F:/UnrealEngine-5.7.3-release/CircleDataList3.log)
- 运行日志：[CircleDataRun2.log](/F:/UnrealEngine-5.7.3-release/CircleDataRun2.log)

## 复跑建议
如果你要重复验证，按这个顺序走：
1. 先执行编译
2. 再执行 `Automation List`
3. 确认 `Plugins.PCG.CircleData.*` 能被发现
4. 再执行 `Automation RunTests Plugins.PCG.CircleData`
5. 读取 `-AbsLog` 里的结果和性能数据

## 注意点
- 如果没有加载 `PCG` 相关模块，测试名会匹配不到。
- 如果没有 `MaxElementExecuteSteps` 这类上限，异常情况下可能出现测试长时间空转。
- `helper` 结果只能作为纯转换基线，不能替代 PCG element / data pipeline 的真实成本。
