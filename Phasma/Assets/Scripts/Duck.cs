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