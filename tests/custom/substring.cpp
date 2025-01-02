#include <string>
#include <iostream>

int main()
{
    std::string input = "generate_tpch 0.01\n";
    auto substr = input.substr(0, input.find_first_of(" \n;"));

    std::cout<<substr<<std::endl;

    return 0;
}