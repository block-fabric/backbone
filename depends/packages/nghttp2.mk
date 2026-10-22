package=nghttp2
$(package)_version=1.66.0
$(package)_download_path=https://github.com/nghttp2/nghttp2/releases/download/v$($(package)_version)/
$(package)_file_name=$(package)-$($(package)_version).tar.xz
$(package)_sha256_hash=00ba1bdf0ba2c74b2a4fe6c8b1069dc9d82f82608af24442d430df97c6f9e631
$(package)_build_subdir=build

# libnghttp2 alone (the HTTP/2 framing and HPACK of the gRPC server, WITH_GRPC): no applications,
# no TLS, no other dependency; static.
define $(package)_set_vars
  $(package)_config_opts := -DCMAKE_BUILD_TYPE=None -DENABLE_LIB_ONLY=ON -DENABLE_DOC=OFF -DENABLE_FAILMALLOC=OFF
  $(package)_config_opts += -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DBUILD_TESTING=OFF -DENABLE_HTTP3=OFF
  $(package)_config_opts += -DWITH_LIBXML2=OFF -DWITH_JEMALLOC=OFF -DENABLE_THREADS=OFF
  $(package)_config_opts += -DCMAKE_DISABLE_FIND_PACKAGE_OpenSSL=ON -DCMAKE_DISABLE_FIND_PACKAGE_Libev=ON
  $(package)_config_opts += -DCMAKE_DISABLE_FIND_PACKAGE_ZLIB=ON -DCMAKE_DISABLE_FIND_PACKAGE_Libcares=ON
  $(package)_config_opts += -DCMAKE_DISABLE_FIND_PACKAGE_Systemd=ON -DCMAKE_DISABLE_FIND_PACKAGE_Jansson=ON
  $(package)_config_opts += -DCMAKE_DISABLE_FIND_PACKAGE_Libevent=ON -DCMAKE_DISABLE_FIND_PACKAGE_Jemalloc=ON
  $(package)_cflags += -fdebug-prefix-map=$($(package)_extract_dir)=/usr -fmacro-prefix-map=$($(package)_extract_dir)=/usr
endef

define $(package)_config_cmds
  $($(package)_cmake) -S .. -B .
endef

define $(package)_build_cmds
  $(MAKE) nghttp2_static
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef

define $(package)_postprocess_cmds
  rm -rf share lib/cmake && \
  rm -rf lib/pkgconfig
endef
