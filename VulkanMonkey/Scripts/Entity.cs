using System;
 
namespace MonoScript
{
  public class Entity
  {
 
    public Entity()
    {
      System.Console.WriteLine("Entity constructed");
    }
 
    ~Entity()
    {
      System.Console.WriteLine("Entity destructed");
    }

  }
}