#include "gem5/m5ops.h"
#include <iostream>
#include <memory>
#include <vector>
#include <unistd.h>

bool isprime(uint64_t n)
{
    for(uint64_t i = 2;i<=n/2;i++)
    {
        if(n%i==0)
            return false;
    }
    return true;
}

void initialize(std::unique_ptr<uint64_t[]> &v, size_t n)
{
    for(int i=0;i<n;i++)
        v[i] = i*i;
}

int get_sum(std::unique_ptr<uint64_t[]> &v, size_t n)
{
    uint64_t sum= 0 ;
    for(int i=1;i<=n;i++)
        sum+=v[i];
    return sum;
}

std::vector<uint64_t> find_all_primes(uint64_t n)
{
    std::vector<uint64_t> primes;
    for(uint64_t i=2;i<n;i++)
    {
        if(isprime(i))
            primes.push_back(i);
    }
    return primes;
}

template <typename T>
void print_vector(std::vector<T> v)
{
    for(T &ele: v)
    {
        std::cout<<ele<<"\n";
    }
}


int main(int argc, char *argv[]){
    
    m5_checkpoint(0,0);

    size_t n = std::stoi(argv[1]);
    //Find all primes below n
    std::vector<uint64_t> primes = find_all_primes(n);


    std::cout<<"Found "<<primes.size()<<" primes below "<<argv[1]<<"\n";
    std::cout<<"PID: "<<getpid()<<"\n";


    // print_vector(primes);

    return 0;
}