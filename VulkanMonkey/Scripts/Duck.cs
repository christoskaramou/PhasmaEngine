using System;
using Microsoft.Xna.Framework;

public class Duck
{
    public Transform transform;

    private float d = 0.0f;
    private Random rnd = new Random();
    public Duck()
    {
        transform.scale = new Vector3(0.3f);
        transform.position = new Vector3(
                Convert.ToSingle(rnd.NextDouble()) * 3.0f - 1.5f,
                Convert.ToSingle(rnd.NextDouble()) * 3.0f - 1.5f,
                Convert.ToSingle(rnd.NextDouble()) * 3.0f - 1.5f);
        transform.rotation = Quaternion.Identity;
    }

    public void Update(float delta)
	{
        d += delta;
        if (d > 0.5f)
        {
            d = 0.0f;
            transform.position = new Vector3(
                Convert.ToSingle(rnd.NextDouble()) * 3.0f - 1.5f,
                Convert.ToSingle(rnd.NextDouble()) * 3.0f - 1.5f,
                Convert.ToSingle(rnd.NextDouble()) * 3.0f - 1.5f);
        }
    }
}