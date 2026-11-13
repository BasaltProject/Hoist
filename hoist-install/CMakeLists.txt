hoist_module(install SOURCES src/Backup.cpp src/Install.cpp src/State.cpp src/Zip.cpp DEPENDS config core github net LIBS tomlplusplus::tomlplusplus ZLIB::ZLIB)
hoist_test(hoist-install-test SOURCE test/InstallTest.cpp LIBS hoist::install)
