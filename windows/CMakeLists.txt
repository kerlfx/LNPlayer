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

# Keep player UI artwork external so designers can iterate without rebuilding.
set(LNPLAYER_UI_ASSET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources")
add_custom_command(TARGET LNPlayerApp POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "$<TARGET_FILE_DIR:LNPlayerApp>/ui"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${LNPLAYER_UI_ASSET_DIR}/player_icons.svg"
        "$<TARGET_FILE_DIR:LNPlayerApp>/ui/player_icons.svg"
    COMMENT "Deploying external player UI artwork"
    VERBATIM
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/LNPlayer.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/LNPlayer.rc"
    @ONLY
)
target_sources(LNPlayerApp PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/LNPlayer.rc")

file(GLOB LNPLAYER_FFMPEG_DLLS CONFIGURE_DEPENDS
    "${LNPLAYER_FFMPEG_ROOT}/bin/*.dll"
)

# lnplayer is a SHARED library. Linking it creates an import-library
# dependency, but does not place its runtime DLL beside LNPlayer.exe in the
# build tree. Copy it explicitly after linking.
add_custom_command(TARGET LNPlayerApp POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:lnplayer>"
        "$<TARGET_FILE_DIR:LNPlayerApp>"
    COMMENT "Copying LNPlayer Core runtime"
    VERBATIM
)

foreach(ffmpeg_dll IN LISTS LNPLAYER_FFMPEG_DLLS)
    add_custom_command(TARGET LNPlayerApp POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${ffmpeg_dll}"
            "$<TARGET_FILE_DIR:LNPlayerApp>"
        VERBATIM
    )
endforeach()

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

# Deploy Qt into a staging directory first, then update the runnable output
# directory with copy-if-different so unchanged runtime files are not recopied.
# The staging directory must contain every non-system runtime dependency of
# LNPlayer.exe, not just the executable, so Qt's dependency scanner sees the
# same dependency graph as the final application directory.
set(LNPLAYER_QT_DEPLOY_STAGING_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/qt-deploy/$<CONFIG>"
)

set(_lnplayer_qt_stage_copy_commands)
list(APPEND _lnplayer_qt_stage_copy_commands
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:lnplayer>"
        "${LNPLAYER_QT_DEPLOY_STAGING_DIR}"
)

foreach(ffmpeg_dll IN LISTS LNPLAYER_FFMPEG_DLLS)
    list(APPEND _lnplayer_qt_stage_copy_commands
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${ffmpeg_dll}"
            "${LNPLAYER_QT_DEPLOY_STAGING_DIR}"
    )
endforeach()

add_custom_command(TARGET LNPlayerApp POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${LNPLAYER_QT_DEPLOY_STAGING_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:LNPlayerApp>"
        "${LNPLAYER_QT_DEPLOY_STAGING_DIR}"
    ${_lnplayer_qt_stage_copy_commands}
    COMMAND "${CMAKE_COMMAND}"
        "-DQT_DEPLOY_PREFIX=${LNPLAYER_QT_DEPLOY_STAGING_DIR}"
        "-DQT_DEPLOY_BIN_DIR=."
        -P "${LNPLAYER_QT_DEPLOY_SCRIPT}"
    COMMAND "${CMAKE_COMMAND}"
        "-DLNPLAYER_DEPLOY_SOURCE=${LNPLAYER_QT_DEPLOY_STAGING_DIR}"
        "-DLNPLAYER_DEPLOY_DESTINATION=$<TARGET_FILE_DIR:LNPlayerApp>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/CopyIfDifferent.cmake"
    COMMENT "Deploying Qt runtime (copying only changed files)"
    VERBATIM
)

install(SCRIPT "${LNPLAYER_QT_DEPLOY_SCRIPT}")
