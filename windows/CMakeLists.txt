find_package(Qt6 6.12 REQUIRED COMPONENTS Widgets Svg)

aux_source_directory("${CMAKE_CURRENT_SOURCE_DIR}" LNPLAYER_WINDOWS_SOURCES)
# Includes the shared Asio UI task runtime and its per-window bridges.

qt_add_executable(LNPlayerApp WIN32
    ${LNPLAYER_WINDOWS_SOURCES}
)

target_include_directories(LNPlayerApp PRIVATE "${ASIO_INCLUDE_DIR}")
target_compile_definitions(LNPlayerApp PRIVATE ASIO_STANDALONE)

target_link_libraries(LNPlayerApp
    PRIVATE
        Qt6::Widgets
        Qt6::Svg
        lnplayer
        d3d11
        dxgi
        d3dcompiler
        comctl32
)

set_target_properties(LNPlayerApp PROPERTIES
    OUTPUT_NAME "LNPlayer"
)

file(GLOB LNPLAYER_FFMPEG_DLLS CONFIGURE_DEPENDS
    "${LNPLAYER_FFMPEG_ROOT}/bin/*.dll"
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/LNPlayer.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/LNPlayer.rc"
    @ONLY
)
target_sources(LNPlayerApp PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/LNPlayer.rc")

# Deploy non-Qt runtime assets as file outputs. CMake reruns these copy
# commands only when a source runtime file changes or an output is missing.
set(LNPLAYER_UI_ASSET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ui")
get_target_property(_lnplayer_output_name lnplayer OUTPUT_NAME)
if(NOT _lnplayer_output_name)
    set(_lnplayer_output_name lnplayer)
endif()

set(_lnplayer_runtime_outputs
    "${CMAKE_CURRENT_BINARY_DIR}/ui/player_icons.svg"
    "${CMAKE_CURRENT_BINARY_DIR}/${_lnplayer_output_name}${CMAKE_SHARED_LIBRARY_SUFFIX}"
)
set(_lnplayer_runtime_copy_commands
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${CMAKE_CURRENT_BINARY_DIR}/ui"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${LNPLAYER_UI_ASSET_DIR}/player_icons.svg"
        "${CMAKE_CURRENT_BINARY_DIR}/ui/player_icons.svg"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:lnplayer>"
        "${CMAKE_CURRENT_BINARY_DIR}"
)

foreach(ffmpeg_dll IN LISTS LNPLAYER_FFMPEG_DLLS)
    get_filename_component(_ffmpeg_name "${ffmpeg_dll}" NAME)
    list(APPEND _lnplayer_runtime_outputs
        "${CMAKE_CURRENT_BINARY_DIR}/${_ffmpeg_name}"
    )
    list(APPEND _lnplayer_runtime_copy_commands
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${ffmpeg_dll}"
            "${CMAKE_CURRENT_BINARY_DIR}/${_ffmpeg_name}"
    )
endforeach()

add_custom_command(
    OUTPUT ${_lnplayer_runtime_outputs}
    ${_lnplayer_runtime_copy_commands}
    DEPENDS
        "${LNPLAYER_UI_ASSET_DIR}/player_icons.svg"
        "$<TARGET_FILE:lnplayer>"
        ${LNPLAYER_FFMPEG_DLLS}
    COMMENT "Deploying changed LNPlayer runtime assets"
    VERBATIM
)
add_custom_target(LNPlayerRuntimeAssets ALL
    DEPENDS ${_lnplayer_runtime_outputs}
)

install(TARGETS LNPlayerApp lnplayer
    RUNTIME DESTINATION .
    LIBRARY DESTINATION .
)
install(FILES "${LNPLAYER_UI_ASSET_DIR}/player_icons.svg" DESTINATION ui)

if(LNPLAYER_FFMPEG_DLLS)
    install(FILES ${LNPLAYER_FFMPEG_DLLS} DESTINATION .)
endif()

qt_generate_deploy_app_script(
    TARGET LNPlayerApp
    OUTPUT_SCRIPT LNPLAYER_QT_DEPLOY_SCRIPT
)

# Qt deployment is expensive and does not need to run after every application
# relink. Keep a stamp per configuration; rerun windeployqt only when the
# deploy script or a non-Qt runtime dependency changes.
set(_lnplayer_qt_deploy_stamp
    "${CMAKE_CURRENT_BINARY_DIR}/qt-deploy/$<CONFIG>/deploy.stamp"
)
add_custom_command(
    OUTPUT "${_lnplayer_qt_deploy_stamp}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${CMAKE_CURRENT_BINARY_DIR}/qt-deploy/$<CONFIG>"
    COMMAND "${CMAKE_COMMAND}"
        "-DQT_DEPLOY_PREFIX=$<TARGET_FILE_DIR:LNPlayerApp>"
        "-DQT_DEPLOY_BIN_DIR=."
        -P "${LNPLAYER_QT_DEPLOY_SCRIPT}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${_lnplayer_qt_deploy_stamp}"
    DEPENDS
        "${LNPLAYER_QT_DEPLOY_SCRIPT}"
        "$<TARGET_FILE:lnplayer>"
        ${LNPLAYER_FFMPEG_DLLS}
    COMMENT "Deploying Qt runtime when dependencies change"
    VERBATIM
)
add_custom_target(LNPlayerQtRuntime ALL
    DEPENDS "${_lnplayer_qt_deploy_stamp}"
)
add_dependencies(LNPlayerQtRuntime LNPlayerApp LNPlayerRuntimeAssets)

install(SCRIPT "${LNPLAYER_QT_DEPLOY_SCRIPT}")
