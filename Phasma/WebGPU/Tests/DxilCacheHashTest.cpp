#include "DxilCacheHash.h"

#include <SDL.h>

int main(int /*argc*/, char * /*argv*/[])
{
#if defined(PE_WIN32)
    struct TestVector
    {
        const char *name;
        const char *input;
        const char *expected;
    };

    const TestVector vectors[] = {
        {
            "empty",
            "",
            "e3b0c44298fc1c149afbf4c8996fb924"
            "27ae41e4649b934ca495991b7852b855",
        },
        {
            "abc",
            "abc",
            "ba7816bf8f01cfea414140de5dae2223"
            "b00361a396177a9cb410ff61f20015ad",
        },
        {
            "multi-block",
            "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
            "248d6a61d20638b8e5c026930c3e6039"
            "a33ce45964ff2167f6ecedd419db06c1",
        },
    };

    int errors = 0;
    for (const TestVector &vector : vectors)
    {
        const std::string actual = pwgpu::DxilCacheSha256Hex(vector.input);
        if (actual == vector.expected)
        {
            printf("OK: %s\n", vector.name);
            continue;
        }

        printf("FAIL: %s\n  expected %s\n  actual   %s\n",
               vector.name,
               vector.expected,
               actual.c_str());
        ++errors;
    }

    return errors == 0 ? 0 : 1;
#else
    return 0;
#endif
}
