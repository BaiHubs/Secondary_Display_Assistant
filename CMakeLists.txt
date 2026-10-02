cmake_minimum_required(VERSION 3.10.0)
project(SecondaryDisplayAssistant VERSION 0.1.0 LANGUAGES C)

add_executable(SecondaryDisplayAssistant WIN32
                src/main.c
                src/wndproc.c
                src/hotkeys.c
                src/shellhook.c
                src/window_filter.c
                src/switcher.c
                src/tray.c
                src/config.c
                src/monitor.c
                src/move.c
                src/brightness.c
                src/display_cmd.c
                src/settings.c
                res/winmover.rc
                )

target_include_directories(SecondaryDisplayAssistant PRIVATE 
                        src
                        res
                        core-ui/include
                        )

# core-ui 是 MSVC 预编译库（MSVC 的 .lib 不能被 MinGW 直接链接），
# 这里用 gendef + dlltool 从 core-ui.dll 生成 MinGW 导入库再链接。
set(COREUI_DIR       "${CMAKE_CURRENT_SOURCE_DIR}/core-ui")
set(COREUI_DLL       "${COREUI_DIR}/lib/dynamic/core-ui.dll")
set(COREUI_MINGW_DIR "${COREUI_DIR}/lib/mingw")
set(COREUI_IMPLIB    "${COREUI_MINGW_DIR}/libcore-ui.a")

add_custom_command(
    OUTPUT  "${COREUI_IMPLIB}"
    COMMAND gendef "${COREUI_DLL}"
    COMMAND dlltool -d core-ui.def -D core-ui.dll -l libcore-ui.a
    DEPENDS "${COREUI_DLL}"
    WORKING_DIRECTORY "${COREUI_MINGW_DIR}"
    COMMENT "Generating core-ui MinGW import library"
    VERBATIM)

add_custom_target(coreui_implib DEPENDS "${COREUI_IMPLIB}")
add_dependencies(SecondaryDisplayAssistant coreui_implib)

target_link_libraries(SecondaryDisplayAssistant 
                        dwmapi 
                        comctl32
                        shcore
                        "${COREUI_IMPLIB}"
                        )

# 运行时把 core-ui.dll 与 settings.uix 拷到 exe 同目录
add_custom_command(TARGET SecondaryDisplayAssistant POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${COREUI_DLL}"
            "$<TARGET_FILE_DIR:SecondaryDisplayAssistant>/core-ui.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/src/settings.uix"
            "$<TARGET_FILE_DIR:SecondaryDisplayAssistant>/settings.uix"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/src/switcher.uix"
            "$<TARGET_FILE_DIR:SecondaryDisplayAssistant>/switcher.uix"
    COMMENT "Copying core-ui.dll + settings.uix next to the executable")
