#pragma once

namespace vm {

	struct Shader;

	struct Reflection
	{
		Reflection(Shader* vert, Shader* frag);

	private:
		void CreateResources();
		void GetResources();
	private:

		Shader* vert;
		Shader* frag;
	};

}