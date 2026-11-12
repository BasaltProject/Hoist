hoist_module(runtime
    SOURCES src/Commands.cpp src/Lock.cpp src/Log.cpp src/Provisioning.cpp src/Scheduler.cpp src/Supervisor.cpp src/UpdateWorker.cpp
    DEPENDS config core expr github install instance net process)
hoist_test(hoist-runtime-test SOURCE test/SchedulerTest.cpp LIBS hoist::runtime)
