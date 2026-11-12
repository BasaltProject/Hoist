hoist_module(expr SOURCES src/Expression.cpp src/Placeholders.cpp)
hoist_test(hoist-expr-test SOURCE test/ExprTest.cpp LIBS hoist::expr)
