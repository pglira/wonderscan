# wonderscan

A Linux desktop app to mark, perspective-correct (dewarp), and export
photographed book pages into a single PDF. C++ / Qt6 / OpenCV.

## Build

Dependencies (Ubuntu 24.04): `qt6-base-dev` and `libopencv-dev`.

```bash
sudo apt install qt6-base-dev libopencv-dev    # qt6-base-dev usually already present
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

The binary is `build/wonderscan`.

## Install

Installs the binary, a `.desktop` launcher, and the app icon (hicolor theme),
so wonderscan appears in your application menu:

```bash
sudo cmake --install build
# updates to the menu/icon cache may need:
sudo update-desktop-database /usr/local/share/applications 2>/dev/null || true
gtk-update-icon-cache /usr/local/share/icons/hicolor 2>/dev/null || true
```

Use `--prefix ~/.local` for a per-user install (no `sudo`).

In VS Code, run **Terminal ▸ Run Task ▸ "wonderscan: install"** (installs to
`~/.local`, no sudo) — see `.vscode/tasks.json` for build / self-test / system
install tasks too.

## Run

```bash
./build/wonderscan            # or just `wonderscan` after installing
./build/wonderscan page1.jpg page2.jpg   # open files directly
```

### Headless self-test

Verifies the warp + PDF pipeline without a display:

```bash
QT_QPA_PLATFORM=offscreen ./build/wonderscan --selftest /tmp/out.pdf
```

## Workflow

1. **Add Images** (or **Add Folder**, or drag-drop) — JPEG / PNG / TIFF / BMP /
   WebP. iPhone HEIC must be converted to JPEG first.
2. Pick a page in the **filmstrip** (left). A green check marks pages whose
   corners are set.
3. **Rotate** the source upright if needed (`[` / `]`). *Note: rotating resets
   that image's corners.*
4. Choose **1 page** or **2 pages (6 pts)** per image (toolbar). For spreads,
   toggle **Split spread** (two pages) vs. stitched (one page).
5. Drag the **corner handles** onto the page edges. The **loupe** (top-left of
   the canvas, adjustable zoom in the toolbar) helps you place them precisely.
6. The **preview pane** (right) shows the rectified result live.
7. **Remove** an unwanted image with `Delete`, the toolbar button, or
   right-click → Remove in the filmstrip (removes it from the project only —
   the file on disk is untouched).
8. **Save Project** (`Ctrl+S`) to store your corners/rotation/mode as a `.wsp`
   JSON file you can reopen later.
9. **Export PDF** (`Ctrl+E`) — choose DPI + JPEG quality. Unmarked images are
   skipped (with a warning). Pages come out in filmstrip order.

## Keyboard

| Action            | Shortcut        |
|-------------------|-----------------|
| Open project      | `Ctrl+O`        |
| Save project      | `Ctrl+S`        |
| Export PDF        | `Ctrl+E`        |
| Rotate left/right | `[` / `]`       |
| Remove image      | `Delete`        |
| Previous / next   | `PgUp` / `PgDn` |
