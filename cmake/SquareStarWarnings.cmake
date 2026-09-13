include_guard(GLOBAL)

function(squarestar_enable_cxx_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        if(SQUARESTAR_STRICT_WARNINGS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
        return()
    endif()

    target_compile_options(${target} PRIVATE -Wall -Wextra)
    if(SQUARESTAR_STRICT_WARNINGS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            -Wpedantic
            -Wconversion
            -Wshadow
            -Wformat=2
            -Wundef
            -Werror)
        if(MINGW)
            target_compile_options(${target} PRIVATE -Wno-pedantic-ms-format)
        endif()
    endif()
endfunction()

function(squarestar_enable_c_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(SQUARESTAR_STRICT_WARNINGS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
        return()
    endif()

    target_compile_options(${target} PRIVATE -Wall -Wextra)
    if(SQUARESTAR_STRICT_WARNINGS AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            -Wpedantic
            -Wconversion
            -Wshadow
            -Wformat=2
            -Wundef
            -Werror)
        if(MINGW)
            target_compile_options(${target} PRIVATE -Wno-pedantic-ms-format)
        endif()
    endif()
endfunction()
