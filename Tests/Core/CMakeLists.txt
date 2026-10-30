# Core test suite. Dependency-free: a minimal framework in ObTest.h, one executable per topic,
# registered with CTest.
set(OB_TEST_SOURCES
  TestCoordinates.cpp
  TestHashAndSerialization.cpp
  TestGenerator.cpp
  TestOperations.cpp
  TestWorld.cpp
  TestReplay.cpp
  TestMesher.cpp
  TestRemeshScheduler.cpp)

foreach(src ${OB_TEST_SOURCES})
  get_filename_component(name ${src} NAME_WE)
  add_executable(${name} ${src} ObTest.h)
  target_link_libraries(${name} PRIVATE OpenBattlefieldCore ob_warnings)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME ${name} COMMAND ${name})
endforeach()
