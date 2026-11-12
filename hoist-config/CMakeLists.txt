hoist_module(config SOURCES src/Config.cpp src/Defaults.cpp src/Reader.cpp DEPENDS core expr LIBS tomlplusplus::tomlplusplus)
hoist_test(hoist-config-test SOURCE test/ConfigTest.cpp LIBS hoist::config)
