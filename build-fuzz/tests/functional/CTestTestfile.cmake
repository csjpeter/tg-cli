# CMake generated Testfile for 
# Source directory: /home/csjpeter/ai-projects/tg-cli/tests/functional
# Build directory: /home/csjpeter/ai-projects/tg-cli/build-fuzz/tests/functional
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(FunctionalTests "/home/csjpeter/ai-projects/tg-cli/build-fuzz/tests/functional/functional-test-runner")
set_tests_properties(FunctionalTests PROPERTIES  _BACKTRACE_TRIPLES "/home/csjpeter/ai-projects/tg-cli/tests/functional/CMakeLists.txt;42;add_test;/home/csjpeter/ai-projects/tg-cli/tests/functional/CMakeLists.txt;0;")
subdirs("pty")
