#include <iostream>
#include <gtest/gtest.h>
#include "utils/pattern/any.h"
#include "utils/pattern/ObjectPool.h"

using namespace utils;
using namespace std;
TEST(PatternTest, Testany) {
    any n;
    auto r = n.IsNull();
    EXPECT_TRUE(r);

    std::string s1 = "hello";
    n = s1;
    n = "world";

    // EXPECT_FALSE(n.anyCast<int>());

    any n1 = 1;
    EXPECT_TRUE(n1.Is<int>());
}


struct AT
{
    AT(){}

    AT(int a, int b) :m_a(a), m_b(b){}

    void Fun()
    {
        cout << m_a << " " << m_b << endl;
    }

    int m_a = 0;
    int m_b = 0;
};

struct BT
{
    void Fun()
    {
        cout << "from object b " << endl;
    }
};

TEST(PatternTest, TestObjectPool) {
    ObjectPool pool;

    auto s1 = std::make_shared<std::string>("Hello");
    pool.Add(s1, "s1");

    auto s2 = std::make_shared<std::string>("Hello World");
    pool.Add(s2, "s2");

    auto get_s1 = pool.GetShared<std::string>("s1");
    auto get_s2 = pool.GetShared<std::string>("s2");

    EXPECT_EQ(get_s1, s1);
    EXPECT_NE(get_s2, s1);


    {
        auto i1 = std::make_shared<int>(1);
        std::string obj_i1 = "i1";
        pool.Add(i1, obj_i1);
    }

    auto result = pool.Release<int>("i1");
    EXPECT_TRUE(result);

    auto i2 = pool.Acquire<int>("i2");
    auto get_i2 = pool.GetShared<int>("i1");
    EXPECT_EQ(get_i2, i2);

    auto i3 = pool.Acquire<int>("i3");
};


TEST(PatternTest, TestObjectPoolFail) {
    ObjectPool pool;
    try
    {
        auto get_s1 = pool.GetShared<std::string>("s1");
        cout << get_s1 << endl;
    }
    catch(ObjectPool::Error)
    {
        cout << "Get expected result" << endl;
    }
}
/*
TEST(PatternTest, TestObjectPool) {
{
    ObjectPool pool;
    pool.Create<AT>(2);
    pool.Create<BT>(2);
    pool.Create<AT, int, int>(2);

    {
        auto p = pool.Get<AT>();
        p->Fun();
    }
    
    auto pb = pool.Get<BT>();
    pb->Fun();

    auto p = pool.Get<AT>();
    p->Fun();

    int a = 5, b = 6;
    auto p2 = pool.Get<AT>(a, b);
    p2->Fun();

    auto p3 = pool.Get<AT>(3, 4);
    p3->Fun();

    {
        auto p4 = pool.Get<AT>(3, 4);
        p4->Fun();
    }
    auto p4 = pool.Get<AT>(7, 8);
    p4->Fun();
}
*/

