# cmake/Sanitizers.cmake
# AddressSanitizer и UndefinedBehaviorSanitizer (только для host Debug)

if(INDICATOR_ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
    message(STATUS "AddressSanitizer: ON")
endif()

if(INDICATOR_ENABLE_UBSAN)
    add_compile_options(-fsanitize=undefined)
    add_link_options(-fsanitize=undefined)
    message(STATUS "UndefinedBehaviorSanitizer: ON")
endif()
