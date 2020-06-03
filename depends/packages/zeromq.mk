package=zeromq
$(package)_version=4.3.2
$(package)_download_path=https://github.com/zeromq/libzmq/releases/download/v$($(package)_version)/
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=ebd7b5c830d6428956b67a0454a7f8cbed1de74b3b01e5c33c5378e22740f763
$(package)_patches=0001-fix-build-with-mingw-thread-name-fix.patch 0002-no-void-pointer-to-store-function-pointer.patch

define $(package)_set_vars
  $(package)_config_opts=--without-docs --disable-shared --without-libsodium --disable-curve --disable-curve-keygen --disable-perf --disable-Werror
  $(package)_config_opts_linux=--with-pic
  $(package)_cxxflags=-std=c++11
endef

define $(package)_preprocess_cmds
   patch -p1 < $($(package)_patch_dir)/0001-fix-build-with-mingw-thread-name-fix.patch && \
   patch -p1 < $($(package)_patch_dir)/0002-no-void-pointer-to-store-function-pointer.patch && \
   cp -f $(BASEDIR)/config.guess $(BASEDIR)/config.sub config
endef

define $(package)_config_cmds
  $($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE) src/libzmq.la
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install-libLTLIBRARIES install-includeHEADERS install-pkgconfigDATA
endef

define $(package)_postprocess_cmds
  sed -i.old "s/ -lstdc++//" lib/pkgconfig/libzmq.pc && \
  rm -rf bin share
endef
