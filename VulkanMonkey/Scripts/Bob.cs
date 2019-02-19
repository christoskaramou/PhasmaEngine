using System;
using Microsoft.Xna.Framework;

public class Bob
{
    public Transform transform;
    static float next = 0.0f;

    public Bob()
    {
        transform.scale = new Vector3(0.02f);
        transform.position = new Vector3(-2.0f + next, -0.4f, -0.2f);
        next += 1.0f;
    }

    public void Update(float delta)
	{

	}
}