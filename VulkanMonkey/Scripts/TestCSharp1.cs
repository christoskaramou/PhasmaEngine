using System;
 
namespace MonoScript
{
  public class TestCSharp1
  {
 
    public TestCSharp1()
    {
      System.Console.WriteLine("TestCSharp1 constructed");
    }
 
    ~TestCSharp1()
    {
      System.Console.WriteLine("TestCSharp1 destructed");
    }

	public void Start()
	{
		
	}
	
	public void Update(float delta)
	{
		System.Console.WriteLine("TestCSharp1 Update call! delta = " + delta);
	}
  }
}