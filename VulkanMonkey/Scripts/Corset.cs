using System;
using Microsoft.Xna.Framework;

public class Corset
{
    public Transform transform;
    private float s = 0.0f;
    public Corset()
    {
        transform.scale = new Vector3(20.0f);
        transform.position = new Vector3(0.0f, 0.0f, 0.0f);
    }

    public void Update(float delta)
	{
        if (Global.IsKeyDown(Key.P))
            Global.SetTimeScale(0.0f);
        else
            Global.SetTimeScale(1.0f);

        s += delta;
		transform.rotation = Quaternion.CreateFromYawPitchRoll(s, 0.0f, 0.0f);
	}
}