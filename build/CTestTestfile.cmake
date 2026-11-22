# CMake generated Testfile for 
# Source directory: /home/tylor/Projects/suite-control
# Build directory: /home/tylor/Projects/suite-control/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(suite_control_ttl "/home/tylor/Projects/suite-control/build/suite_control_ttl_gen" "--check" "--root" "/home/tylor/Projects/suite-control")
set_tests_properties(suite_control_ttl PROPERTIES  _BACKTRACE_TRIPLES "/home/tylor/Projects/suite-control/CMakeLists.txt;108;add_test;/home/tylor/Projects/suite-control/CMakeLists.txt;0;")
add_test(suite_lv2_host "/home/tylor/Projects/suite-control/build/suite_lv2_host_test" "/home/tylor/Projects/suite-control/build/lv2/suite-control.lv2/suite_control.so")
set_tests_properties(suite_lv2_host PROPERTIES  _BACKTRACE_TRIPLES "/home/tylor/Projects/suite-control/CMakeLists.txt;118;add_test;/home/tylor/Projects/suite-control/CMakeLists.txt;0;")
add_test(suite_control_dsp "/home/tylor/Projects/suite-control/build/suite_control_dsp_test")
set_tests_properties(suite_control_dsp PROPERTIES  _BACKTRACE_TRIPLES "/home/tylor/Projects/suite-control/CMakeLists.txt;128;add_test;/home/tylor/Projects/suite-control/CMakeLists.txt;0;")
add_test(suite_control_panel "/home/tylor/Projects/suite-control/build/suite_control_panel_test")
set_tests_properties(suite_control_panel PROPERTIES  _BACKTRACE_TRIPLES "/home/tylor/Projects/suite-control/CMakeLists.txt;141;add_test;/home/tylor/Projects/suite-control/CMakeLists.txt;0;")
add_test(suite_control_host "/home/tylor/Projects/suite-control/build/suite_control_host_test" "/home/tylor/Projects/suite-control/build/clap/SuiteControl.clap")
set_tests_properties(suite_control_host PROPERTIES  _BACKTRACE_TRIPLES "/home/tylor/Projects/suite-control/CMakeLists.txt;149;add_test;/home/tylor/Projects/suite-control/CMakeLists.txt;0;")
add_test(suite_peer_integration "/home/tylor/Projects/suite-control/build/suite_peer_integration" "/home/tylor/.clap/SuiteChannelPlus.clap")
set_tests_properties(suite_peer_integration PROPERTIES  _BACKTRACE_TRIPLES "/home/tylor/Projects/suite-control/CMakeLists.txt;159;add_test;/home/tylor/Projects/suite-control/CMakeLists.txt;0;")
