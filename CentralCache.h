#pragma once
#include "Common.h"


//由于central cache占据的内存会很大且会被频繁使用，故使用单例模式在整个过程中只创建central cache类的一个实例
/*
单例模式：确保一个类只有一个实例对象并提供一个全局访问点(使用static修饰，保证该实例对象在在静态区创建，
          会被共享)来访问这个实例。
实现方式:
饿汉式(静态式): 在类加载时就立即创建单例实例；
懒汉式(动态式): 在第一次使用时才创建实例，实现延迟加载(多线程情况下需要对创建实例的函数进行加锁)

对于一些频繁使用且创建成本较高(大内存对象)的对象，使用单例模式可以避免重复创建带来的性能开销，节省系统资源
*/
class CentralCache
{
public:
	//单例模式中需要定义一个公有成员函数来获得创建的单个实例对象
	//由于采用的是饿汉模式，故该类的单例在类加载时就已经创建出来，即main函数执行前就已经创建
	//故不存在多线程竞争的问题，于是直接返回即可
	//而懒汉式是程序运行时才会创建单个实例对象，故在多线程下会存在创建多次的可能
	/*
	懒汉式:
	static CentralCache* GetInstance()
	{
		//可能多个线程同时判断_sInst为空
	    if(_sInst == nullptr)
		{
			//双重检查锁定
			std::lock_gurad<std::mutex>guard(_mutex);//上锁
			//第一次判断为空的线程获得锁，其他线程阻塞
			if (_sInst == nullptr)
			{
				_sInst = new Singleton();//类里面的成员函数是可以调用构造函数创建实例对象
			}
		}
	}
	*/
	static CentralCache* GetInstance()
	{
		return &_sInst;
	}


	//获取一个非空的Span
	Span* GetOneSpan(SpanList &list, size_t byte_size);


	//从中心缓存获取一定数量的且是thread cache所需的内存对象给thread cache
	//其中batchNum是thread cache所要的内存对象的个数，每个对象的大小是byte_size大小
	//start是函数通过引用返回thread cache给中心缓存从自己的桶取出的若干内存对象组成的链表的头指针
	//end是函数通过引用返回thread cache给中心缓存从自己的桶取出的若干内存对象组成的链表的尾指针
	//函数的返回值是这次实际分配给thread cache的内存对象的个数，因为有可能中心缓存的哈希桶当中所剩的内存对象不够thread cache所需的n个
	size_t FetchRangeObj(void* &start, void* &end, size_t batchNum, size_t byte_size);

	//将Thread Cache 中自由链表中的一定数量的对象释放到span跨度
	void ReleaseListToSpans(void* start, size_t byte_size);


private:
	//Central Cache中的桶定义
	//CentralCache初始化列表不给任何内容，编译器会自动调用spanlist的初始化函数对spanlist类的对象进行初始化
	
	//第index号桶的Span大小 = 一次分配给线程的最大内存对象批量数 * 对应index桶下的内存对象大小
	SpanList _spanlists[NFREELISTS];


private:
	//将Central Cache的构造函数设置成私有成员函数，防止外部使用默认构造函数来创建CentralCache类的对象
	//因此保证了单例模式
	CentralCache()
	{}

	//使用“= delete”语法，明确的告知编译器不允许使用拷贝构造函数来创建CentralCache类的对象
	//防止拿到对象后使用拷贝构造函数进行创建CentralCache类的对象
	CentralCache(const CentralCache&) = delete;

	static CentralCache _sInst;//创建的单一实例对象无法被修改
	
	//类锁，用于线程安全
	//static std::mutex _mutex;
};