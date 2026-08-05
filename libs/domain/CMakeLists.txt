add_library(orglink-domain INTERFACE)
add_library(orglink::domain ALIAS orglink-domain)

target_include_directories(orglink-domain INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(orglink-domain INTERFACE cxx_std_20)

