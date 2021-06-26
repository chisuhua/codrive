
#include "utils/lang/error.h"

#include "DynamicLibrary.h"


namespace utils {

DynamicLibrary::DynamicLibrary(void)
    : handle_(nullptr)
{}

DynamicLibrary::DynamicLibrary(const std::string& name)
    : handle_(nullptr)
{
    Load(name);
}

DynamicLibrary::~DynamicLibrary(void)
{
    UnLoad();
}

bool DynamicLibrary::Load(const std::string& name)
{
#ifdef _WIN32
    handle_ = LoadLibraryA(name.c_str());
#else
    handle_ = dlopen(name.c_str(), RTLD_LAZY);
#endif

	// handle_ = LoadLibraryA(dllPath.data());
	if (handle_ == nullptr)
	{
		printf("LoadLibrary failed, %s : %s\n", name.c_str(), dlerror());
		return false;
	}

	return true;
}

// start loading based on the vector of library names.  First success
// terminates the loading attempts.  Needed for CentOS6 and OpenCL.
// The Catalyst installer (at least in Catalyst 12.1) creates libOpenCL.so.1
// but no symbolic link to libOpenCL.so.  This symbolic link exists on other
// distributions where multi-step repackaging is required (build a .deb, run it)
bool DynamicLibrary::Load(const std::vector<std::string>& names)
{
    bool    loaded = false;

    for (std::vector<std::string>::const_iterator it = names.begin(); it != names.end(); it++)
    {
        loaded = Load(*it);

        if (loaded)
        {
            return loaded;
        }
    }

    return loaded;
}

bool DynamicLibrary::UnLoad()
{
	if (handle_ == nullptr)
		return true;

#ifdef _WIN32
	auto b = FreeLibrary(handle_);
#else
    auto b = dlclose(handle_);
#endif
	if (!b)
		return false;

	handle_ = nullptr;
	return true;
}

void* DynamicLibrary::GetProcAddress(const std::string& name)
{
    if (handle_ != NULL)
    {
#ifdef _WIN32
        return ::GetProcAddress(handle_, name.c_str());
#else
        return dlsym(handle_, name.c_str());
#endif
    }
    else
    {
        return nullptr;
    }
}


}
