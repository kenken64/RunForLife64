# RunForLife64

An original Nintendo 64 homebrew endless runner. It uses the lane-dodge rhythm of modern runner games, but all code, title, gameplay tuning, and low-poly projected 3D visuals are original.

## Current Scope

- Three-lane runner
- RDPQ hardware-rasterized 3D road, city, player, obstacles, buses, and coins
- On-screen FPS counter
- D-pad or analog lane switching
- Jump over barriers with A/C-Up/Up
- Slide under gates with B/Z
- Squat under gates by holding Down/C-Down or pulling the stick down
- Avoid full blocks by changing lane
- Jump onto bus obstacles to collect elevated coin trails
- Random jetpack and invisibility powerups
- Randomized GLB-derived side-city building meshes
- GLB-derived Back to the Future player mesh
- Coins, distance score, level progression, controlled speed ramp, title screen, and game-over restart

## Build

This workspace has a project-local libdragon install under `.tools/`.
Build from an MSYS2 shell or through the project-local MSYS2 bash:

```sh
make
```

Expected output:

```text
runforlife64.z64
```

The build pads the ROM to 1 MiB and marks it as NTSC-U for better Project64
compatibility.

## GLB Models

Put source `.glb` model files in:

```text
assets/models/
```

The build converts:

- `assets/models/building*.glb` into side-city building meshes
- `assets/models/backtofuture.glb` into the player mesh

After changing the GLB files, regenerate the mesh C data:

```powershell
python tools\glb_to_rfl_mesh.py
```

The normal `make` build also regenerates the mesh data before compiling it into
the ROM.

If you need to run it from PowerShell using the project-local portable MSYS2 install:

```powershell
.\.tools\msys64\usr\bin\bash.exe -lc "cd /d/Projects/RunForLife64 && make"
```

The generated ROM is:

```text
runforlife64.z64
```
