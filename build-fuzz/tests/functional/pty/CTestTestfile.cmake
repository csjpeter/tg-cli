# CMake generated Testfile for 
# Source directory: /home/csjpeter/ai-projects/tg-cli/tests/functional/pty
# Build directory: /home/csjpeter/ai-projects/tg-cli/build-fuzz/tests/functional/pty
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(PtyTests "/home/csjpeter/ai-projects/tg-cli/build-fuzz/tests/functional/pty/pty-test-runner")
set_tests_properties(PtyTests PROPERTIES  _BACKTRACE_TRIPLES "/home/csjpeter/ai-projects/tg-cli/tests/functional/pty/CMakeLists.txt;12;add_test;/home/csjpeter/ai-projects/tg-cli/tests/functional/pty/CMakeLists.txt;0;")
add_test(PtyTuiTests "/home/csjpeter/ai-projects/tg-cli/build-fuzz/tests/functional/pty/pty-tui-test-runner")
set_tests_properties(PtyTuiTests PROPERTIES  _BACKTRACE_TRIPLES "/home/csjpeter/ai-projects/tg-cli/tests/functional/pty/CMakeLists.txt;44;add_test;/home/csjpeter/ai-projects/tg-cli/tests/functional/pty/CMakeLists.txt;0;")
add_test(PtyTuiResizeTests "/home/csjpeter/ai-projects/tg-cli/build-fuzz/tests/functional/pty/pty-tui-resize-test-runner")
set_tests_properties(PtyTuiResizeTests PROPERTIES  _BACKTRACE_TRIPLES "/home/csjpeter/ai-projects/tg-cli/tests/functional/pty/CMakeLists.txt;73;add_test;/home/csjpeter/ai-projects/tg-cli/tests/functional/pty/CMakeLists.txt;0;")
add_test(PtyReadlineTests "/home/csjpeter/ai-projects/tg-cli/build-fuzz/tests/functional/pty/pty-readline-test-runner")
set_tests_properties(PtyReadlineTests PROPERTIES  _BACKTRACE_TRIPLES "/home/csjpeter/ai-projects/tg-cli/tests/functional/pty/CMakeLists.txt;106;add_test;/home/csjpeter/ai-projects/tg-cli/tests/functional/pty/CMakeLists.txt;0;")
add_test(PtyCtrlCTests "/home/csjpeter/ai-projects/tg-cli/build-fuzz/tests/functional/pty/pty-ctrl-c-test-runner")
set_tests_properties(PtyCtrlCTests PROPERTIES  _BACKTRACE_TRIPLES "/home/csjpeter/ai-projects/tg-cli/tests/functional/pty/CMakeLists.txt;135;add_test;/home/csjpeter/ai-projects/tg-cli/tests/functional/pty/CMakeLists.txt;0;")
