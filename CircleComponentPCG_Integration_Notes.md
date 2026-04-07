# CircleComponent / PCG Integration Notes

This repo contains two separate implementation paths for "Get Circle Data":

## 1. Source-mode implementation

Location:
- `Engine/Plugins/PCG/Source/PCG/Public/Elements/PCGTypedGetter.h`
- `Engine/Plugins/PCG/Source/PCG/Private/Elements/PCGTypedGetter.cpp`

Key type:
- `UPCGGetCircleSettings`

Behavior:
- Default node name: `GetCircleData`
- Default node title: `Get Circle Data`
- Runs inside the PCG core module
- Reads `UCircleComponent` and outputs `UPCGSplineData`

This is the source-tree version.

## 2. Plugin-mode implementation

Current standalone plugin location:
- `Plugins/CircleComponentPCG`

Key type:
- `UPCGGetCircleDataSettings`

Behavior:
- Default node name: `GetCircleData_Interop`
- Default node title: `Get Circle Data`
- Runs inside the standalone plugin module
- Reads `UCircleComponent` and outputs `UPCGSplineData`

This is the standalone plugin version intended to be copied into another UE install.

For a side-by-side comparison, see:
- `CircleComponentPCG_Comparison_Table.md`

## 3. What is user-authored code

The circle feature code in this repo is user-authored / project-authored, not stock UE behavior:

Runtime component:
- `Engine/Source/Runtime/Engine/Classes/Components/CircleComponent.h`
- `Engine/Source/Runtime/Engine/Private/Components/CircleComponent.cpp`

Editor customization:
- `Engine/Source/Editor/DetailCustomizations/Private/CircleComponentDetails.h`
- `Engine/Source/Editor/DetailCustomizations/Private/CircleComponentDetails.cpp`
- `Engine/Source/Editor/DetailCustomizations/Private/DetailCustomizations.cpp`

PCG helper / tests:
- `Engine/Plugins/PCG/Source/PCG/Public/Helpers/PCGCircleHelpers.h`
- `Engine/Plugins/PCG/Source/PCG/Private/Tests/Elements/PCGGetCircleDataTest.cpp`

Standalone plugin implementation:
- `Plugins/CircleComponentPCG/Source/CircleComponentPCG/Public/Components/CircleComponent.h`
- `Plugins/CircleComponentPCG/Source/CircleComponentPCG/Private/Components/CircleComponent.cpp`
- `Plugins/CircleComponentPCG/Source/CircleComponentPCGEditor/Public/CircleComponentDetails.h`
- `Plugins/CircleComponentPCG/Source/CircleComponentPCGEditor/Private/CircleComponentDetails.cpp`
- `Plugins/CircleComponentPCG/Source/CircleComponentPCGInterop/Public/Elements/PCGGetCircleData.h`
- `Plugins/CircleComponentPCG/Source/CircleComponentPCGInterop/Private/Elements/PCGGetCircleData.cpp`
- `Plugins/CircleComponentPCG/Source/CircleComponentPCGInterop/Public/Helpers/PCGCircleHelpers.h`

The rest of the PCG typed getter flow is PCG framework code.

## 4. How to use the standalone plugin in another UE install

1. Copy `Plugins/CircleComponentPCG` into the target project or engine `Plugins` directory.
2. Enable the plugin in the target UE project.
3. Make sure `PCG` is enabled.
4. Rebuild the target project.
5. In Blueprints, add `CircleComponent` as a spawnable component.
6. In PCG Graph, use `Get Circle Data` from the plugin node set, or use the source-mode node if you kept the source-tree patch.

## 5. Important constraint

Do not enable both implementations in the same build if they define the same class names and node names.
Use one of these deployment modes:

- Source fork mode: keep the `Engine/Plugins/PCG` and runtime source-tree changes.
- Standalone plugin mode: use `Plugins/CircleComponentPCG` and do not keep the source fork active in the target build.
