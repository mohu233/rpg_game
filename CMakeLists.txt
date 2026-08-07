cmake_minimum_required(VERSION 3.20)

project(FreeWalkRpgDemo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(RPG_SHARED_SOURCES
    src/scene.cpp
    src/scene_render.cpp
    src/terrain_render.cpp
)

set(RPG_ASSET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets")

add_executable(FreeWalkRpgDemo WIN32
    src/main.cpp
    ${RPG_SHARED_SOURCES}
)

add_executable(SceneEditor WIN32
    src/editor.cpp
    ${RPG_SHARED_SOURCES}
)

function(configure_rpg_target target_name)
    target_compile_definitions(${target_name} PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        RPG_ASSET_DIR=L"${RPG_ASSET_DIR}"
    )

    target_compile_options(${target_name} PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
        $<$<CXX_COMPILER_ID:GNU>:-finput-charset=UTF-8>
    )

    target_link_libraries(${target_name} PRIVATE
        user32
        gdi32
        gdiplus
        comdlg32
        msimg32
    )

    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:${target_name}>/assets
    )

    if(MINGW)
        if(target_name STREQUAL "FreeWalkRpgDemo")
            target_link_options(${target_name} PRIVATE -municode)
        endif()

        get_filename_component(RPG_MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
        foreach(runtime_dll IN ITEMS libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${RPG_MINGW_BIN_DIR}/${runtime_dll}"
                    "$<TARGET_FILE_DIR:${target_name}>/${runtime_dll}"
                VERBATIM
            )
        endforeach()
    endif()
endfunction()

configure_rpg_target(FreeWalkRpgDemo)
configure_rpg_target(SceneEditor)

enable_testing()

add_executable(SceneDataTests
    tests/scene_data_tests.cpp
    src/scene.cpp
)

target_compile_definitions(SceneDataTests PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    RPG_ASSET_DIR=L"${RPG_ASSET_DIR}"
)

add_test(NAME SceneDataRoundTrip COMMAND SceneDataTests)
