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

using System;
using Microsoft.Xna.Framework;

public class Duck
{
    public Transform transform;
    public Transform initTransform;
    private float d = 0.0f;
    private float rX = 0.0f;
    private float rY = 0.0f;
    private float rZ = 0.0f;

    private Random rnd = new Random();
    public Duck()
    {
        transform.scale = new Vector3(0.2f);
        transform.position = new Vector3(
                Convert.ToSingle(rnd.NextDouble()) * 3.0f - 1.5f,
                Convert.ToSingle(rnd.NextDouble()) * 3.0f - 1.5f,
                Convert.ToSingle(rnd.NextDouble()) * 3.0f - 1.5f);
        transform.rotation = Quaternion.Identity;
        initTransform = transform;
        rX = Convert.ToSingle(rnd.Next(2));
        rY = Convert.ToSingle(rnd.Next(2));
        rZ = Convert.ToSingle(rnd.Next(2));
    }

    public void Update(float delta)
    {
        d += delta;
        Vector3 v = new Vector3(
            rX * Convert.ToSingle(Math.Tan(Convert.ToDouble(d))),
            rY * Convert.ToSingle(Math.Cos(Convert.ToDouble(d))),
            rZ * Convert.ToSingle(Math.Sin(Convert.ToDouble(d))));
        transform.position = initTransform.position + v;
    }
}