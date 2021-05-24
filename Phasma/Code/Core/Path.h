/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

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
