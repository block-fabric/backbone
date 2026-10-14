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
