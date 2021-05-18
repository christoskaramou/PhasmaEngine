#pragma once

#include <fstream>
#include<string>

namespace pe
{
	class Path
	{
	public:
		inline static std::string Assets;
		inline static std::string Executable;

	private:
		friend class Constructor;

		// Helper to initialize static variables
		class Constructor
		{
		public:
			Constructor()
			{
				std::ifstream file("roots.txt");
				if (file)
				{
					std::string line;
					size_t index;
					while (std::getline(file, line))
					{
						index = line.find(s_assetsMarker);
						if (index != std::string::npos)
						{
							Assets = line.substr(index + s_assetsMarker.size());
						}

						index = line.find(s_executableMarker);
						if (index != std::string::npos)
						{
							Executable = line.substr(index + s_executableMarker.size());
						}
					}
				}
			}
		};

		inline static std::string s_assetsMarker = "Assets root: ";
		inline static std::string s_executableMarker = "Executable root: ";
		inline static Constructor s_constructor;
	};
}
