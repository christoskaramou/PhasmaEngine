/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

namespace pe
{
    class Path
    {
    public:
        inline static std::string Assets;
        inline static std::string Executable;

    private:
        friend class Constructor;

        // Helper to initialize static variables
        class Constructor
        {
        public:
            Constructor()
            {
#ifdef WIN32
                char str[MAX_PATH];
                GetModuleFileNameA(nullptr, str, MAX_PATH);
                Executable = std::filesystem::path(str).remove_filename().string();
#endif

                if (!std::filesystem::exists(Executable))
                    Executable = std::filesystem::current_path().string();

                std::replace(Executable.begin(), Executable.end(), '\\', '/');

                // Search for assets folder
                std::filesystem::path path(Executable);
                while (true)
                {
                    std::filesystem::path parent = path.parent_path();
                    if (path == parent)
                        break;

                    path = parent;
                    if (std::filesystem::exists(path / "Phasma"))
                    {
                        if (std::filesystem::exists(path / "Phasma" / "Assets"))
                        {
                            Assets = path.string() + "/Phasma/Assets/";
                            break;
                        }
                    }
                }

                if (Assets.empty())
                    Assets = Executable + "Assets/";
            }
        };

        inline static Constructor s_constructor;
    };
}
