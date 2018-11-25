#include "../include/Queue.h"

using namespace vm;

std::vector<std::tuple<std::string, std::string>> Queue::loadModel{};
std::vector<std::future<void>> Queue::func{};