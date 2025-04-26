#pragma once
#include "Common.h"
#include "ObjectPool.h"

class ThreadCache
{
public:
	//申请和释放内存对象
	void* Allocate(size_t size);


	//释放时需要size参数确定归还内存所在的哈希桶
	//每个哈希桶对应一个自由链表
	void Deallocate(void* ptr, size_t size);

	//从中心缓存获取内存对象(index --需要的内存对象所在哈希桶的编号，size --分配对齐后的内存对象的大小)
	void* FetchFromCentralCache(size_t index, size_t size);

	// 释放对象时，链表过长时，回收内存回到中心缓存,size是list中内存对象的大小
	void ListTooLong(Freelist& list, size_t size);



private:
	//每个自由链表用一个类来实现，这个类封装了每个自由链表的头指针以及相关的插入、弹出、判空等操作
	Freelist _freelists[NFREELISTS];
};

//TLS - thread local storage
/*线程局部存储（TLS），是一种变量的存储方法，这个变量在它所在的线程内是全局可访问的，
但是不能被其他线程访问到，这样就保持了数据的线程独立性。而熟知的进程全局变量，
是所有线程都可以访问的，这样就不可避免需要锁来控制，增加了控制成本和代码复杂度。*/

/*使用TLS可以避免两个线程同时需要访问自己的ThreadCache时，产生的对ThreadCache数组同时访问带来的一些变量
的锁竞争问题，因为ThreadCache数组属于进程，而进程的数据或者代码是各个线程共享的*/

//windows下TLS 的实现
//每个线程独享自己的ThreadCache对象; pTLSTheadCache是指向自己的ThreadCache对象的指针
//初始为空，线程创建后都会有自己的一个pTLSThreadCache指针，申请后创建该对象

//static保证修饰的全局变量只在当前文件可见,可以保证头文件内定义的全局变量或者函数在不同的cpp文件中展开后，
//在链接时不会出现变量或者函数重定义的问题
static _declspec(thread) ThreadCache* pTLSThreadCache = nullptr;