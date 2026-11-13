file(GLOB HOIST_CONFIG_DEFAULTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/resources/config-defaults/*.toml)
hoist_embed_text(hoist-config-defaults
    NAMESPACE basalt::hoist::config
    HEADER basalt/hoist/config/ConfigDefaults.hpp
    SUFFIX DefaultToml
    FILES ${HOIST_CONFIG_DEFAULTS})

hoist_module(config SOURCES src/Config.cpp src/Defaults.cpp src/Reader.cpp DEPENDS core expr
    LIBS tomlplusplus::tomlplusplus hoist-config-defaults)
hoist_test(hoist-config-test SOURCE test/ConfigTest.cpp LIBS hoist::config)
