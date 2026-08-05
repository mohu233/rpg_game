cmake_minimum_required(VERSION 3.20)

project(FreeWalkRpgDemo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(RPG_SHARED_SOURCES
    src/scene.cpp
    src/scene_render.cpp
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

    target_link_libraries(${target_name} PRIVATE
        user32
        gdi32
        msimg32
    )

    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:${target_name}>/assets
    )
endfunction()

configure_rpg_target(FreeWalkRpgDemo)
configure_rpg_target(SceneEditor)
