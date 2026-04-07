# PCG Circle Data（插件方式）变更说明

## 说明
本方案是**插件化实现**，通过新增独立插件 `PCGCircleInterop` 提供 `Get Circle Data` 节点，不再依赖继续修改 PCG 核心源码。

## 新增文件清单

1. `Engine/Plugins/Experimental/PCGInterops/PCGCircleInterop/PCGCircleInterop.uplugin`
2. `Engine/Plugins/Experimental/PCGInterops/PCGCircleInterop/Source/PCGCircleInterop/PCGCircleInterop.Build.cs`
3. `Engine/Plugins/Experimental/PCGInterops/PCGCircleInterop/Source/PCGCircleInterop/Public/PCGCircleInteropModule.h`
4. `Engine/Plugins/Experimental/PCGInterops/PCGCircleInterop/Source/PCGCircleInterop/Private/PCGCircleInteropModule.cpp`
5. `Engine/Plugins/Experimental/PCGInterops/PCGCircleInterop/Source/PCGCircleInterop/Public/Elements/PCGGetCircleData.h`
6. `Engine/Plugins/Experimental/PCGInterops/PCGCircleInterop/Source/PCGCircleInterop/Private/Elements/PCGGetCircleData.cpp`

## 关键实现点

## 1) 插件模块
- 新模块名：`PCGCircleInterop`
- 模块类型：`Runtime`
- 依赖模块：`Core`, `CoreUObject`, `Engine`, `PCG`

## 2) 节点定义
- 设置类：`UPCGGetCircleDataSettings`（继承 `UPCGGetSplineSettings`）
- 执行类：`FPCGGetCircleInteropElement`（继承 `FPCGDataFromActorElement`）
- 节点标题：`Get Circle Data`
- 节点名：`GetCircleData_Interop`

## 3) Circle 组件适配逻辑
- 默认 `ComponentSelector` 过滤为 `UCircleComponent`
- 采样 API：
  - `GetLocationAtScaledAngle(...)`
  - `GetTangentAtScaledAngle(...)`
- 将采样点组装为 `FSplinePoint`，输出 `UPCGSplineData`
- 输出类型为 `EPCGDataType::PolyLine`
- 继承 Actor/Component tags 到输出

## 4) 冲突规避
- 执行类命名为 `FPCGGetCircleInteropElement`，避免与源码内置方式中的同名类冲突。

## 构建验证
- 已执行并通过：
  - `Engine/Build/BatchFiles/Build.bat UnrealEditor Win64 Development -Module=PCGCircleInterop -NoHotReloadFromIDE`
  - `Engine/Build/BatchFiles/Build.bat UnrealEditor Win64 Development -NoHotReloadFromIDE`
- 结果：`Result: Succeeded`

