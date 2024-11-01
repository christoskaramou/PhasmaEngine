#pragma once

class ScriptTest
{
public:
    ScriptTest();

    void Init();
    void Update();
    void Draw();
    void Destroy();

private:
    int i = 1;
    float f = 0.9f;
};
