using System;
using System.Runtime.CompilerServices;

public class Load
{
    private float d = 0.0f;
    private int instances = 0;

    public Load()
	{
        //Global.LoadModel("objects/Corset/glTF/", "Corset.gltf");
        //Global.LoadModel("objects/Duck/glTF/", "Duck.gltf", 1);
        //Global.LoadModel("objects/DamagedHelmet/glTF/", "DamagedHelmet.gltf");
        //Global.LoadModel("objects/sponza/glTF/", "Sponza.gltf");
    }
    public void Update(float delta)
    {
        d += delta;
        //if (Global.KeyDown(Key.SPACE) && d > 0.1f)
        if (d > 0.05f)
        {
            d = 0.0f;
            Global.LoadModel("objects/Duck/glTF/", "Duck.gltf"); // same models will not loaded all over again but a copy will be created which is fast
            instances++;
        }

        if (Global.KeyDown(Key.P))
            Global.SetTimeScale(0.0f);
        else
            Global.SetTimeScale(1.0f);

        if (Global.MouseButtonDown(MouseButton.Middle))
            Global.SetMousePos(500.0f, 200.0f);

        //Mouse m = Global.GetMousePos();
        //float w = Global.GetMouseWheel();
        //Console.WriteLine("Mouse Position: " + m.x + ", " + m.y + ", MouseWheel: " + w);
    }
}