# cmake/Warnings.cmake
# Флаги предупреждений компилятора

function(apply_warnings target)
    target_compile_options(${target} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wcast-align
        -Wstrict-prototypes
        -Wmissing-prototypes
        -Wold-style-definition
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
    )
    if(INDICATOR_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE -Werror)
    endif()
endfunction()
