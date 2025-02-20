#include <iostream>
#include "gem5/m5ops.h"
#include <vector>


int main()
{
    int len = 100000;
    std::vector<int> v;
    for(int i=0;i<len;i++)
    {
        v.push_back(i);
    }
    
    std::cout<<"Adding mem region"<<std::endl;
    m5_add_mem_region(1, reinterpret_cast<uint64_t>(v.data()), reinterpret_cast<uint64_t>(v.data()) + (v.size()+1) * sizeof(int));
    std::cout<<"Dumping addresses"<<std::endl;
    m5_mem_region_cmd(0);
    std::cout<<"Checkpoint"<<std::endl;
    m5_checkpoint(0,0);
    std::cout<<"Resuming From Checkpoint"<<std::endl;
    std::cout<<"Loading memory regions"<<std::endl;
    m5_mem_region_cmd(1);
    std::cout<<"Loaded memory regions"<<std::endl;
    int sum = 0;

    for (int &i: v)
    {   ++i;
        sum += i;
    }
    std::cout<<"Sum:"<<sum<<std::endl;
    return 0;
}