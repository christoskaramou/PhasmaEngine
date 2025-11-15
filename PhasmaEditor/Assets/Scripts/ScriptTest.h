#pragma once

// TODO: How will it figure out the path to the script?
#include "ScriptObject.h"
#include <iostream>

class ScriptTest : public pe::ScriptObject
{
public:
    ScriptTest();

    void Init() override;
    void Update() override;
    void Draw() override;
    void Destroy() override;

private:
    int i = 1;
    float f = 0.9f;
};
