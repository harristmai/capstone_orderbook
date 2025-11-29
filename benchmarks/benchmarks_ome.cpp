#include <chrono>
#include <iostream>
#include <vector>

#include "order_management_engine.h"

class BenchmarkTimer
{
   public:
    void start()
    {
        begin = std::chrono::steady_clock::now();
    }

    uint64_t elapsed_ns()
    {
        auto end = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
    }

   private:
    std::chrono::steady_clock::time_point begin;
};

int main()
{
    OrderManagementEngine ome;

    // Generate test data
    std::vector<uint8_t> test_data(10000);  // 10KB of test data

    BenchmarkTimer timer;
    timer.start();

    for (int i = 0; i < 1000; ++i)
    {
        ome.process_itch_chunk(test_data);
    }

    uint64_t total_ns = timer.elapsed_ns();

    std::cout << "Total time: " << total_ns / 1e6 << " ms\n";
    std::cout << "Orders processed: " << ome.get_orders_processed() << "\n";
    std::cout << "Throughput: " << (ome.get_orders_processed() * 1e9 / total_ns) << " orders/sec\n";

    return 0;
}