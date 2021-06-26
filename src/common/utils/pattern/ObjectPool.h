#pragma once

#include <string>
#include <functional>
#include <tuple>
#include <map>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <list>

#include "utils/lang/error.h"
#include "utils/lang/lang.h"
#include "utils/lang/debug.h"
#include "utils/lang/string.h"
#include "any.h"

// ObjectPool store the shared_ptr to object
//    * when shared_ptr 's use_count is 1, it mean this ObjectPool is only owner, mean this object  is free object
//       * the free object 's use_count is 1
//    * client can
//       * create object, create new object and use_count is 1
//       * add object to ObjectPool
//       * acquire object, get fresh object from ObjectPool's free list or create new object if free list is empty
//          * by default, the use_count is 2 since both client and ObjectPool's shared_ptr
//       * release object, return object to  free list
//       * get object, it use instance_id to get object from used list, the use_count is +1

namespace utils {

    using hash_t = uint64_t;

const int MaxObjectNum = 10;

class ObjectID {
public:
    ObjectID(std::type_index ti,  std::string& name)
        :type_idx_( ti )
        ,name_( name )
    {
		hash_ = std::hash<std::string>()(name);
    }

    std::type_index type_idx_;
    std::string name_;
    size_t      hash_;


    void Print() const
    {
        printf( "\"%s\"", name_.c_str() );
    }

    // equal
    bool Compare( const ObjectID &other ) const
    {
        if( hash_ != other.hash_ ) return false;
        if( type_idx_ != other.type_idx_ ) return false;
        return ( name_.compare( other.name_ ) == 0 );
    }
};

static inline
ObjectID GetObjectID( std::type_index ti, std::string& str )
{
    ObjectID ret(ti, str);
    return ret;
}

static inline
ObjectID GetObjectID( std::type_index ti, const char *str )
{
	std::string s = str;
    return GetObjectID(ti, s);
}

static inline
ObjectID GetObjectID( const ObjectID &id )
{
    return id;
}

class Object {
public:
//     Object() {};

    Object(
            const ObjectID id,
            any any_obj,
            bool valid
            )
        : id_ (id),
          any_obj_ (any_obj),
          valid_ (valid)
    {}

    ~Object()
    {}

    std::type_index getTypeIdex()
    {
        return any_obj_.getTypeIndex();
    }

    any& getanyObject()
    {
		return any_obj_;
	}

    ObjectID id_;
    any any_obj_;
    bool valid_;
};

class HashFunc {
public:
    hash_t operator() ( const Object  *O ) const
    {
        return (O->id_.hash_) ^ std::hash<std::type_index>()(O->id_.type_idx_);
    }
};

class CmpFunc {
public:
    // return true if they are equal
    bool operator() ( const Object *left,
                      const Object *right ) const
    {
        return left->id_.Compare( right->id_ );
    }
};



class ObjectPool
{
public:
    class Error : public utils::Error
    {
    public:
        Error(const std::string &message) : utils::Error(message)
        {
            AppendPrefix("ObjectPool");
        }
    };

    typedef std::unordered_set<Object*, HashFunc, CmpFunc> UsedObjSet;
    typedef std::map<std::type_index, std::list<Object*>> FreeObjMap;

    template<typename T, typename... Args>
    using Constructor = std::function<std::shared_ptr<T>(Args...)>;
public:

    ObjectPool()
		:need_clear_(false)
		,used_objs_( new UsedObjSet() )
		,free_objs_( new FreeObjMap() )
    {
        debug_.setPath("stdout");
        debug_.setPrefix("ObjectPool:");
    }

    ~ObjectPool()
    {
        need_clear_ = true;
    }

    //默认创建多少个对象
    template<typename T, typename... Args>
    void Create(Args... args)
    {
        auto obj =  std::make_shared<T>(args...);
        Add(obj);
    }

    template<typename T >
    bool Add(std::shared_ptr<T> obj, const char* obj_str = "") {
        // any any(obj);
        std::type_index ti = std::type_index(typeid(T));
        auto Oid = GetObjectID(ti, obj_str);

		Object* O = new Object(Oid, obj, true);

        UsedObjSet::iterator it = used_objs_->find( O );

        if (it != used_objs_->end()) {
            throw Error(utils::fmt("Object %s of type %s is is ready in Pool\n",
                     obj_str,
                     typeid(T).name()));
			return false;
        }

		used_objs_->insert(O);
		return true;
    }

	template<typename T >
    bool Add(std::shared_ptr<T> obj, std::string& obj_str = "") {
    	return Add(obj, obj_str.c_str());
	}

    template<typename T >
    bool Add(std::shared_ptr<T> obj, char * obj_str = "" ) {
        return Add(obj, (const char*)obj_str);
	}

    template<typename T >
    UsedObjSet::iterator GetObjectIter(const char* obj_str = "") {
        std::type_index ti = std::type_index(typeid(T));
        auto Oid = GetObjectID(ti, obj_str);

		Object* O = new Object(Oid, nullptr, false);

        UsedObjSet::iterator it = used_objs_->find( O );

        if (it == used_objs_->end()) {
            throw Error(utils::fmt("GetShared Object %s of type %s is not found Pool\n",
                     obj_str,
                     typeid(T).name()));
        }
		return it;
	}

    template<typename T >
    std::shared_ptr<T> GetShared(const char* obj_str)
    {
		Object* O = *(GetObjectIter<T>(obj_str));

		return O->getanyObject().anyCast<std::shared_ptr<T>>();
	}

    template<typename T >
    std::shared_ptr<T> GetShared(std::string& obj_str)
    {
        return GetShared<T>(obj_str.c_str());
    }


    template<typename T, typename... Args>
    std::shared_ptr<T> Acquire(const char* obj_str, Args... args)
    {
        std::type_index ti = std::type_index(typeid(T));
        auto Oid = GetObjectID(ti, obj_str);

        auto it = free_objs_->find(ti);
        if (it != free_objs_->end() && !(it->second).empty()) {
            debug_ << utils::fmt("Acquire called: Recycle object from free_objs\n");
			Object* O = (it->second).front();
        	auto ptr = O->getanyObject().anyCast<std::shared_ptr<T>>();
	        if (sizeof...(Args)>0)
            	*ptr.get() = std::move(T(args...));
            used_objs_->insert(O);
			(it->second).pop_front();
			return O->any_obj_.anyCast<std::shared_ptr<T>>();
		} else {
            debug_ << utils::fmt("Acquire called: Create new object \n");
			any any_obj ( std::make_shared<T>(args...));
			Object* O = new Object(Oid, any_obj, true);
			used_objs_->insert(O);
			return any_obj.anyCast<std::shared_ptr<T>>();
		}
    }

    template<typename T, typename... Args>
    std::shared_ptr<T> Acquire(std::string& obj_str, Args... args) 
    {
        return Acquire<T>(obj_str, std::forward<Args>(args)...);
    }


    template<typename T >
    bool Release(const char* obj_str = "") 
    {
		auto it = GetObjectIter<T>(obj_str);
		Object* O = *it;

		auto obj = O->getanyObject().anyCast<std::shared_ptr<T>>();
        debug_ << utils::fmt("Release called, use_count is %ld \n", obj.use_count());
		if (obj.use_count() <= 2)
		{
        	std::type_index ti = std::type_index(typeid(T));
			(*free_objs_)[ti].push_back(O);
			used_objs_->erase(it);
			return true;
		}
		return false;
	}

    template<typename T >
    bool Release(std::string& obj_str = "") 
    {
        return Release<T>(obj_str.c_str());
    }

private:

    bool need_clear_;
    // The actual ID-to-element lookup map
    // used by Get() to find things.
    UsedObjSet *used_objs_;
    FreeObjMap *free_objs_;

    utils::Debug debug_;

};

};

