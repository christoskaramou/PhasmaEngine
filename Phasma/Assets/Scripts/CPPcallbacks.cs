using System.Runtime.CompilerServices;

public class Global
{
    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern void LoadModel(string folderPath, string modelName, uint instances = 1);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern bool KeyDown(Key key);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern void SetTimeScale(float time_scale);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern bool MouseButtonDown(MouseButton button);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern ref Mouse GetMousePos();

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern void SetMousePos(float x, float y);

    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern float GetMouseWheel();
}