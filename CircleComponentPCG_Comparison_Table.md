# CircleComponent / PCG Version Comparison

This document separates the three states that have existed in this repo:

- Source fork version: code directly under `Engine/...`
- Engine-tree plugin version: plugin code still living under `Engine/Plugins/...`
- Standalone plugin version: copyable plugin under `Plugins/CircleComponentPCG`

## Quick Summary

| Item | Source fork version | Engine-tree plugin version | Standalone plugin version |
| --- | --- | --- | --- |
| Intended use | Patch the engine tree directly | Validate plugin-style packaging inside the engine repo | Ship to other UE installs |
| Can be used without engine source changes | No | No | Yes |
| `CircleComponent` location | `Engine/Source/...` | `Engine/Plugins/...` | `Plugins/CircleComponentPCG/...` |
| `Get Circle Data` location | `Engine/Plugins/PCG/...` source fork path | `Engine/Plugins/Experimental/PCGInterops/PCGCircleInterop/...` | `Plugins/CircleComponentPCG/Source/CircleComponentPCGInterop/...` |
| Output data model | `UPCGSplineData` in the source-fork implementation | `UPCGSplineData` in the engine-tree plugin implementation | `UPCGCircleData` in the standalone plugin implementation |
| Geometry model | Segment-based approximation | Segment-based approximation | Analytic circle parameters, not segment-first |
| PCG pin type | `PolyLine` | `PolyLine` | `PolyLine` for compatibility |
| Blueprint support | Yes, but tied to source fork | Yes, but tied to engine tree plugin build | Yes, copy plugin into another project and enable it |
| Details customization | Engine editor customization | Engine editor customization | Plugin editor module customization |
| Main risk | Hard to move across UE installs | Still bound to this engine tree | Must avoid loading alongside a conflicting source-fork build |

## What each version actually means

### 1. Source fork version

This is the version that modifies the UE tree directly.

Typical files:
- `Engine/Source/Runtime/Engine/Classes/Components/CircleComponent.h`
- `Engine/Source/Runtime/Engine/Private/Components/CircleComponent.cpp`
- `Engine/Source/Editor/DetailCustomizations/Private/CircleComponentDetails.h`
- `Engine/Source/Editor/DetailCustomizations/Private/CircleComponentDetails.cpp`
- `Engine/Plugins/PCG/Source/PCG/Public/Helpers/PCGCircleHelpers.h`
- `Engine/Plugins/PCG/Source/PCG/Private/Tests/Elements/PCGGetCircleDataTest.cpp`
- `Engine/Plugins/Experimental/PCGInterops/PCGCircleInterop/...`

This version is only portable if you move the engine fork with it.

### 2. Engine-tree plugin version

This is the plugin-style implementation that still exists under the UE source tree.

Typical files:
- `Engine/Plugins/Experimental/PCGInterops/PCGCircleInterop/...`

This is useful as a reference implementation, but it is still tied to this exact engine checkout.

### 3. Standalone plugin version

This is the copyable plugin intended for other UE installs.

Typical files:
- `Plugins/CircleComponentPCG/CircleComponentPCG.uplugin`
- `Plugins/CircleComponentPCG/Source/CircleComponentPCG/...`
- `Plugins/CircleComponentPCG/Source/CircleComponentPCGEditor/...`
- `Plugins/CircleComponentPCG/Source/CircleComponentPCGInterop/...`

This is the version to copy into another project’s `Plugins` folder.

## `Get Circle Data` ownership

| Build path | `Get Circle Data` belongs to | Notes |
| --- | --- | --- |
| Source fork version | `Engine/Plugins/PCG/Source/PCG/...` | Core PCG node path, source-tree patch |
| Engine-tree plugin version | `Engine/Plugins/Experimental/PCGInterops/PCGCircleInterop/...` | Plugin-style node inside engine tree |
| Standalone plugin version | `Plugins/CircleComponentPCG/Source/CircleComponentPCGInterop/...` | Copyable plugin node |

## `CircleComponent` behavior comparison

| Behavior | Source fork version | Standalone plugin version |
| --- | --- | --- |
| `Scale` affects circle geometry | No | No |
| `FullAngle` display in editor | Degrees | Degrees |
| `Thickness` renders visible thickness | Yes | Yes |
| `ShapeType` supports `Wire`, `Solid`, `ThickWire` | Yes | Yes |
| Circle data can be treated as analytic | Not as the final output model | Yes, via `UPCGCircleData` |

## Safe deployment rule

Use exactly one deployment mode in a given build:

1. Source fork mode
2. Standalone plugin mode

Do not enable both in the same build if they define overlapping `CircleComponent` symbols or the same PCG node semantics.

## Current recommended path

If the goal is portability to another UE install:

1. Keep the standalone plugin.
2. Treat the source fork version as reference only.
3. Prefer the standalone plugin for new work.

