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

using System.Runtime.CompilerServices;

public class Global
{
    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern void LoadModel(string folderPath, string modelName, uint instances = 1);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern bool KeyDown(Key key);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern void SetTimeScale(float timeScale);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern bool MouseButtonDown(MouseButton button);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern ref Mouse GetMousePos();

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern void SetMousePos(float x, float y);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern float GetMouseWheel();
}