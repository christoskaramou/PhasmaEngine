#pragma once

#include <mono/jit/jit.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/mono-config.h>
#include <mono/metadata/debug-helpers.h>
#include <mono/metadata/threads.h>
#include <mono/metadata/mono-debug.h>
#include <vector>
#include <filesystem>
#include <string>

namespace vm {
	struct Script
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