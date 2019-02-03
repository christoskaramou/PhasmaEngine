using System.Runtime.CompilerServices;

public class Global
{
    [MethodImplAttribute(MethodImplOptions.InternalCall)]
    public extern static void LoadModel(string folderPath, string modelName);

    [MethodImplAttribute(MethodImplOptions.InternalCall)]
    public extern static bool IsKeyDown(Key key);

    [MethodImplAttribute(MethodImplOptions.InternalCall)]
    public extern static void SetTimeScale(float timeScale);
}
