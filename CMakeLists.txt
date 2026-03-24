cmake_minimum_required(VERSION 3.18)

project(UltrasoundHostApp VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# nvcc is not on PATH — point CMake at it directly
if(NOT DEFINED CMAKE_CUDA_COMPILER)
    set(CMAKE_CUDA_COMPILER "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.9/bin/nvcc.exe")
endif()

# Optional CUDA — check AFTER setting host compiler
include(CheckLanguage)
check_language(CUDA)
if(CMAKE_CUDA_COMPILER)
    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    enable_language(CUDA)
    find_package(CUDAToolkit REQUIRED)
    set(CUDA_AVAILABLE TRUE)
    message(STATUS "CUDA found: ${CMAKE_CUDA_COMPILER} (host: ${CMAKE_CUDA_HOST_COMPILER})")
else()
    set(CUDA_AVAILABLE FALSE)
    message(STATUS "CUDA not found — building CPU-only")
endif()

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network Multimedia)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network Multimedia)

set(PROJECT_SOURCES
        main.cpp
        gui/mainwindow.cpp
        gui/mainwindow.h
        gui/mainwindow.ui

        gui/OscilloscopeWorker.h
        gui/OscilloscopeWorker.cpp

        network/IDataProvider.h
        network/IDataProvider.cpp

        network/TCPDataProvider.h
        network/TCPDataProvider.cpp

        network/UDPDataProvider.h
        network/UDPDataProvider.cpp

        network/SimulatedDataProvider.h
        network/SimulatedDataProvider.cpp

        network/TCPControl.h
        network/TCPControl.cpp

        model/PacketDecoder.h
        model/PacketDecoder.cpp
        model/MicrophonePacket.h
        
        model/MicrophoneArray.h
        model/MicrophoneArray.cpp
        
        model/BeamformingCalculator.h
        model/BeamformingCalculator.cpp
        
        storage/MatFileWriter.h
        storage/MatFileWriter.cpp
        
        storage/DataRecorder.h
        storage/DataRecorder.cpp
        
        model/BeamformerWorker.h
        model/BeamformerWorker.cpp

        audio/AudioOutput.h
        audio/AudioOutput.cpp

        visualization/OscilloscopeWidget.h
        visualization/OscilloscopeWidget.cpp
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(UltrasoundHostApp
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET UltrasoundHostApp APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(UltrasoundHostApp SHARED
            ${PROJECT_SOURCES}
        )
    else()
        add_executable(UltrasoundHostApp
            ${PROJECT_SOURCES}
        )
    endif()
endif()

if(CUDA_AVAILABLE)
    target_sources(UltrasoundHostApp PRIVATE
        model/BeamformerKernelLauncher.h
        model/BeamformerWorkerCUDA.h
        model/BeamformerWorkerCUDA.cpp
        model/BeamformerWorkerCUDA.cu
    )
    target_compile_definitions(UltrasoundHostApp PRIVATE BEAMFORMER_CUDA_AVAILABLE)
    set_target_properties(UltrasoundHostApp PROPERTIES
        CUDA_SEPARABLE_COMPILATION ON
    )
    # MSVC requires /Zc:__cplusplus so Qt headers see the correct C++ version
    if(MSVC)
        target_compile_options(UltrasoundHostApp PRIVATE
            $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/Zc:__cplusplus>
        )
    endif()
    target_link_libraries(UltrasoundHostApp PRIVATE CUDA::cudart)
endif()

target_include_directories(UltrasoundHostApp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/visualization
)

# Pass source directory so the app can find assets (e.g. microphoneLocations.csv) at runtime
target_compile_definitions(UltrasoundHostApp PRIVATE
    APP_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(UltrasoundHostApp PRIVATE 
    Qt${QT_VERSION_MAJOR}::Widgets 
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Multimedia
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.UltrasoundHostApp)
endif()
set_target_properties(UltrasoundHostApp PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS UltrasoundHostApp
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(UltrasoundHostApp)
endif()
