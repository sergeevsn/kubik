cmake_minimum_required(VERSION 3.18)
project(kubik LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(KUBIK_ENABLE_OPENMP "OpenMP для чтения срезов" ON)

if(KUBIK_ENABLE_OPENMP)
    find_package(OpenMP QUIET)
endif()

# --- segyio (только C API) ---
include(FetchContent)
set(BUILD_PYTHON OFF CACHE BOOL "" FORCE)
set(BUILD_BIN OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_MEX OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    segyio
    GIT_REPOSITORY https://github.com/equinor/segyio.git
    GIT_TAG v2.0.0-alpha.1
)
FetchContent_MakeAvailable(segyio)

# --- KFR (FFT) ---
set(KFR_ENABLE_DFT ON CACHE BOOL "" FORCE)
set(KFR_ENABLE_MULTIARCH OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
# 32-bit MinGW без -msse2: автодетект KFR возвращает "-"
if(WIN32)
    set(KFR_ARCH sse2 CACHE STRING "" FORCE)
endif()
FetchContent_Declare(
    kfr
    GIT_REPOSITORY https://github.com/kfrlib/kfr.git
    GIT_TAG 6.2.0
)
FetchContent_MakeAvailable(kfr)

# --- Qt5 ---
find_package(Qt5 REQUIRED COMPONENTS Widgets)

set(CMAKE_AUTOMOC ON)

add_library(kubik_core STATIC
    src/segy/SegyCube.cpp
    src/segy/Resample.cpp
    src/dsp/FftFilter.cpp
    src/dsp/FftFilter2D.cpp
)
target_include_directories(kubik_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_definitions(kubik_core PUBLIC KUBIK_USE_KFR=1)
target_link_libraries(kubik_core PUBLIC segyio kfr kfr_dft)
if(KUBIK_ENABLE_OPENMP AND OpenMP_CXX_FOUND)
    target_link_libraries(kubik_core PUBLIC OpenMP::OpenMP_CXX)
endif()

add_executable(kubik
    src/main.cpp
    src/gui/MainWindow.cpp
    src/gui/DiscreteValueSpinBox.cpp
    src/gui/SegyHeaderDialog.cpp
    src/gui/SurveyCoordinatesDialog.cpp
    src/gui/SurveyMapWidget.cpp
    src/gui/SliceView.cpp
    src/gui/CubeNavigator.cpp
    src/gui/CollapsibleGroupBox.cpp
    src/gui/ColorSchemes.cpp
    src/gui/SpectrumPlotWidget.cpp
    src/gui/SlicePreviewWidget.cpp
    src/gui/FftFilterDialog.cpp
    src/gui/FftFilter2DDialog.cpp
    src/gui/Spectrum2DPlotWidget.cpp
)
target_link_libraries(kubik PRIVATE kubik_core Qt5::Widgets)
if(KUBIK_ENABLE_OPENMP AND OpenMP_CXX_FOUND)
    target_link_libraries(kubik PRIVATE OpenMP::OpenMP_CXX)
endif()

install(TARGETS kubik DESTINATION bin)

enable_testing()
add_executable(kubik_segy_test tests/segy_open_test.cpp)
target_link_libraries(kubik_segy_test PRIVATE kubik_core)
add_test(NAME segy_open_test COMMAND kubik_segy_test
    "${CMAKE_SOURCE_DIR}/data/both_stime_25x5.sgy")
