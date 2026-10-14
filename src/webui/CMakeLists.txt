# Copyright (c) 2026 The Chains developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

# chains-app: the node with its HTML interface in a native web view (webview/webview), no Qt.

set(WEBUI_PREBUILT_DIST "" CACHE PATH "Use this already built interface (the output of `npm run build` in src/webui/app) instead of building it with npm.")

# The interface: built with npm (Svelte, TypeScript, Vite), then embedded into the program.
set(webui_app_dir ${CMAKE_CURRENT_SOURCE_DIR}/app)
if(WEBUI_PREBUILT_DIST)
  set(webui_dist ${WEBUI_PREBUILT_DIST})
  file(GLOB_RECURSE webui_dist_files CONFIGURE_DEPENDS ${webui_dist}/*)
  set(webui_dist_stamp ${webui_dist_files})
else()
  find_program(NPM_EXECUTABLE npm REQUIRED)
  set(webui_dist ${CMAKE_CURRENT_BINARY_DIR}/ui-dist)
  file(GLOB_RECURSE webui_app_sources CONFIGURE_DEPENDS
    ${webui_app_dir}/src/*
    ${webui_app_dir}/public/*
  )
  list(FILTER webui_app_sources EXCLUDE REGEX "\\.test\\.ts$")
  add_custom_command(
    OUTPUT ${webui_dist}/index.html
    COMMAND ${CMAKE_COMMAND} -DNPM=${NPM_EXECUTABLE} -DAPP_DIR=${webui_app_dir} -DOUT_DIR=${webui_dist} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/npm_build.cmake
    DEPENDS
      ${webui_app_sources}
      ${webui_app_dir}/index.html
      ${webui_app_dir}/package.json
      ${webui_app_dir}/package-lock.json
      ${webui_app_dir}/vite.config.ts
      ${webui_app_dir}/tsconfig.json
      ${CMAKE_CURRENT_SOURCE_DIR}/cmake/npm_build.cmake
    COMMENT "Building the interface of chains-app (npm)"
    VERBATIM
  )
  set(webui_dist_stamp ${webui_dist}/index.html)
endif()

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/assets_data.cpp
  COMMAND ${CMAKE_COMMAND} -DDIST_DIR=${webui_dist} -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/assets_data.cpp -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
  DEPENDS ${webui_dist_stamp} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
  COMMENT "Embedding the interface of chains-app"
  VERBATIM
)

add_library(chains_webui_assets STATIC EXCLUDE_FROM_ALL
  ${CMAKE_CURRENT_BINARY_DIR}/assets_data.cpp
)
target_link_libraries(chains_webui_assets PRIVATE core_interface)

# What does not need a window: the bridge, the events, the settings of the app, the assets.
add_library(chains_webui_core STATIC EXCLUDE_FROM_ALL
  appconfig.cpp
  assets.cpp
  bridge.cpp
  console.cpp
  events.cpp
  nodeapi.cpp
  nodeevents.cpp
  options.cpp
  sidechainnodes.cpp
  singleinstance.cpp
)
target_link_libraries(chains_webui_core
  PUBLIC
    core_interface
    bitcoin_cli
    bitcoin_common
    bitcoin_util
    univalue
)

# The wallet functions of the page (the wallet pages of chains-qt), in a build with wallets.
if(ENABLE_WALLET)
  add_library(chains_webui_wallet STATIC EXCLUDE_FROM_ALL
    walletapi.cpp
  )
  target_link_libraries(chains_webui_wallet
    PUBLIC
      core_interface
      chains_webui_core
      bitcoin_wallet
      bitcoin_node
  )
endif()

if(CMAKE_SYSTEM_NAME MATCHES "Windows|Darwin")
  set(webui_window window_generic.cpp)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(WEBKITGTK REQUIRED IMPORTED_TARGET gtk+-3.0 webkit2gtk-4.1)
  set(webui_window window_gtk.cpp tray_gtk.cpp)
endif()

add_executable(chains-app WIN32
  main.cpp
  app.cpp
  ${webui_window}
  ../init/bitcoin-qt.cpp
)
# The vendored webview header is not ours: no warnings from it.
target_include_directories(chains-app SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/webview)
target_link_libraries(chains-app
  core_interface
  chains_webui_core
  chains_webui_assets
  $<TARGET_NAME_IF_EXISTS:chains_webui_wallet>
  bitcoin_node
  $<TARGET_NAME_IF_EXISTS:bitcoin_wallet>
  $<TARGET_NAME_IF_EXISTS:PkgConfig::WEBKITGTK>
)
if(WIN32)
  target_compile_definitions(chains-app PRIVATE WEBVIEW_MSWEBVIEW2_EXPLICIT_LINK=0)
  target_link_libraries(chains-app advapi32 ole32 shell32 shlwapi user32 version)
elseif(APPLE)
  target_link_libraries(chains-app "-framework WebKit" "-framework Cocoa")
endif()
install_binary_component(chains-app)
if(NOT WIN32 AND NOT APPLE)
  # The desktop entry: the app in the menus of the desktop, and the handler of chains: links (BIP 21).
  install(FILES ${PROJECT_SOURCE_DIR}/share/chains-app.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
    COMPONENT chains-app
  )
  install(FILES ${PROJECT_SOURCE_DIR}/share/pixmaps/bitcoin256.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
    RENAME chains.png
    COMPONENT chains-app
  )
endif()

if(BUILD_TESTS)
  add_subdirectory(test)
endif()
