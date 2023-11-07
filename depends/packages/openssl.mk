package=openssl
$(package)_version=3.0.12
$(package)_download_path=https://www.openssl.org/source
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=f93c9e8edde5e9166119de31755fc87b4aa34863662f67ddfcba14d0b6b69b61

define $(package)_set_vars
$(package)_config_env=AR="$($(package)_ar)" RANLIB="$($(package)_ranlib)" CC="$($(package)_cc)"
$(package)_config_opts=--prefix=$(host_prefix) --openssldir=$(host_prefix) --libdir=$(host_prefix)/lib
$(package)_config_opts+=no-camellia
$(package)_config_opts+=no-capieng
$(package)_config_opts+=no-cast
$(package)_config_opts+=no-dso
$(package)_config_opts+=no-dtls1
$(package)_config_opts+=no-ec_nistp_64_gcc_128
$(package)_config_opts+=no-gost
$(package)_config_opts+=no-idea
$(package)_config_opts+=no-md2
$(package)_config_opts+=no-mdc2
$(package)_config_opts+=no-rc4
$(package)_config_opts+=no-rc5
$(package)_config_opts+=no-rdrand
$(package)_config_opts+=no-rfc3779
$(package)_config_opts+=no-sctp
$(package)_config_opts+=no-seed
$(package)_config_opts+=no-shared
$(package)_config_opts+=no-ssl-trace
$(package)_config_opts+=no-static-engine
$(package)_config_opts+=no-tests
$(package)_config_opts+=no-unit-test
$(package)_config_opts+=no-weak-ssl-ciphers
$(package)_config_opts+=no-whirlpool
$(package)_config_opts+=no-zlib
$(package)_config_opts+=no-zlib-dynamic
$(package)_config_opts+=$($(package)_cflags) $($(package)_cppflags)
$(package)_config_opts_linux=-fPIC -Wa,--noexecstack
$(package)_config_opts_x86_64_linux=linux-x86_64
$(package)_config_opts_i686_linux=linux-generic32
$(package)_config_opts_arm_linux=linux-generic32
$(package)_config_opts_aarch64_linux=linux-generic64
$(package)_config_opts_mipsel_linux=linux-generic32
$(package)_config_opts_mips_linux=linux-generic32
$(package)_config_opts_powerpc_linux=linux-generic32
$(package)_config_opts_riscv64_linux=linux-generic64
$(package)_config_opts_x86_64_darwin=darwin64-x86_64-cc
$(package)_config_opts_x86_64_mingw32=mingw64
$(package)_config_opts_i686_mingw32=mingw
endef

define $(package)_preprocess_cmds
  echo "Configuration for openssl : $($(package)_config_opts)"
endef

define $(package)_config_cmds
  ./Configure $($(package)_config_opts)
endef

define $(package)_build_cmds
  $(MAKE) -j6
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install_sw
endef

define $(package)_postprocess_cmds
endef
