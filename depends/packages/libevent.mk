package=libevent
$(package)_version=2.1.11
$(package)_download_path=https://github.com/libevent/libevent/releases/download/release-2.1.11-stable/
$(package)_file_name=libevent-$($(package)_version)-stable.tar.gz
$(package)_sha256_hash=a65bac6202ea8c5609fd5c7e480e6d25de467ea1917c08290c521752f147283d

define $(package)_preprocess_cmds
  ./autogen.sh
endef

define $(package)_set_vars
  $(package)_config_opts=--disable-shared --disable-openssl --disable-libevent-regress
  $(package)_config_opts_release=--disable-debug-mode
  $(package)_config_opts_linux=--with-pic
endef

define $(package)_config_cmds
  $($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef

define $(package)_postprocess_cmds
endef
