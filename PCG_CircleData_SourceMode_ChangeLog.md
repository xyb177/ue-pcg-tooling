# PCG Circle Data（源码内置方式）变更说明

## 说明
本方案是**直接修改 PCG 源码**，在 PCG 内置节点体系中新增 `Get Circle Data` 能力。

## 新增/修改文件清单

### 修改文件
1. `Engine/Plugins/PCG/Source/PCG/Public/Elements/PCGTypedGetter.h`
2. `Engine/Plugins/PCG/Source/PCG/Private/Elements/PCGTypedGetter.cpp`
3. `Engine/Source/Runtime/Engine/Classes/Components/CircleComponent.h`

### 删除文件
1. `Engine/Plugins/PCG/Source/PCG/Public/Elements/PCGGetCircleData.h`

## 关键改动内容

## 1) 在 `PCGTypedGetter` 中新增 Circle 节点类型
- 在 `PCGTypedGetter.h` 中新增：
  - `UPCGGetCircleSettings`（设置类）
  - `FPCGGetCircleDataElement`（执行类）
- 节点显示名为 `Get Circle Data`。
- `GetDataFilter()` 设置为 `EPCGDataType::PolyLine`，与 spline/curve 数据链路兼容。

## 2) 在 `PCGTypedGetter.cpp` 中实现 Circle -> Spline 数据转换
- 默认将 `ComponentSelector` 固定为 `UCircleComponent`。
- 从 `CircleComponent` 采样得到控制点：
  - 使用 `GetLocationAtScaledAngle(...)`
  - 使用 `GetTangentAtScaledAngle(...)`
- 支持闭环/非闭环（受 `IsClosedLoop` 和 `FullAngle` 影响）。
- 生成 `UPCGSplineData` 输出到默认 `Out` pin。
- 继承 Actor/Component Tags 到输出 `TaggedData`。

## 3) 修复 `CircleComponent.h` 的导出声明冲突
- `UCircleComponent` 类本身已是 `ENGINE_API`，其成员函数不应重复加 `ENGINE_API`。
- 清理重复导出，解决 UHT/C2487 相关编译错误。

## 4) 删除旧的半成品头文件
- 删除 `Engine/Plugins/PCG/Source/PCG/Public/Elements/PCGGetCircleData.h`（旧文件仅有声明、无完整实现，且会导致类型冲突/构建干扰）。

## 构建验证
- 已执行并通过：
  - `Engine/Build/BatchFiles/Build.bat UnrealEditor Win64 Development -NoHotReloadFromIDE`
- 结果：`Result: Succeeded`

