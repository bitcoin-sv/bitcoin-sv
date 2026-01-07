package=gperftools
$(package)_version=2.15
$(package)_download_path=https://github.com/gperftools/gperftools/releases/download/gperftools-2.15/
$(package)_file_name=gperftools-$($(package)_version).tar.gz
$(package)_sha256_hash=c69fef855628c81ef56f12e3c58f2b7ce1f326c0a1fe783e5cae0b88cbbe9a80

define $(package)_preprocess_cmds
endef

define $(package)_set_vars
  $(package)_config_opts=--enable-shared=no --enable-minimal
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
