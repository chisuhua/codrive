#pragma once
#include <stdexcept>
namespace utils {

template <typename T>
class Singleton
{
public:
    template<typename... Args>
    static T* Instance(Args&&... args)
    {
        if(instance_==nullptr)
        instance_ = new T(std::forward<Args>(args)...);

        return instance_;
    }

    static T* getInstance()
    {
        if (instance_ == nullptr)
        throw std::logic_error("the instance is not init, please initialize the instance first");

        return instance_;
    }

    static void DestroyInstance()
    {
        delete instance_;
        instance_ = nullptr;
    }

protected:
    Singleton(void) {};
    virtual ~Singleton(void) {};

private:
    Singleton(const Singleton&);
    Singleton& operator=(const Singleton&);

private:
    static T* instance_;
};

template <class T> T*  Singleton<T>::instance_ = nullptr;
}
