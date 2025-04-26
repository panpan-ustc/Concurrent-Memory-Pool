#include "Common.h"
#include "ConcurrentAlloc.h"
#include "CentralCache.h"
#include <vector>

void Alloc1()
{
	for (size_t i = 0; i < 5; ++i)
	{
		void* ptr = ConcurrentAlloc(6);
	}
}

void Alloc2()
{
	for (size_t i = 0; i < 5; ++i)
	{
		void* ptr = ConcurrentAlloc(7);
	}
}

inline void TLSTest()
{
	//各个线程被交替调用
	//创建一个线程t1,让他执行Alloc1函数的内容，传递函数指针(函数名)
	std::thread t1(Alloc1);

	//创建一个线程t2,让他执行Alloc1函数的内容，传递函数指针(函数名)
	std::thread t2(Alloc2);

	//主线程调用join函数阻塞等待t1线程执行完毕
	//join函数还会负责相关线程执行完成后(调用join函数的线程执行前),线程占用的栈空间和线程局部存储资源的回收，避免内存泄露
	t1.join();

	//主线程调用t2线程的join函数，等待t2线程执行完毕
	t2.join();
}

inline void TestConcurrentAlloc1()
{

	//假设当前的批量是b，则已经申请了 1 + 2 + ... + (b - 1) = b(b-1)/2
	//当前线程总共向Thread Cache 申请的次数是 [(b-1)(b-2)/2 + 1, b(b-1)/2]
	//由于每次 Thread Cache 归还b块内存对象 给 Central Cache，
	//假设已经还了k次，则共还了k*b块(k ∈ N); 故 k*b ≤ b(b-1)/2 => k ≤ (b-1)/2
	//并且假设第 k + 1次是不会发生的，即k次归还后剩下的数量不足以发生第 k+1 次归还
	//于是 k + 1 > (b-1)/2， 即k = [(b-1)/2]((b-1)/2的下取整)
	//当k = (b-1)/2 => b = 2*k + 1;
	//即b为奇数时，一定存在k使得 k = (b-1)/2, 此时一定可以正好全还完时，Central Cache中span中的计数器减为0
	size_t begin = clock();
	void* p1 = ConcurrentAlloc(6);
	void* p2 = ConcurrentAlloc(8);
	void* p3 = ConcurrentAlloc(1);
	void* p4 = ConcurrentAlloc(7);
	void* p5 = ConcurrentAlloc(8);
	void* p6 = ConcurrentAlloc(5);
	void* p7 = ConcurrentAlloc(4);

	ConcurrentFree(p1);
	ConcurrentFree(p2);
	ConcurrentFree(p3);
	ConcurrentFree(p4);
	ConcurrentFree(p5);
	ConcurrentFree(p6);
	ConcurrentFree(p7);

	size_t end = clock();
	cout << (end - begin) << endl;
}

inline void TestConcurrentAlloc2()
{
	size_t begin = clock();
	void* p1 = malloc(6);
	void* p2 = malloc(8);
	void* p3 = malloc(1);
	void* p4 = malloc(7);
	void* p5 = malloc(8);
	void* p6 = malloc(5);
	void* p7 = malloc(4);

	free(p1);
	free(p2);
	free(p3);
	free(p4);
	free(p5);
	free(p6);
	free(p7);
	size_t end = clock();
	cout << (end - begin) << endl;
}

void Testmalloc()
{
	std::vector<void*>p;
	size_t begin1 = clock();
	for (size_t i = 1; i <= 10000; i++)
	{
		p.push_back(malloc(i));
		//cout << p[i - 1] << endl;
	}

	while (!p.empty())
	{
		void* p1 = p.back();
		size_t size = p.size() - 1;
		p.pop_back();
		free(p1);
	}
	size_t end1 = clock();
	cout << "malloc:  " << (end1 - begin1) << endl;
}


inline void MultiThreadAlloc1()
{
	std::vector<void*>ptr1;
	for (size_t i = 0; i < 7; i++)
	{
		void* ptr = ConcurrentAlloc(6);
		ptr1.push_back(ptr);
	}

	for (auto p : ptr1)
	{
		ConcurrentFree(p);
	}
}

inline void MultiThreadAlloc2()
{
	std::vector<void*>ptr2;
	for (size_t i = 0; i < 7; i++)
	{
		void* ptr = ConcurrentAlloc(7);
		ptr2.push_back(ptr);
	}

	for (auto p : ptr2)
	{
		ConcurrentFree(p);
	}
}

inline void TestMultiThread()
{
	std::thread t1(MultiThreadAlloc1);


	std::thread t2(MultiThreadAlloc2);

	t1.join();
	t2.join();
}

//解决线程申请的内存对象大小 > 256KB(32页)的问题
/*
1.申请的内存对象大小 ∈[32*8KB + 1, 128*8KB], 直接向 Page Cache 申请
2.申请的内存对象大小 > 128页， 直接向堆申请
*/
void BigAlloc()
{
	void* p1 = ConcurrentAlloc(257 * 1024);

	ConcurrentFree(p1);

	void* p2 = ConcurrentAlloc(129 * 8 * 1024);

	ConcurrentFree(p2);

}


int main()
{
	TestConcurrentAlloc1();

	TestConcurrentAlloc2();

	TestMultiThread();

	BigAlloc();
	return 0;
}