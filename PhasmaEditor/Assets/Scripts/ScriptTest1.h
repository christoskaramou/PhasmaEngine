#pragma once

#include "ScriptTest.h"
#include <iostream>

class ScriptTest1 : public ScriptTest
{
public:
    ScriptTest1() { std::cout << "ScriptTest1()" << std::endl; }

    void Init() override { std::cout << "Init1()" << std::endl; }
    void Update() override { std::cout << "Update1()" << std::endl; }
    void Draw() override { std::cout << "Draw1()" << std::endl; }
    void Destroy() override { std::cout << "Destroy1()" << std::endl; }

private:
    int i1 = 1;
    float f1 = 0.9f;
};
