# Copyright (c) 2017 The Bitcoin developers

# Add Berkeley DB dependency.
find_package(BerkeleyDB REQUIRED)

# Add event dependency. This is only required for evhttp_uridecode
# in rpcwallet.cpp so it may be worth considering using an alternative.
find_package(Event REQUIRED)

add_library(wallet
	crypter.cpp
	db.cpp
	finaltx.cpp
	rpcdump.cpp
	rpcwallet.cpp
	wallet.cpp
	walletdb.cpp
)

enable_coverage(wallet)

target_link_libraries(wallet util univalue ${BDBXX_LIBRARY} ${EVENT_LIBRARY})

target_include_directories(wallet
	PUBLIC
		${BDBXX_INCLUDE_DIR}
	PRIVATE
		${EVENT_INCLUDE_DIR}
)

if(ENABLE_CLANG_TIDY)
    AddClangTidy(wallet)
endif()
