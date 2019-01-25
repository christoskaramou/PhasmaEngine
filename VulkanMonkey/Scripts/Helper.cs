using System;
using System.Runtime.InteropServices;
using Microsoft.Xna.Framework;

[StructLayout(LayoutKind.Sequential)]
public struct Transform
{
    public Vector3 scale;// = Vector3.One;
    public Quaternion rotation;// = Quaternion.Identity;
    public Vector3 position;// = Vector3.Zero;
}