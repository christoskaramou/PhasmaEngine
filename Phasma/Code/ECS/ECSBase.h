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
    template <class T, class U>
    constexpr void ValidateBaseClass()
    {
        static_assert(std::is_base_of<T, U>::value, "ValidateBaseClass<T, U>(): \"T is not the base class of U\" assertion");
    }

    class BaseBehaviour
    {
    public:
        virtual ~BaseBehaviour()
        {
        }

        virtual void Init()
        {
        }

        virtual void Update(double delta)
        {
        }

        virtual void Draw()
        {
        }

        virtual void FixedUpdate()
        {
        }

        virtual void OnGUI()
        {
        }

        virtual void OnEnable()
        {
        }

        virtual void OnDisable()
        {
        }

        virtual void Destroy()
        {
        }
    };
}
