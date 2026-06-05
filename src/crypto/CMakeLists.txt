# Copyright (c) 2017 The Bitcoin developers
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

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
	sha256_dispatch.cpp
	$<$<BOOL:${CRYPTO_USE_ASM}>:sha256_sse4.cpp>
	$<$<BOOL:${CRYPTO_USE_ASM}>:sha256_sse4_shim.cpp>
	sha256d64_scalar.cpp
	sha256d64_shani.cpp
	sha256d64_shani_shim.cpp
	sha256_stream_scalar.cpp
	sha512.cpp
)
# sha256d64_shani.cpp is byte-identical to upstream and gated on
# ENABLE_X86_SHANI; define the macro here only on x86_64 so the TU
# compiles empty elsewhere. SKIP_LINTING keeps clang-tidy off the
# vendored body (CMake 3.27+; older CMake ignores the property).
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
	# -Wno-cast-align: upstream's (__m128i*) casts on byte buffers
	# pair with _mm_loadu_si128 / _mm_storeu_si128 (unaligned variants),
	# but Clang's -Wcast-align flags the cast itself regardless of how
	# the result is used. Verbatim vendoring forbids modifying the body.
	set_source_files_properties(sha256d64_shani.cpp PROPERTIES
		COMPILE_OPTIONS "-msha;-msse4.1;-mssse3;-Wno-cast-align"
		COMPILE_DEFINITIONS "ENABLE_X86_SHANI"
		SKIP_LINTING TRUE)
else()
	set_source_files_properties(sha256d64_shani.cpp
		PROPERTIES SKIP_LINTING TRUE)
endif()
enable_coverage(crypto)

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

if(ENABLE_CLANG_TIDY)
    include(ClangTidy)
    AddClangTidy(crypto)
endif()

# Dependencies
target_link_libraries(crypto ${OPENSSL_CRYPTO_LIBRARY})

if(MSVC)
	# crypt32  is required for CertOpenStore and other certificate related functions
	target_link_libraries(crypto crypt32)
endif()
