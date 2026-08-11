# CMake generated Testfile for 
# Source directory: E:/rpg_game
# Build directory: E:/rpg_game/build-msvc
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[SceneDataRoundTrip]=] "E:/rpg_game/build-msvc/SceneDataTests.exe")
set_tests_properties([=[SceneDataRoundTrip]=] PROPERTIES  _BACKTRACE_TRIPLES "E:/rpg_game/CMakeLists.txt;89;add_test;E:/rpg_game/CMakeLists.txt;0;")
add_test([=[ItemDefinitions]=] "E:/rpg_game/build-msvc/ItemDataTests.exe")
set_tests_properties([=[ItemDefinitions]=] PROPERTIES  _BACKTRACE_TRIPLES "E:/rpg_game/CMakeLists.txt;109;add_test;E:/rpg_game/CMakeLists.txt;0;")
