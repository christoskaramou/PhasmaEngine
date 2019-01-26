#pragma once

#include <mono/jit/jit.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/mono-config.h>
#include <mono/metadata/debug-helpers.h>
#include <vector>
#include "../Code/Math/Math.h"

namespace vm {
	struct Script
	{
	private:
		static MonoDomain* monoDomain;

		MonoAssembly* assembly;
		MonoImage* monoImage;
		MonoClass* entityClass;
		MonoObject* entityInstance;
		std::vector<MonoMethod*> methods{};
		MonoMethod* ctor;
		MonoMethod* dtor;
		MonoMethod* update;
		std::vector<MonoClassField*> fields{};

		static bool initialized;

	public:
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
			MonoClassField* idField = mono_class_get_field_from_name(entityClass, name);
			mono_field_get_value(entityInstance, idField, &value);
			return value;
		}

		template<class T>
		void getValue(T* value, const char* name)
		{
			MonoClassField* idField = mono_class_get_field_from_name(entityClass, name);
			mono_field_get_value(entityInstance, idField, value);
		}

		template<class T>
		void setValue(T& value, const char* name)
		{
			MonoClassField* idField = mono_class_get_field_from_name(entityClass, name);
			mono_field_set_value(entityInstance, idField, &value);
		}
	};
}