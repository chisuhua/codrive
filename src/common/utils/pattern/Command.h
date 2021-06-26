#pragma once

#include <functional>
#include <type_traits>
#include <map>
#include <string>
#include <memory>

using namespace std;
namespace utils
{

class Command
{
public:
    virtual ~Command() {}
};

/*
template<typename Ret = void>
struct Command
{
private:
	std::function < Ret()> m_f;

public:
	//func wrapp for callable
	template< class F, class... Args, class = typename std::enable_if<!std::is_member_function_pointer<F>::value>::type>
	void Wrap(F && f, Args && ... args)
	{
		m_f = [&]{return f(args...); };
	}

	//func wrap for constant member function
	template<class R, class C, class... DArgs, class P, class... Args>
	void Wrap(R(C::*f)(DArgs...) const, P && p, Args && ... args)
	{
		m_f = [&, f]{return (*p.*f)(args...); };
	}

	//func wrap for non-constant member function
	template<class R, class C, class... DArgs, class P, class... Args>
	void Wrap(R(C::*f)(DArgs...), P && p, Args && ... args)
	{
		m_f = [&, f]{return (*p.*f)(args...); };
	}

	Ret Excecute()
	{
		return m_f();
	}
};
*/


template <class... Args>
class FuncCommand : public Command
{
    typedef std::function<void(Args...)> FuncType;
    FuncType m_f;
public:
    FuncCommand() {}
    FuncCommand(FuncType f) : m_f(f) {}
    FuncType get() {return m_f;}
    void Execcute(Args... args) { if (m_f) m_f(args...); }
};


class CommandManager
{
    typedef shared_ptr<Command> CommandPtr;
    typedef map<string, CommandPtr> FMap;

public :

    template <class T>
    void add(string name, const T& cmd)
    {
        fmap_.insert(pair<string, CommandPtr>(name, CommandPtr(new T(cmd))));
    }

    Command* get(string name)
    {
        FMap::const_iterator it = fmap_.find(name);
        if(it != fmap_.end())
        {
            return it->second.get();
        }
        return nullptr;
    }


    template <class... ArgTypes>
    void execute(string name, ArgTypes... args)
    {
        typedef FuncCommand<ArgTypes...> FuncCommandType;
        FMap::const_iterator it = fmap_.find(name);
        if(it != fmap_.end())
        {
            FuncCommandType* c = dynamic_cast<FuncCommandType*>(it->second.get());
            if(c)
            {
              (*c)(args...);
            }
        }
    }

private :
    FMap fmap_;
};

}

/* usage
void x() {}
void y(int ) {}
void main() {
    CommandManager m;
    m.add("print", FuncCommand<>(x));
    m.add("print1", FuncCommand<int>(y));
    m.execute("print");
    m.execute("print1", 1);
}
*/
