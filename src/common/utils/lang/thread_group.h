#include <thread>
#include <mutex>
#include <list>
#include <memory>
#include <algorithm>
namespace utils
{
    //兼容boost::threadGroup
    //使用std::thread代替boost::thread,std::mutex代替boost::shared_mutex
    class threadGroup
    {
    private:
        threadGroup(threadGroup const&);
        threadGroup& operator=(threadGroup const&);
    public:
        threadGroup() {}
        ~threadGroup()
        {
            threads_.clear();
        }

        template<typename F>
        thread* CreateThread(F threadfunc)
        {
            std::lock_guard<std::mutex> guard(m);
            std::unique_ptr<std::thread> new_thread(new std::thread(threadfunc));
            threads_.push_back(new_thread.get());
            return new_thread.release();
        }

        void AddThread(std::thread* thrd)
        {
            if(thrd)
            {
                std::lock_guard<std::mutex> guard(m);
                threads_.push_back(thrd);
            }
        }

        void RemoveThread(std::thread* thrd)
        {
            std::lock_guard<std::mutex> guard(m);
            auto it=std::find(threads_.begin(), threads_.end(),thrd);
            if(it!=threads_.end())
                threads_.erase(it);
            /*
            for(auto it=threads_.begin(),end=threads_.end();it!=end;++it)
            {
                if (it->get() == thrd)
                    threads_.erase(it);
            }
            */
        }

        void JoinAll()
        {
            std::lock_guard<std::mutex> guard(m);
            for(auto it=threads_.begin(),end=threads_.end();it!=end;++it)
            {
                (*it)->join();
            }
        }

        size_t Size() const
        {
            std::lock_guard<std::mutex> guard(m);
            return threads_.size();
        }

    private:
        // list<std::shared_ptr<std::thread>> threads_;
        std::list<std::thread*> threads_;
        mutable std::mutex m;
    };
}
