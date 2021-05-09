using System;
using Microsoft.Xna.Framework;

public class Terrain
{
    public Transform transform;

    public Terrain()
    {
        transform.scale = new Vector3(500.0f);
        transform.position = new Vector3(-190.0f, 30.0f, -40.0f);
    }
}