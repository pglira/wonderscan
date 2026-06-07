# Page-detection model

The **Auto-detect Corners** feature loads `page-seg.onnx` from this directory
(a YOLO11-seg model that finds book-page outlines). The file is **not committed**
to git — it is distributed as a **GitHub Release asset**.

## Get it

Download `page-seg.onnx` from the
[latest release](https://github.com/pglira/wonderscan/releases) and drop it here:

```bash
curl -fsSL -o models/page-seg.onnx \
  https://github.com/pglira/wonderscan/releases/latest/download/page-seg.onnx
```

Then build as usual; the app finds it at `models/page-seg.onnx` (and `cmake
--install` ships it to `share/wonderscan/page-seg.onnx`).

## License

The model was trained and exported with **Ultralytics YOLO11**, which is
**AGPL-3.0**. The model weights are therefore licensed **AGPL-3.0**, separate
from the MIT license of the wonderscan application code. If you redistribute the
model (or an app that bundles it), comply with AGPL-3.0 or obtain an Ultralytics
Enterprise license.

## Retrain / re-export

See the training project's `README.md` (in the wonderscan data folder) for how to
rebuild the dataset, retrain, export a new `page-seg.onnx`, and deploy it.
