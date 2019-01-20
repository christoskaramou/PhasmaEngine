using System;
using System.Runtime.CompilerServices;

public class Ext {
  [MethodImplAttribute(MethodImplOptions.InternalCall)]
  public extern static void Sample();
}

namespace MonoScript
{
	public class TestCSharp
	{
		public float rotation = 0.0f;
	
		public void Start()
		{
			
		}
		
		public void Update(float delta)
		{
			System.Console.WriteLine("TestCSharp Update call! delta = " + delta);
			Ext.Sample();
			rotation += 1.0f;
		}
	}
}