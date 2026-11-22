# CMake generated Testfile for 
# Source directory: /home/tylor/Projects/suite-control
# Build directory: /home/tylor/Projects/suite-control/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(suite_control_dsp "/home/tylor/Projects/suite-control/build/suite_control_dsp_test")
set_tests_properties(suite_control_dsp PROPERTIES  _BACKTRACE_TRIPLES "/home/tylor/Projects/suite-control/CMakeLists.txt;71;add_test;/home/tylor/Projects/suite-control/CMakeLists.txt;0;")
add_test(suite_control_panel "/home/tylor/Projects/suite-control/build/suite_control_panel_test")
set_tests_properties(suite_control_panel PROPERTIES  _BACKTRACE_TRIPLES "/home/tylor/Projects/suite-control/CMakeLists.txt;84;add_test;/home/tylor/Projects/suite-control/CMakeLists.txt;0;")
add_test(suite_control_host "/home/tylor/Projects/suite-control/build/suite_control_host_test" "/home/tylor/Projects/suite-control/build/clap/SuiteControl.clap")
set_tests_properties(suite_control_host PROPERTIES  _BACKTRACE_TRIPLES "/home/tylor/Projects/suite-control/CMakeLists.txt;92;add_test;/home/tylor/Projects/suite-control/CMakeLists.txt;0;")
add_test(suite_peer_integration "/home/tylor/Projects/suite-control/build/suite_peer_integration" "/home/tylor/.clap/SuiteChannelPlus.clap")
set_tests_properties(suite_peer_integration PROPERTIES  _BACKTRACE_TRIPLES "/home/tylor/Projects/suite-control/CMakeLists.txt;102;add_test;/home/tylor/Projects/suite-control/CMakeLists.txt;0;")
