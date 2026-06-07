# wonderscan — notes for Claude

A Linux desktop app to mark, perspective-correct (dewarp), and export
photographed book pages into a single PDF. **C++17 / Qt6 / OpenCV**, plus ONNX
Runtime for the auto-detect feature. See `README.md` for the user-facing
build/install instructions.

## Build / run / test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release    # reconfigure after adding a source file
cmake --build build -j"$(nproc)"                  # binary: build/wonderscan

# Headless self-test — runs the Document model tests AND the warp+PDF pipeline.
# This is the regression gate; run it after every change.
QT_QPA_PLATFORM=offscreen ./build/wonderscan --selftest /tmp/out.pdf
```

The same tasks are available in VS Code (`.vscode/tasks.json`): configure,
build, self-test, and install (to `~/.local`, no sudo).

There is **no GUI test harness** — the UI in `MainWindow` is not covered by
automated tests. For UI changes, smoke-test offscreen:
`QT_QPA_PLATFORM=offscreen timeout 2 ./build/wonderscan some.jpg`.

ONNX Runtime is required to build; point `-DONNXRUNTIME_ROOT=...` at an extracted
release if it isn't at `~/.local/onnxruntime`. The detection model is
`models/page-seg.onnx`.

## Architecture (`src/`)

Layers, roughly model → processing → UI:

- **`Page`** — per-image editing state (path, rotation, 4/6 points, mode, split).
  Defines the point ordering and coordinate space (see below).
- **`Document`** — the editable document model: the ordered page list, matching
  base thumbnails, export settings (dpi/quality), project path, dirty flag.
  Owns collection ops (add/remove/move/rotate/markedCount) and load/save (via
  `ProjectIO`). Pure model, no UI — unit-tested by `runModelTests()` in
  `main.cpp`.
- **`ProjectIO`** (`Project.h/.cpp`) — JSON read/write of the `.wsp` project file.
- **`AppSettings`** — typed wrapper over `QSettings`: editor prefs (nudge/loupe/
  equal-widths), last dir, recent-projects list. The single place that knows the
  settings key names.
- **`ImageConv`** — the one `QImage` ⇄ `cv::Mat` bridge (RGB order, deep copies).
- **`Warp`** — pure dewarp functions (`applyRotation`, `renderPages`); OpenCV.
- **`PdfExporter`** — renders pages and writes a minimal image-only PDF.
- **`PageDetector`** — YOLO11-seg page detection on ONNX Runtime.
- **`ImageCanvas`** — widget: draws the source + draggable corner handles + loupe.
- **`PreviewPane`** — widget: shows the live dewarped result.
- **`MainWindow`** — the controller/view: wires the above together, owns the
  selection (`m_current`), current-image render caches, dialogs, menus, actions.
  Action enablement lives in **one** place: `syncControlsToCurrent()`.

## Conventions worth knowing

- **Coordinate space:** `Page::points` are in the *rotated* image's pixel space
  (the image the user sees and marks on). `PageDetector` returns corners in that
  same space, so detections drop straight into `Page::points`.
- **Point order:** 4-pt `[TL, TR, BR, BL]`; 6-pt spread
  `[TL, TopSpine, TR, BR, BottomSpine, BL]`.
- **Dirty flag:** `Document` mutators do *not* set it; `MainWindow::setDirty()`
  marks the document dirty *and* updates the window-modified chrome. `load()` and
  `save()` clear it.
- Match the surrounding style: explanatory comments on the *why*, anonymous-
  namespace helpers for file-local logic, `tr(...)` on user-facing strings.
