using System;
 
namespace MonoScript
{
  public class Test
  {
 
    public Test()
    {
      System.Console.WriteLine("Test constructed");
    }
 
    ~Test()
    {
      System.Console.WriteLine("Test destructed");
    }

	public void Start()
	{
		
	}
	
	public void Update(float delta)
	{
		System.Console.WriteLine("Test Update call! delta = " + delta);
	}
  }
}