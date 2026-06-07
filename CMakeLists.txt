cmake_minimum_required(VERSION 3.16)
project(wonderscan LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(OpenCV REQUIRED COMPONENTS core imgproc dnn)

include(GNUInstallDirs)

qt_standard_project_setup()

qt_add_executable(wonderscan
  src/main.cpp
  src/Page.cpp
  src/Warp.cpp
  src/ImageConv.cpp
  src/AppSettings.cpp
  src/PdfExporter.cpp
  src/Project.cpp
  src/Document.cpp
  src/ImageCanvas.cpp
  src/PreviewPane.cpp
  src/PageDetector.cpp
  src/MainWindow.cpp
  resources.qrc
)

target_include_directories(wonderscan PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(wonderscan PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets ${OpenCV_LIBS})

# ---- ONNX Runtime (page-detection inference) -------------------------------
# The system OpenCV (4.6) dnn module is too old to parse a YOLO11 graph, so the
# neural-net forward pass runs on ONNX Runtime instead. Point ONNXRUNTIME_ROOT
# at an extracted release (https://github.com/microsoft/onnxruntime/releases).
set(ONNXRUNTIME_ROOT "$ENV{HOME}/.local/onnxruntime"
    CACHE PATH "ONNX Runtime root containing include/ and lib/")
if(NOT EXISTS "${ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h")
  message(FATAL_ERROR
    "ONNX Runtime not found at ONNXRUNTIME_ROOT=${ONNXRUNTIME_ROOT}. "
    "Download a release tarball and set -DONNXRUNTIME_ROOT=/path/to/onnxruntime.")
endif()
target_include_directories(wonderscan PRIVATE ${ONNXRUNTIME_ROOT}/include)
target_link_libraries(wonderscan PRIVATE ${ONNXRUNTIME_ROOT}/lib/libonnxruntime.so)
# rpath so the app finds libonnxruntime.so.1 at runtime (it lives outside the
# default loader paths).
set_target_properties(wonderscan PROPERTIES
  BUILD_RPATH "${ONNXRUNTIME_ROOT}/lib"
  INSTALL_RPATH "${ONNXRUNTIME_ROOT}/lib")

# Where the page-detection ONNX model lives: the source tree (running from the
# build dir) and the install prefix (installed app). PageDetector searches both.
target_compile_definitions(wonderscan PRIVATE
  WONDERSCAN_MODEL_DEV_PATH="${CMAKE_SOURCE_DIR}/models/page-seg.onnx"
  WONDERSCAN_MODEL_INSTALL_PATH="${CMAKE_INSTALL_FULL_DATADIR}/wonderscan/page-seg.onnx"
)

# ---- Installation (binary + desktop entry + icons) -------------------------

install(TARGETS wonderscan
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES models/page-seg.onnx
        DESTINATION ${CMAKE_INSTALL_DATADIR}/wonderscan)

install(FILES packaging/wonderscan.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

install(FILES packaging/wonderscan.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

foreach(sz 16 32 48 64 128 256 512)
  install(FILES ${CMAKE_SOURCE_DIR}/packaging/icons/${sz}x${sz}/apps/wonderscan.png
          DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${sz}x${sz}/apps)
endforeach()
