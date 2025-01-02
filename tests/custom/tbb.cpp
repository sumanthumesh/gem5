#include <iostream>
#include <vector>
#include <tbb/concurrent_vector.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_sort.h>

int main() {
    // Create a concurrent vector
    tbb::concurrent_vector<int> vec;

    // Populate the vector in parallel
    tbb::parallel_for(0, 100, [&](int i) {
        vec.push_back(100 - i);  // Push elements in reverse order
    });

    // Sort the vector in parallel
    tbb::parallel_sort(vec.begin(), vec.end());

    // Print the sorted vector
    for (const auto &val : vec) {
        std::cout << val << " ";
    }
    std::cout << std::endl;

    return 0;
}
