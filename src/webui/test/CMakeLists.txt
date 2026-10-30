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
  chainadapter_tests.cpp
  dialog_tests.cpp
  events_tests.cpp
  hardening_tests.cpp
  i18n_tests.cpp
  moduleinstaller_tests.cpp
  nodeapi_tests.cpp
  nodeevents_tests.cpp
  seed_tests.cpp
  singleinstance_tests.cpp
  supervisor_tests.cpp
  sidechainpages_tests.cpp
  windowframe_tests.cpp
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
if(WIN32)
  # The UTF-8 code page, as test_bitcoin.
  add_windows_application_manifest(test_chains-app)
endif()
add_test(NAME webui_tests COMMAND test_chains-app)
install_binary_component(test_chains-app INTERNAL)

# The tests of the interface (vitest), and the end-to-end smoke test of chains-app in a virtual
# display (it also saves pictures of every page in e2e-shots).
if(NPM_EXECUTABLE)
  add_test(NAME webui_frontend_tests COMMAND ${NPM_EXECUTABLE} test WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../app)
  add_test(NAME webui_frontend_types COMMAND ${NPM_EXECUTABLE} run check WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../app)
endif()
find_program(XVFB_RUN_EXECUTABLE xvfb-run)
find_package(Python3 COMPONENTS Interpreter)
# The hooks of the tests are in chains-app only with WEBUI_TEST_HOOKS (never in a release): this checks
# the program has them exactly when it should.
add_test(NAME webui_test_hooks_built
  COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:chains-app> -DEXPECT=${WEBUI_TEST_HOOKS} -P ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/check_test_hooks.cmake
)
# The end-to-end tests drive the app with its test hooks.
if(WEBUI_TEST_HOOKS AND XVFB_RUN_EXECUTABLE AND Python3_Interpreter_FOUND)
  add_test(NAME webui_e2e_smoke
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/e2e/run_smoke.py
      --app $<TARGET_FILE:chains-app>
      --tmp ${CMAKE_CURRENT_BINARY_DIR}/e2e
      --shots ${CMAKE_CURRENT_BINARY_DIR}/e2e-shots
  )
  set_tests_properties(webui_e2e_smoke PROPERTIES TIMEOUT 1500 RUN_SERIAL TRUE)
  if(ENABLE_WALLET)
    # The fixes of the audit (doc/webui-audit.md): chain text that is not UTF-8, long calls, a long history,
    # keys for one call, contrast, narrow windows, settings that wait for Apply, -disablewallet, a language.
    add_test(NAME webui_e2e_audit
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/e2e/run_audit.py
        --app $<TARGET_FILE:chains-app>
        --tmp ${CMAKE_CURRENT_BINARY_DIR}/e2e-audit
        --shots ${CMAKE_CURRENT_BINARY_DIR}/e2e-shots
    )
    set_tests_properties(webui_e2e_audit PROPERTIES TIMEOUT 3000 RUN_SERIAL TRUE)
    # The wallet pages, driven as a user would (also saves pictures of them in e2e-shots).
    add_test(NAME webui_e2e_wallet
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/e2e/run_wallet.py
        --app $<TARGET_FILE:chains-app>
        --tmp ${CMAKE_CURRENT_BINARY_DIR}/e2e-wallet
        --shots ${CMAKE_CURRENT_BINARY_DIR}/e2e-shots
    )
    set_tests_properties(webui_e2e_wallet PROPERTIES TIMEOUT 1500 RUN_SERIAL TRUE)
    # Simple mode, as a user chooses it: Home, the sidebar, Move coins to a real deposit, the
    # glossary, then the switch (the other e2e scripts run in Advanced; also saves pictures in e2e-shots).
    add_test(NAME webui_e2e_simple
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/e2e/run_simple.py
        --app $<TARGET_FILE:chains-app>
        --tmp ${CMAKE_CURRENT_BINARY_DIR}/e2e-simple
        --shots ${CMAKE_CURRENT_BINARY_DIR}/e2e-shots
    )
    set_tests_properties(webui_e2e_simple PROPERTIES TIMEOUT 1500 RUN_SERIAL TRUE)
    # The unified app (doc/unified-app.md): a sidechain run by the app, from Settings → Sidechains to its daemon
    # stopped. Needs the programs of Layers: -DWEBUI_E2E_SIDECHAIN_BINDIR=<layers build>/bin.
    set(WEBUI_E2E_SIDECHAIN_BINDIR "" CACHE PATH "Folder with layersd, for the end-to-end test of the unified app")
    if(WEBUI_E2E_SIDECHAIN_BINDIR)
      add_test(NAME webui_e2e_unified
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/e2e/run_unified.py
          --app $<TARGET_FILE:chains-app>
          --sidechain-bindir ${WEBUI_E2E_SIDECHAIN_BINDIR}
          --tmp ${CMAKE_CURRENT_BINARY_DIR}/e2e-unified
          --shots ${CMAKE_CURRENT_BINARY_DIR}/e2e-shots
      )
      set_tests_properties(webui_e2e_unified PROPERTIES TIMEOUT 1500 RUN_SERIAL TRUE)
      # The Trade page (doc/unified-app.md §22): Layers with its DEX's rules (a layersd whose pages' policy reads its DEX:
      # branch unified-dex of layers): pools made, a swap through two pools asked with its route, liquidity in and out.
      add_test(NAME webui_e2e_dex
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/e2e/run_dex.py
          --app $<TARGET_FILE:chains-app>
          --sidechain-bindir ${WEBUI_E2E_SIDECHAIN_BINDIR}
          --tmp ${CMAKE_CURRENT_BINARY_DIR}/e2e-dex
          --shots ${CMAKE_CURRENT_BINARY_DIR}/e2e-shots
      )
      set_tests_properties(webui_e2e_dex PROPERTIES TIMEOUT 1500 RUN_SERIAL TRUE)
      # What Hivemind's markets came to (doc/unified-app.md §24): markets trading, voted on, partly decided and paid out (yes
      # or no, a scale, several outcomes), Hivemind's pages and the app's. Needs hivemindd: -DWEBUI_E2E_HIVEMIND_BINDIR=<dir>.
      set(WEBUI_E2E_HIVEMIND_BINDIR "" CACHE PATH "Folder with hivemindd, for the end-to-end test of the markets' results")
      if(WEBUI_E2E_HIVEMIND_BINDIR)
        add_test(NAME webui_e2e_hmresults
          COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/e2e/run_hmresults.py
            --app $<TARGET_FILE:chains-app>
            --sidechain-bindir ${WEBUI_E2E_HIVEMIND_BINDIR}
            --tmp ${CMAKE_CURRENT_BINARY_DIR}/e2e-hmresults
            --shots ${CMAKE_CURRENT_BINARY_DIR}/e2e-shots
        )
        set_tests_properties(webui_e2e_hmresults PROPERTIES TIMEOUT 2400 RUN_SERIAL TRUE)
      endif()
      # The pages of the sidechains in the app (doc/unified-app.md §13): Layers and Thunder, their spends through the
      # trusted dialog. Needs thunderd too: -DWEBUI_E2E_THUNDER_BINDIR=<thunder build>/bin.
      set(WEBUI_E2E_THUNDER_BINDIR "" CACHE PATH "Folder with thunderd, for the end-to-end test of the pages of the sidechains")
      if(WEBUI_E2E_THUNDER_BINDIR)
        add_test(NAME webui_e2e_packs
          COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/e2e/run_packs.py
            --app $<TARGET_FILE:chains-app>
            --sidechain-bindir ${WEBUI_E2E_SIDECHAIN_BINDIR}
            --sidechain-bindir ${WEBUI_E2E_THUNDER_BINDIR}
            --tmp ${CMAKE_CURRENT_BINARY_DIR}/e2e-packs
            --shots ${CMAKE_CURRENT_BINARY_DIR}/e2e-shots
        )
        set_tests_properties(webui_e2e_packs PROPERTIES TIMEOUT 3000 RUN_SERIAL TRUE)
        # The app's own pages over every chain (doc/unified-app.md §18): Overview, Send, Receive, Transactions, Wallets,
        # Sidechains, Staking and Assets with Layers and Thunder on.
        # The single seed (doc/unified-app.md §23): the first start, a restore into a fresh data folder, wallets of before.
        add_test(NAME webui_e2e_seed
          COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/e2e/run_seed.py
            --app $<TARGET_FILE:chains-app>
            --sidechain-bindir ${WEBUI_E2E_SIDECHAIN_BINDIR}
            --sidechain-bindir ${WEBUI_E2E_THUNDER_BINDIR}
            --tmp ${CMAKE_CURRENT_BINARY_DIR}/e2e-seed
            --shots ${CMAKE_CURRENT_BINARY_DIR}/e2e-shots
        )
        set_tests_properties(webui_e2e_seed PROPERTIES TIMEOUT 3600 RUN_SERIAL TRUE)
        add_test(NAME webui_e2e_integrated
          COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/e2e/run_integrated.py
            --app $<TARGET_FILE:chains-app>
            --sidechain-bindir ${WEBUI_E2E_SIDECHAIN_BINDIR}
            --sidechain-bindir ${WEBUI_E2E_THUNDER_BINDIR}
            --tmp ${CMAKE_CURRENT_BINARY_DIR}/e2e-integrated
            --shots ${CMAKE_CURRENT_BINARY_DIR}/e2e-shots
        )
        set_tests_properties(webui_e2e_integrated PROPERTIES TIMEOUT 3000 RUN_SERIAL TRUE)
        # What the app costs while nothing happens (doc/unified-app.md §20.6): Home with every chain, a wallet of 20 000
        # records, Layers, Thunder, BitAssets and Hivemind on, software GL; fails over 5 % of one core. Needs the
        # programs of BitAssets and Hivemind too: -DWEBUI_E2E_EXTRA_BINDIRS=<bitassets bin>;<hivemind bin>.
        set(WEBUI_E2E_EXTRA_BINDIRS "" CACHE STRING "Folders with bitassetsd and hivemindd, for the idle cost of the app")
        if(WEBUI_E2E_EXTRA_BINDIRS)
          set(extra_bindirs)
          foreach(dir IN LISTS WEBUI_E2E_EXTRA_BINDIRS)
            list(APPEND extra_bindirs --sidechain-bindir ${dir})
          endforeach()
          add_test(NAME webui_e2e_idle_cost
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/e2e/run_showcase.py
              --app $<TARGET_FILE:chains-app>
              --sidechain-bindir ${WEBUI_E2E_SIDECHAIN_BINDIR}
              --sidechain-bindir ${WEBUI_E2E_THUNDER_BINDIR}
              ${extra_bindirs}
              --state ${CMAKE_CURRENT_BINARY_DIR}/e2e-showcase-state
              --tmp ${CMAKE_CURRENT_BINARY_DIR}/e2e-showcase
              --shots ${CMAKE_CURRENT_BINARY_DIR}/e2e-shots
              --perf --perf-seconds 60 --pages home --budget 5
          )
          set_tests_properties(webui_e2e_idle_cost PROPERTIES TIMEOUT 3600 RUN_SERIAL TRUE)
          # At the hub's scale (doc/unified-app.md §21.3): Chains' wallet of 25 000 transactions, 98 assets with 54 logos,
          # 90 markets; Layers, BitAssets and Hivemind on; the hub's window (3440 x 1380), look and way of drawing
          # (WSLg: software GL with WebKit's DMA-BUF renderer). Idle on each page under 5 % of one core; Home, Overview,
          # Transactions and Hivemind's Markets while the network lives as the hub's (a block of Chains a minute, each
          # sidechain merged-mined in it, a payment on a sidechain every 3 s) under 25 %. The state is made once (about
          # 25 minutes) and kept in e2e-hub-state.
          add_test(NAME webui_e2e_hub_cost
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/e2e/run_showcase.py
              --app $<TARGET_FILE:chains-app>
              --sidechain-bindir ${WEBUI_E2E_SIDECHAIN_BINDIR}
              ${extra_bindirs}
              --scale hub
              --state ${CMAKE_CURRENT_BINARY_DIR}/e2e-hub-state
              --tmp ${CMAKE_CURRENT_BINARY_DIR}/e2e-hub
              --shots ${CMAKE_CURRENT_BINARY_DIR}/e2e-shots-hub
              --screen 3440x1440 --window 3440x1380 --env WEBKIT_DISABLE_DMABUF_RENDERER=
              --perf --perf-seconds 30 --pages home,overview,transactions,sidechains,trade,trade:pools,explorer,hivemind:markets,bitassets:assets
              --steady home,overview,transactions,hivemind:markets,trade --steady-seconds 90
              --budget 5 --page-budget 5 --steady-budget 25
          )
          set_tests_properties(webui_e2e_hub_cost PROPERTIES TIMEOUT 7200 RUN_SERIAL TRUE)
          # Every chain of the catalog at once (doc/unified-app.md §25): the seven on, each one's deposit, payment, address,
          # withdrawal and what it is for (a validator key and a bond, a name, shielded coins, a market), and the pages of
          # every chain. Needs every program in the folders given (unified-bin.next, then unified-bin).
          add_test(NAME webui_e2e_allchains
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/e2e/run_allchains.py
              --app $<TARGET_FILE:chains-app>
              --sidechain-bindir ${WEBUI_E2E_SIDECHAIN_BINDIR}
              ${extra_bindirs}
              --tmp ${CMAKE_CURRENT_BINARY_DIR}/e2e-allchains
              --shots ${CMAKE_CURRENT_BINARY_DIR}/e2e-shots-allchains
          )
          set_tests_properties(webui_e2e_allchains PROPERTIES TIMEOUT 3600 RUN_SERIAL TRUE)
          # The hub's scale with all seven on: idle under 5 % of one core on every page.
          add_test(NAME webui_e2e_hub_cost_allchains
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/e2e/run_showcase.py
              --app $<TARGET_FILE:chains-app>
              --sidechain-bindir ${WEBUI_E2E_SIDECHAIN_BINDIR}
              ${extra_bindirs}
              --scale hub --sidechains layers,fastblocks,thunder,bitnames,bitassets,zside,hivemind
              --state ${CMAKE_CURRENT_BINARY_DIR}/e2e-hub7-state
              --tmp ${CMAKE_CURRENT_BINARY_DIR}/e2e-hub7
              --shots ${CMAKE_CURRENT_BINARY_DIR}/e2e-shots-hub7
              --screen 3440x1440 --window 3440x1380 --env WEBKIT_DISABLE_DMABUF_RENDERER=
              --perf --perf-seconds 30 --pages home,overview,transactions,sidechains,explorer,staking
              --steady home,overview,transactions --steady-seconds 90
              --budget 5 --page-budget 5 --steady-budget 25
          )
          set_tests_properties(webui_e2e_hub_cost_allchains PROPERTIES TIMEOUT 9000 RUN_SERIAL TRUE)
        endif()
      endif()
      # The supervisor's unit tests with a real layersd wired to a real chainsd.
      set_tests_properties(webui_tests PROPERTIES ENVIRONMENT "CHAINS_TEST_SIDECHAIN_BINDIR=${WEBUI_E2E_SIDECHAIN_BINDIR}")
    endif()
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
