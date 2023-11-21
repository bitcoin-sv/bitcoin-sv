# Copyright (c) 2023 Bitcoin Association Distributed under the Open BSV
# software license, see the accompanying file LICENSE

function(AddClangTidy target)
    find_program(CLANG-TIDY_PATH clang-tidy REQUIRED)
    set_target_properties(${target}
        PROPERTIES CXX_CLANG_TIDY
        "${CLANG-TIDY_PATH};-p;${CMAKE_BINARY_DIR}")
endfunction()
