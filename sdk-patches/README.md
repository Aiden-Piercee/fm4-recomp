# SDK patches

`tools/Build-Local.ps1` applies every `*.patch` in this folder, in name order,
on top of the pinned SDK commit from `release/release-policy.json`. Any other
modification of the SDK checkout is still refused, and the build receipt
records the hash of each applied patch.

Every patch keeps existing behavior by default: new code paths sit behind
runtime settings that are off unless enabled. The validated profile in
[`docs/STATUS.md`](../docs/STATUS.md) stays the correctness reference.

To rebuild only the patched GPU plugin into an existing local build:

```powershell
pwsh -File .\tools\Build-Local.ps1 -GameDataRoot "D:\Games\FM4-extracted" -SdkOnly
```

## Series

- `0001-d3d12-deferred-readback-sync.patch`: `d3d12_readback_resolve_deferred_sync` waits for resolve readback once per guest-visible sync point instead of after every resolve.
