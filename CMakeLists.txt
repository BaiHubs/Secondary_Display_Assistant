cmake_minimum_required(VERSION 3.10.0)
project(SecondaryDisplayAssistant VERSION 0.1.0 LANGUAGES C)

add_executable(SecondaryDisplayAssistant WIN32
                brightness.c 
                config.c 
                display_cmd.c 
                gui.c 
                monitor.c 
                move.c 
                settings.c 
                winmover.c
                winmover.rc
                )

target_link_libraries(SecondaryDisplayAssistant 
                        dwmapi 
                        comctl32
                        )

