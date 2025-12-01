cmake_minimum_required(VERSION 3.15)
project(OrderManagementEngine)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
set(CMAKE_CXX_FLAGS_BENCHMARK "-O3 -march=native -DBENCHMARK_MODE")

include_directories(include)

# Main library - consolidated orderbook with data fabric
add_library(orderbook_lib
    src/orderbook.cpp
    src/bid_ask.cpp
)

# Main executable
add_executable(orderbook_main
    src/main.cpp
)
target_link_libraries(orderbook_main orderbook_lib)

enable_testing()

# Tests (uncomment when test files are created)
# add_executable(test_itch tests/test_itch_parsing.cpp)
# target_link_libraries(test_itch ome_lib)
# add_test(NAME ITCHParsingTests COMMAND test_itch)

# add_executable(test_orders tests/test_order_operations.cpp)
# target_link_libraries(test_orders ome_lib)
# add_test(NAME OrderOperationTests COMMAND test_orders)

# add_executable(test_integration tests/test_integration.cpp)
# target_link_libraries(test_integration ome_lib)
# add_test(NAME IntegrationTests COMMAND test_integration)

# Benchmarks (uncomment when benchmark file is created)
# add_executable(benchmark_ome benchmarks/benchmark_ome.cpp)
# target_link_libraries(benchmark_ome ome_lib)

# Compiler flags for all targets
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()