#pragma once

#include <fstream>
#include <string>
#include <filesystem>

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
				Executable = std::filesystem::current_path().string() + "/";
				std::replace(Executable.begin(), Executable.end(), '\\', '/');
				
				Assets = "Assets/";
				
				std::ifstream file(Executable + "AssetsRoot");
				if (file)
				{
					std::string line;
					size_t index;
					while (std::getline(file, line))
					{
						index = line.find(s_assetsMarker);
						if (index != std::string::npos)
						{
							std::string path = line.substr(index + s_assetsMarker.size());
							path.erase(remove(path.begin(), path.end(), ' '), path.end());
							
							// Formating the path
							std::replace(path.begin(), path.end(), '\\', '/');
							if (path.size() != 0 && path.back() != '/')
							{
								path += "/";
							}
							
							Assets = path;
						}
					}
				}
			}
		};
		
		inline static std::string s_assetsMarker = "Assets root:";
		inline static Constructor s_constructor;
	};
}
