using System;
using System.Runtime.CompilerServices;

public class Ext {
  [MethodImplAttribute(MethodImplOptions.InternalCall)]
  public extern static void LoadModel(string folderPath, string modelName);
}
