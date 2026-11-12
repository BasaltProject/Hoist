hoist_module(core SOURCES src/Time.cpp src/Version.cpp)
hoist_test(hoist-core-test SOURCE test/CoreTest.cpp LIBS hoist::core)
