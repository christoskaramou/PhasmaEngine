using System;
using Microsoft.Xna.Framework;

public class DamagedHelmet
{
    public Transform transform;
    private float s = 0.0f;

    public DamagedHelmet()
    {
        transform.scale = new Vector3(0.5f);
        transform.position = new Vector3(0.0f, 0.0f, 0.0f);
    }

    public void Update(float delta)
	{
        s += delta*15.0f;
		transform.rotation = Quaternion.CreateFromYawPitchRoll(s, 0.0f, 1.0f);
	}
}