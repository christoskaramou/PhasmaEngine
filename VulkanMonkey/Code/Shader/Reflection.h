#pragma once

namespace vm
{
	class Shader;

	class Reflection
	{
	public:
		Reflection(Shader* vert, Shader* frag);
	private:
		void CreateResources();
		void GetResources();

		Shader* vert;
		Shader* frag;
	};

}