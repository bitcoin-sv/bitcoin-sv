# Copyright (c) 2017 The Bitcoin developers
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

project(crypto)

option(CRYPTO_USE_ASM "Use assembly version of crypto primitives" ON)

# The library
add_library(crypto
	aes.cpp
	chacha20.cpp
	hmac_sha256.cpp
	hmac_sha512.cpp
	ripemd160.cpp
	sha1.cpp
	sha256.cpp
	$<$<BOOL:${CRYPTO_USE_ASM}>:sha256_sse4.cpp>
	sha512.cpp
)

target_include_directories(crypto
	PRIVATE
		..
	PUBLIC
		# To access the config.
		${CMAKE_CURRENT_BINARY_DIR}/..
)

target_compile_definitions(crypto PUBLIC HAVE_CONFIG_H)

# Use assembly is specified
if(CRYPTO_USE_ASM)
	if(enable_asan)
		message(FATAL_ERROR "enable_asan can not be used with"
			" CRYPTO_USE_ASM enabled!")
	endif()
	target_compile_definitions(crypto PRIVATE USE_ASM)
endif()

# Dependencies
target_link_libraries(crypto ${OPENSSL_CRYPTO_LIBRARY})

if(MSVC)
	# crypt32  is required for CertOpenStore and other certificate related functions
	target_link_libraries(crypto crypt32)
endif()
