// iostream test for Serotonin OS
#include <iostream>
#include <string>

int main(int argc, char** argv, char** envp) {
    (void)argc; (void)argv; (void)envp;

    

    std::cout << "Hello from iostream!" << std::endl;
    std::cout << "Testing numbers: " << 42 << std::endl;

    std::string name = "Serotonin";
    std::cout << "OS: " << name << std::endl;

    return 0;
}
