# Copyright (c) 2026 The Chains developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

# Unit tests of the host side of chains-app: the bridge, the events, the app
# settings, the assets and the single-instance socket. No window is needed.
add_executable(test_chains-app
  main.cpp
  appconfig_tests.cpp
  assets_tests.cpp
  bridge_tests.cpp
  events_tests.cpp
  nodeevents_tests.cpp
  singleinstance_tests.cpp
)
target_link_libraries(test_chains-app
  core_interface
  chains_webui_core
  chains_webui_assets
  test_util
  bitcoin_node
  Boost::headers
)
add_test(NAME webui_tests COMMAND test_chains-app)

# The tests of the interface (vitest), and the end-to-end smoke test of chains-app in a virtual
# display (it also saves pictures of every page in e2e-shots).
if(NPM_EXECUTABLE)
  add_test(NAME webui_frontend_tests COMMAND ${NPM_EXECUTABLE} test WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../app)
endif()
find_program(XVFB_RUN_EXECUTABLE xvfb-run)
find_package(Python3 COMPONENTS Interpreter)
if(XVFB_RUN_EXECUTABLE AND Python3_Interpreter_FOUND)
  add_test(NAME webui_e2e_smoke
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/e2e/run_smoke.py
      --app $<TARGET_FILE:chains-app>
      --tmp ${CMAKE_CURRENT_BINARY_DIR}/e2e
      --shots ${CMAKE_CURRENT_BINARY_DIR}/e2e-shots
  )
  set_tests_properties(webui_e2e_smoke PROPERTIES TIMEOUT 1500 RUN_SERIAL TRUE)
endif()
