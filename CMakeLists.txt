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
find_package(OpenCV REQUIRED COMPONENTS core imgproc)

qt_standard_project_setup()

qt_add_executable(wonderscan
  src/main.cpp
  src/Page.cpp
  src/Warp.cpp
  src/PdfExporter.cpp
  src/Project.cpp
  src/ImageCanvas.cpp
  src/PreviewPane.cpp
  src/MainWindow.cpp
  resources.qrc
)

target_include_directories(wonderscan PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(wonderscan PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets ${OpenCV_LIBS})

# ---- Installation (binary + desktop entry + icons) -------------------------
include(GNUInstallDirs)

install(TARGETS wonderscan
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES packaging/wonderscan.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

install(FILES packaging/wonderscan.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

foreach(sz 16 32 48 64 128 256 512)
  install(FILES ${CMAKE_SOURCE_DIR}/packaging/icons/${sz}x${sz}/apps/wonderscan.png
          DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${sz}x${sz}/apps)
endforeach()
