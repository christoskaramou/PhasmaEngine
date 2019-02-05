public class Load
{
    private static float d = 0.0f;

    public Load()
	{
		//Global.LoadModel("objects/Corset/glTF/", "Corset.gltf");
		Global.LoadModel("objects/Duck/glTF/", "Duck.gltf");
		//Global.LoadModel("objects/DamagedHelmet/glTF/", "DamagedHelmet.gltf");
		//Global.LoadModel("objects/sponza/glTF/", "Sponza.gltf");
	}
    public void Update(float delta)
    {
        d += delta;
        if (Global.IsKeyDown(Key.SPACE) && d > 0.1f)
        {
            d = 0.0f;
            Global.LoadModel("objects/Duck/glTF/", "Duck.gltf"); // same models will not loaded all over again but a copy will be created which is fast
        }

        if (Global.IsKeyDown(Key.P))
            Global.SetTimeScale(0.0f);
        else
            Global.SetTimeScale(1.0f);
    }
}