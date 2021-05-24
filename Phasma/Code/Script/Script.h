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

#if 0
#include <mono/jit/jit.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/mono-config.h>
#include <mono/metadata/debug-helpers.h>
#include <mono/metadata/threads.h>
#include <mono/metadata/mono-debug.h>
#include <vector>
#include <filesystem>
#include <string>

namespace pe
{
	class Script
	{
	private:
		static inline MonoDomain* domain = nullptr;
		static inline MonoThread* mainThread = nullptr;

		MonoDomain* child;

		MonoAssembly* assembly;
		MonoImage* monoImage;
		MonoClass* scriptClass;
		MonoObject* scriptInstance;
		std::vector<MonoMethod*> methods{};
		MonoMethod* ctor;
		MonoMethod* dtor;
		MonoMethod* updateFunc;
		std::vector<MonoClassField*> fields{};

		static bool initialized;

	public:
		std::string name;
		std::string ext; // .dll, .exe
		std::vector<std::string> includes{"CPPcallbacks.cs", "Helper.cs", "MonoGame.Framework.dll"}; // include scripts and libs

		static std::vector<std::string> dlls;
		static void Init();
		static void Cleanup();
		static void addCallback(const char* target, const void* staticFunction);
		Script(const char* file, const char* extension = "dll");
		~Script();

		void update(float delta) const;

		template<class T>
		void getValue(T& value, const char* name)
		{
			MonoClassField* idField = mono_class_get_field_from_name(scriptClass, name);
			if (idField)
				mono_field_get_value(scriptInstance, idField, &value);
		}

		template<class T>
		T getValue(const char* name)
		{
			T value;
			getValue(value, name);
			return value;
		}

		template<class T>
		void setValue(T& value, const char* name)
		{
			MonoClassField* idField = mono_class_get_field_from_name(scriptClass, name);
			if (idField)
				mono_field_set_value(scriptInstance, idField, &value);
		}
	};
}
#endif