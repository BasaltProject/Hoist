hoist_module(github SOURCES src/GitHub.cpp DEPENDS core net)
hoist_test(hoist-github-test SOURCE test/GitHubTest.cpp LIBS hoist::github)
