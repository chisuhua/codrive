#pragma once

#ifdef _WIN32
    #include <Windows.h>
#else
    #include <dlfcn.h>
typedef void (*FARPROC)();
#endif

#include "utils/lang/error.h"

#include <vector>
#include <map>
#include <functional>
#include <string>
// #include "aasim/root_func.h"

namespace utils {

class DynamicLibrary
{
public:

	DynamicLibrary();
	DynamicLibrary(const std::string&);

	~DynamicLibrary();

	bool Load(const std::string& name);
    bool Load(const std::vector<std::string>& names);

	bool UnLoad();
    void* GetProcAddress(const std::string& name);

	template <typename FType>
	std::function<FType> GetFunction(const std::string& func_name)
	{
		auto it = func_map_.find(func_name);
		if (it == func_map_.end())
		{
			auto addr = GetProcAddress(func_name.c_str());
			if (!addr)
				return nullptr;
			func_map_.insert(std::make_pair(func_name, addr));
			it = func_map_.find(func_name);
		}
        std::function<FType> ftyped =
            reinterpret_cast<FType*>(it->second);
		return ftyped;
	}

	template <typename FType, typename... Args>
	typename std::result_of<std::function<FType>(Args...)>::type ExcecuteFunc(const std::string& funcName, Args&&... args)
	{
		auto f = GetFunction<FType>(funcName);
		if (f == nullptr)
		{
	        std::string s = "can not find this function " + funcName;
			throw utils::Error(s);
		}

		return f(std::forward<Args>(args)...);
	}



private:

#ifdef _WIN32
    typedef HMODULE handle_t;
	std::map<std::string, void*> func_map_;
#else
    typedef void* handle_t;
	std::map<std::string, void*> func_map_;
#endif

    handle_t handle_;
};

}
