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

#include "imgui/imgui.h"
#include <cctype>
#include <cstdlib>
#include <cstring>                     // for strlen
#include "vcruntime_string.h"           // for memcpy

namespace pe
{
    // Demonstrate creating a simple console window, with scrolling, filtering, completion and history.
    // For the console example, here we are using a more C++ like approach of declaring a class to hold the data and the functions.
    struct Console
    {
        char InputBuf[256]{};
        ImVector<char *> Items;
        bool ScrollToBottom{};
        ImVector<char *> History;
        int HistoryPos;    // -1: new line, 0..History.Size-1 browsing history.
        ImVector<const char *> Commands;
        static bool close_app;

        Console();

        ~Console();

        // Portable helpers
        static int Stricmp(const char *str1, const char *str2)
        {
            int d;
            while ((d = toupper(*str2) - toupper(*str1)) == 0 && *str1)
            {
                str1++;
                str2++;
            }
            return d;
        }

        static int Strnicmp(const char *str1, const char *str2, int n)
        {
            int d = 0;
            while (n > 0 && (d = toupper(*str2) - toupper(*str1)) == 0 && *str1)
            {
                str1++;
                str2++;
                n--;
            }
            return d;
        }

        static char *Strdup(const char *str)
        {
            size_t len = strlen(str) + 1;
            void *buff = malloc(len);
            if (!buff) return nullptr;
            return (char *) memcpy(buff, (const void *) str, len);
        }

        static void Strtrim(char *str)
        {
            char *str_end = str + strlen(str);
            while (str_end > str && str_end[-1] == ' ') str_end--;
            *str_end = 0;
        }

        void ClearLog();

        void AddLog(const char *fmt, ...)

        IM_FMTARGS(2);

        void Draw(const char *title, bool *p_open);

        void ExecCommand(const char *command_line);

        static int TextEditCallbackStub(
                ImGuiInputTextCallbackData *data
        ); // In C++11 you are better off using lambdas for this sort of forwarding callbacks
        int TextEditCallback(ImGuiInputTextCallbackData *data);
    };
}