# CircleComponentPCG

Standalone plugin package for:

- `CircleComponent` runtime component
- Details panel customization for TA use
- PCG `Get Circle Data` integration
- Blueprint spawnable `CircleComponent`
- Standalone `Get Circle Data` node for PCG Graph

## Enable order

1. Copy this folder into the target UE project's `Plugins` directory.
2. Enable `PCG`.
3. Enable `CircleComponentPCG`.
4. Rebuild the project.

## Notes

- Add `CircleComponent` in a Blueprint via the normal `Add Component` flow.
- The PCG node reads `UCircleComponent` and emits analytic circle data that stays compatible with `PolyLine` consumers.
- The plugin node title is `Get Circle Data`; the internal node name is `GetCircleData_Interop`.
- Do not enable this plugin together with a source-fork build that defines the same `CircleComponent` symbols.
