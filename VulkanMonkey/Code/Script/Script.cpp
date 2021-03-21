#if 0
#include "Script.h"
#include <fstream>

namespace vm
{
	std::vector<std::string> Script::dlls{};
	bool Script::initialized = false;

	constexpr uint32_t PUBLIC_FLAG = 0x0006;

	bool endsWithValue(const std::string& mainStr, const std::string& toMatch)
	{
		return mainStr.size() >= toMatch.size() && mainStr.compare(mainStr.size() - toMatch.size(), toMatch.size(), toMatch) == 0;
	}

	std::vector<char> readDll(const std::string& filename)
	{
		std::ifstream file(filename, std::ios::ate | std::ios::binary);
		if (!file.is_open()) {
			throw std::runtime_error("failed to open file!");
		}
		const size_t fileSize = static_cast<size_t>(file.tellg());
		std::vector<char> buffer(fileSize);
		file.seekg(0);
		file.read(buffer.data(), fileSize);
		file.close();

		return buffer;
	}

	Script::Script(const char* file, const char* extension)
	{
		if (!initialized)
			Init();

		name = file;
		ext = extension;
		std::string cmd = R"(C:\Windows\System32\cmd.exe /c Include\Mono\lib\mono\4.5\mcs -t:library Scripts\)" + name + ".cs";
		for (auto& incl : includes) {
			if (endsWithValue(incl, ".cs"))
				cmd += " Scripts\\" + incl;
			else if (endsWithValue(incl, ".dll"))
				cmd += " -r:Scripts\\" + incl;
		}
		system(cmd.c_str());

		dlls.clear();
		dlls.shrink_to_fit();
		for (auto& f : std::filesystem::directory_iterator("Scripts"))
		{
			std::string n = f.path().string();
			if (endsWithValue(n, ".dll")) {
				n = n.substr(0, n.find_last_of('.'));
				dlls.push_back(n.substr(n.rfind('\\') + 1));	//	Scripts\\example.dll --> Scripts\\example --> example
			}
		}
		if (std::find(dlls.begin(), dlls.end(), name) == dlls.end())
			throw std::runtime_error("error creating script dll");

		ctor = nullptr;
		dtor = nullptr;
		updateFunc = nullptr;
		child = mono_domain_create_appdomain(const_cast<char*>(name.c_str()), nullptr);
		mono_domain_set(child, false);
		assembly = mono_domain_assembly_open(mono_domain_get(), std::string("Scripts\\" + name + "." + ext).c_str()); // "Scripts/file.extension"
		monoImage = mono_assembly_get_image(assembly);
		scriptClass = mono_class_from_name(monoImage, "", file);
		scriptInstance = mono_object_new(mono_domain_get(), scriptClass);
		mono_runtime_object_init(scriptInstance);

		// variables
		void* iter = nullptr;
		while (MonoClassField* f = mono_class_get_fields(scriptClass, &iter))
			fields.push_back(f);

		// functions
		void* it = nullptr;
		while (MonoMethod* m = mono_class_get_methods(scriptClass, &it))
			methods.push_back(m);

		for (auto& m : methods) {
			if (mono_method_get_flags(m, nullptr) & PUBLIC_FLAG) {
				std::string methodName(mono_method_get_name(m));
				if (methodName == ".ctor")
					ctor = m;
				else if (methodName == "Finalize")
					dtor = m;
				else if (methodName == "Update")
					updateFunc = m;
			}
		}
		mono_domain_set(mono_get_root_domain(), false);
	}

	Script::~Script()
	{
		if (dtor) {
			void** args = nullptr;
			MonoObject* exception = nullptr;
			mono_runtime_invoke(dtor, scriptInstance, args, &exception);
		}
		mono_domain_unload(child);
	}

	void Script::Init()
	{
		if (initialized)
			return;
#if _DEBUG
		//char* options = "--debugger- agent=transport=dt_socket,address=localhost:12345,server=y,suspend=y";
		mono_jit_parse_options(0, nullptr);
		mono_debug_init(MONO_DEBUG_FORMAT_MONO);
#endif
		mono_set_dirs("include/Mono/lib", "include/Mono/etc"); // move to a universal dir
		mono_config_parse(nullptr);
		domain = mono_jit_init("VMonkey");

		for (auto& file : std::filesystem::directory_iterator("Scripts"))
		{
			std::string name = file.path().string();
			if (endsWithValue(name, ".dll")) {
				name = name.substr(0, name.find_last_of('.'));
				dlls.push_back(name.substr(name.rfind('\\') + 1));	//	Scripts\\example.dll --> Scripts\\example --> example
			}
		}

		mainThread = mono_thread_current();
		initialized = true;
	}

	void Script::Cleanup()
	{
		if (domain)
			mono_jit_cleanup(domain);
		domain = nullptr;
		initialized = false;
	}

	void Script::addCallback(const char* target, const void* staticFunction)
	{
		if (!initialized) Init();
		mono_add_internal_call(target, staticFunction);
	}

	void Script::update(float delta) const
	{
		if (!updateFunc) return;

		void* args[1];
		args[0] = &delta;

		MonoObject* exception = nullptr;

		// mono_thread_attach is used so other threads except main can interact with Mono
		// https://www.mono-project.com/docs/advanced/embedding/#threading-issues
		MonoThread* monoThread = mono_thread_attach(mono_get_root_domain());

		mono_runtime_invoke(updateFunc, scriptInstance, args, &exception);

		// Avoid detaching the main thread
		if (mainThread != monoThread)
			mono_thread_detach(monoThread);
	}
}
#endif
