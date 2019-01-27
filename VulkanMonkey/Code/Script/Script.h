#pragma once

#include "../Code/Math/Math.h"
#include <mono/jit/jit.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/mono-config.h>
#include <mono/metadata/debug-helpers.h>
#include <vector>
#include <filesystem>

namespace vm {
	struct Script
	{
	private:
		static MonoDomain* monoDomain;

		MonoAssembly* assembly;
		MonoImage* monoImage;
		MonoClass* scriptClass;
		MonoObject* scriptInstance;
		std::vector<MonoMethod*> methods{};
		MonoMethod* ctor;
		MonoMethod* dtor;
		MonoMethod* update;
		std::vector<MonoClassField*> fields{};

		static bool initialized;

	public:
		static std::vector<std::string> dlls;
		static void Init();
		static void Cleanup();
		static void addCallback(const char* target, const void* staticFunction);
		Script(std::string file, std::string extension = "dll");
		~Script();

		void Update(float delta);

		template<class T>
		T getValue(const char* name)
		{
			T value;
			MonoClassField* idField = mono_class_get_field_from_name(scriptClass, name);
			if (idField)
				mono_field_get_value(scriptInstance, idField, &value);
			return value;
		}

		template<class T>
		void getValue(T& object, const char* name)
		{
			MonoClassField* idField = mono_class_get_field_from_name(scriptClass, name);
			if (idField)
				mono_field_get_value(scriptInstance, idField, &object);
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