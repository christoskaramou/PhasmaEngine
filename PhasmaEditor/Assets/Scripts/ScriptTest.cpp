#include "ScriptTest.h"

ScriptTest::ScriptTest()
{
    std::cout << "ScriptTest()" << std::endl;
}

void ScriptTest::Init()
{
    std::cout << "Init()" << std::endl;
}

void ScriptTest::Update()
{
    std::cout << "Update()" << std::endl;
}

void ScriptTest::Draw()
{
    std::cout << "Draw()" << std::endl;
}

void ScriptTest::Destroy()
{
    std::cout << "Destroy()" << std::endl;
}

REGISTER_SCRIPT(ScriptTest)
