#pragma once

// TODO: How will it figure out the path to the script?
#include "../../Code/Script/ScriptObject.h"
#include <iostream>

class ScriptTest2 : public pe::ScriptObject
{
public:
    ScriptTest2() { std::cout << "ScriptTest2()" << std::endl; }

    void Init() override { std::cout << "Init2()" << std::endl; }
    void Update() override { std::cout << "Update2()" << std::endl; }
    void Draw() override { std::cout << "Draw2()" << std::endl; }
    void Destroy() override { std::cout << "Destroy2()" << std::endl; }

private:
    int i2 = 1;
    float f2 = 0.9f;
};
