#include "gem5/m5ops.h"
#include <iostream>
#include <vector>

int main(int argc, char *argv[])
{

    std::cout<<"Before add mem region"<<std::endl;

    std::vector<int> v = {1,2,3,4,5};

    uint64_t start = reinterpret_cast<uint64_t>(v.data());
    uint64_t end = start+v.size()*sizeof(int);

    m5_add_mem_region(start,end);

    std::cout<<"After mem region"<<std::endl;

    return 0;
}