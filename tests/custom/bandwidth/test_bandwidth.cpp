#include <fstream>
#include <iostream>
#include "gem5/m5ops.h"
#include <cstdint>

int main(){

    const size_t size=4096*4096;
    const uint8_t val=0x69;

    // Allocate an array of uint8
    uint8_t *arr = new uint8_t[size];

    for (int i=0;i<size;i++)
    {
        arr[i] = val;
    }

    m5_add_mem_region(1,reinterpret_cast<uint64_t>(arr),reinterpret_cast<uint64_t>(arr)+size*sizeof(uint8_t));
    m5_mem_region_cmd(0);
    
    // Checkpoint it
    m5_checkpoint(0,0);
    
    m5_mem_region_cmd(1);
    size_t sum = 0;
    // Start gem5 roi
    m5_mem_region_cmd(2);
    for (int i=0;i<size;i++)
    {
        sum += arr[i];
    }
    m5_mem_region_cmd(3);

    std::cout<<"SUM:"<<sum<<std::endl;

    return 0;
}