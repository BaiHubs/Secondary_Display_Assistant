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
                        )

target_link_libraries(SecondaryDisplayAssistant 
                        dwmapi 
                        comctl32
                        )

