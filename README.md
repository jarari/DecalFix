# DecalFix

DecalFix is an F4SE plugin for Fallout 4 that fixes rendering problems caused by full precision vertex data on NIF meshes.

Fallout 4 supports a `Full Precision` vertex flag for meshes that need more accurate vertex positions. Some engine paths do not handle that layout correctly, which can cause blood decals, flame effects, and other model-based shader effects to render as stretched triangles, oversized particles, or misplaced geometry.

This plugin keeps full precision meshes intact for normal rendering while correcting the affected decal and effect shader paths at runtime.

## Requirements

- Fallout 4 runtime `1.10.163`
- F4SE
- Address Library/CommonLibF4-supported environment

Next-gen/AE runtime support is not enabled yet because some hook offsets still need verification.

## Building

Requirements:

- XMake `3.0.0+`
- C++23 compiler, such as MSVC or Clang-CL

Build with:

```bat
xmake build
```

The plugin DLL is generated under:

```text
build/windows/x64/releasedbg/DecalFix.dll
```

## Notes

The plugin is intended to fix engine-side handling of full precision meshes, not to modify NIF files or remove the `Full Precision` flag from assets.
