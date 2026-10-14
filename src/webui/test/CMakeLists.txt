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
  hardening_tests.cpp
  i18n_tests.cpp
  nodeapi_tests.cpp
  nodeevents_tests.cpp
  singleinstance_tests.cpp
  $<$<BOOL:${ENABLE_WALLET}>:walletapi_tests.cpp>
)
target_link_libraries(test_chains-app
  core_interface
  chains_webui_core
  chains_webui_assets
  $<TARGET_NAME_IF_EXISTS:chains_webui_wallet>
  test_util
  bitcoin_node
  Boost::headers
)
add_test(NAME webui_tests COMMAND test_chains-app)

# The tests of the interface (vitest), and the end-to-end smoke test of chains-app in a virtual
# display (it also saves pictures of every page in e2e-shots).
if(NPM_EXECUTABLE)
  add_test(NAME webui_frontend_tests COMMAND ${NPM_EXECUTABLE} test WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../app)
  add_test(NAME webui_frontend_types COMMAND ${NPM_EXECUTABLE} run check WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../app)
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
  if(ENABLE_WALLET)
    # The wallet pages, driven as a user would (also saves pictures of them in e2e-shots).
    add_test(NAME webui_e2e_wallet
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/e2e/run_wallet.py
        --app $<TARGET_FILE:chains-app>
        --tmp ${CMAKE_CURRENT_BINARY_DIR}/e2e-wallet
        --shots ${CMAKE_CURRENT_BINARY_DIR}/e2e-shots
    )
    set_tests_properties(webui_e2e_wallet PROPERTIES TIMEOUT 1500 RUN_SERIAL TRUE)
    # The node and Chains pages, with a pretend desktop (tray, notifications) on a private session
    # bus and a second node as a peer (also saves pictures of these pages in e2e-shots).
    find_program(DBUS_DAEMON_EXECUTABLE dbus-daemon)
    if(DBUS_DAEMON_EXECUTABLE)
      add_test(NAME webui_e2e_node
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/e2e/run_node.py
          --app $<TARGET_FILE:chains-app>
          --tmp ${CMAKE_CURRENT_BINARY_DIR}/e2e-node
          --shots ${CMAKE_CURRENT_BINARY_DIR}/e2e-shots
      )
      set_tests_properties(webui_e2e_node PROPERTIES TIMEOUT 2000 RUN_SERIAL TRUE)
    endif()
  endif()
endif()
