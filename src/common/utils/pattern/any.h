#pragma once
#include <memory>
#include <typeindex>
#include <exception>
#include <iostream>

namespace utils {
struct any
{
	any(void) : type_index_(std::type_index(typeid(void))) {}
	any(const any& that) : ptr_(that.Clone()), type_index_(that.type_index_) {}
	any(any && that) : ptr_(std::move(that.ptr_)), type_index_(that.type_index_) {}

	//创建智能指针时，对于一般的类型，通过std::decay来移除引用和cv符，从而获取原始类型
	template<typename U, class = typename std::enable_if<!std::is_same<typename std::decay<U>::type, any>::value, U>::type> any(U && value) : ptr_(new Derived < typename std::decay<U>::type>(std::forward<U>(value))),
		type_index_(std::type_index(typeid(typename std::decay<U>::type))){}

	bool IsNull() const { return !bool(ptr_); }

	template<class U> bool Is() const
	{
		return type_index_ == std::type_index(typeid(U));
	}

	//将any转换为实际的类型
	template<class U>
	U& anyCast()
	{
		if (!Is<U>())
		{
			std::cout << "can not cast " << typeid(U).name() << " to " << type_index_.name() << std::endl;
			throw std::logic_error{"bad cast"};
		}

		auto derived = dynamic_cast<Derived<U>*> (ptr_.get());
		return derived->m_value;
	}

	any& operator=(const any& a)
	{
		if (ptr_ == a.ptr_)
			return *this;

		ptr_ = a.Clone();
		type_index_ = a.type_index_;
		return *this;
	}

    std::type_index getTypeIndex()
    {
        return type_index_;
    }

private:
	struct Base;
	typedef std::unique_ptr<Base> BasePtr;

	struct Base
	{
		virtual ~Base() {}
		virtual BasePtr Clone() const = 0;
	};

	template<typename T>
	struct Derived : Base
	{
		template<typename U>
		Derived(U && value) : m_value(std::forward<U>(value)) { }

		BasePtr Clone() const
		{
			return BasePtr(new Derived<T>(m_value));
		}

		T m_value;
	};

	BasePtr Clone() const
	{
		if (ptr_ != nullptr)
			return ptr_->Clone();

		return nullptr;
	}

	BasePtr ptr_;
	std::type_index type_index_;
};
}
