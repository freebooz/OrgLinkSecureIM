add_library(orglink-protocol STATIC
    src/ApplicationMessages.cpp
    src/Frame.cpp
)
add_library(orglink::protocol ALIAS orglink-protocol)

target_include_directories(orglink-protocol PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(orglink-protocol PUBLIC cxx_std_20)
