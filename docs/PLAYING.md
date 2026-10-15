# FM4 Recompiled for Windows x64

Extract the complete win-x64 player ZIP into a local folder, such as `D:\Games\FM4-Recompiled`.
No compiler, SDK installation or PowerShell is required to play.
Use Windows 11 x64, an AVX2-capable CPU and a Direct3D 12 GPU; other systems are untested.

Copy the contents of your own extracted FM4 Europe Game of the Year Disc 1 into a `game` folder beside `fm4.exe`.
The complete game tree is required, including `Media` and these three files directly inside `game`:

- `default.xex`: SHA-256 `B34DFB88978819CCCDA6DAFD755E7139970A58FBF6707DFBCBA06846857F52BD`
- `XMediaFacade_default.xex`: SHA-256 `2E82A8D08A59E79F4D68E68B744C2872855BFDD583EB39AD0A9C54E5B3FA1492`
- `SpeechFacade_default.xex`: SHA-256 `49D506A2F8556836943D8BAB3A50B6CD3877524500509EF462FB6E9802F2F4BF`

Double-click `Launch FM4.bat`. Saves and caches stay in the package's `user` folder.
Keep that folder when updating. The player ZIP contains no game files, DLC, Xbox system files or saves.
For optional Disc 2 content, put your own four FM4 packages in `disc2-content` beside `fm4.exe`; the game validates and installs them at startup.

The default is native 1x, D3D12 ROV and full synchronous readback.
Stable 60 FPS is not achieved; heavy scenes can run around 30 FPS, and fresh profiles can cache blank car thumbnails.
The binary package has build/dependency checks, not a new full gameplay qualification.
See [current status](https://github.com/Alexbeav/fm4-recomp/blob/main/docs/STATUS.md).

Project source: [alpha.2](https://github.com/Alexbeav/fm4-recomp/tree/v0.1.0-alpha.2).
SDK source: [f16992c](https://github.com/Alexbeav/rexglue-sdk/tree/f16992c92e610c2d71773994436b442d9946f4e1).
Packaging scripts and current instructions are in the [repository](https://github.com/Alexbeav/fm4-recomp).
Licenses and third-party notices are included beside the executable.
