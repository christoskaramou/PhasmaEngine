using System;
using Microsoft.Xna.Framework;

public class Test
{
    public Transform transform;
    private float s = 0.0f;

	public Test()
	{
		Ext.LoadModel("objects/DamagedHelmet/glTF/", "DamagedHelmet.gltf");
	}
	
	public void Update(float delta)
	{
        s += delta;
		float r = (float)Math.Sin(s);
		transform.position = new Vector3(0.0f, 0.0f, r * 5.0f);
		transform.rotation = Quaternion.CreateFromYawPitchRoll(s, 0.0f, 0.0f);
        transform.scale = Vector3.One;
	}
}