#pragma once
#include "Common.h"

//判断程序运行环境
#ifdef _WIN32
#include <Windows.h>
#else 
	//linux下头文件
#endif

//定长内存池---单一对象池
#define K 1024
const size_t PreapplyBytes = 8 * 1024 * K;//预申请内存大小
const size_t PageSize = 8 * K;//页大小

template<class T>
class ObjectPool
{
public:
	T* New()
	{
		T* obj = nullptr;
		//优先把还回来的内存块对象再次重复利用---头删法
		if (_freelist)
		{
			//对void**对象解引用，得到的是一块void*大小的内存，即void*类型的对象
			void* next = *(void**)_freelist;
			obj = (T*)_freelist;
			_freelist = next;
		}
		else
		{
			//解决预申请的剩余内存不足以分配给一个对象的问题
		    //当该场景发生时，就直接将上一次申请的剩余的最后一点内存丢弃，重新申请
			if (_remainBytes < sizeof(T))
			{
				//_memory = (char*)malloc(PreapplyBytes);
				_memory = (char*)SystemAlloc(PreapplyBytes >> 13);//等价于除以2^(13)换算出页数
				_remainBytes = PreapplyBytes;

				//申请失败，抛异常
				if (_memory == nullptr)
				{
					throw std::bad_alloc();
				}
			}

			obj = (T*)_memory;

			//解决申请内存的对象大小不足4或8字节的问题
			//不足会导致在对应系统下归还内存时该内存块头部没有足够的大小存储next指针
			//解决方法是如果申请内存池中内存空间的对象过小，直接强行分配给它对应一个指针大小的空间
			size_t objsize = sizeof(T);
			if (sizeof(T) < sizeof(void*))
			{
				 objsize = sizeof(void*);
			}
			_memory += objsize;
			_remainBytes -= objsize;
		}

		//定位new，显示调用T的构造函数初始化对象
		new(obj)T;

		return obj;
	}

	void Delete(T* obj)
	{
		//显示调用析构函数清理对象
		obj->~T();

		//上述的逻辑判断可以采用同一句代码实现,不需要特判链表初始的情况
		//头插
		*(void**)obj = _freelist;
		_freelist = obj;


		//删除一个对象，管理归还的内存的思路就是使用一个自由链表
		//将归还的内存的前4或8(根据系统而定)字节保存链表中下一块内存的指针，归还时使用头插法插入到链表中
		//针对指针大小不确定的问题，可以采用强制类型转换成二级指针再解引用的方式解决
		/*if (_freelist == nullptr)
		{
			_freelist = obj;
			//将obj强转为指向int大小内存(对象)的指针
			//*(int*)obj = nullptr;//取类型为T的对象obj内存中的前int大小赋值为0，即存空指针

			//将obj强转为指向void*大小内存(对象/变量)的指针，void*大小在32位系统下是4B，64为系统下是8B
			//这里的强转为什么类型并不重要，只要是强转为一个二级指针都是合理的
			//因为一个指针大小在在32位系统下是4B，64为系统下是8B
			*(void**)obj = nullptr;
		}

		else
		{
			//头插
			*(void**)obj = _freelist;
			_freelist = obj;
		}*/
	}



private:
	//管理已申请的定长内存池
	char* _memory = nullptr;//指向内存池的指针
	size_t _remainBytes = 0;//内存池在切分过程中剩余字节数，用于判断内存池是否够用

private:
	//管理归还回来的内存对象，每个对象大小为sizeof(T)
	void* _freelist = nullptr;
	
};

//struct TreeNode
//{
//	int _val;
//	TreeNode* _left, * _right;
//
//	TreeNode()
//		:_val(0), _left(nullptr), _right(nullptr)
//	{}
//
//	~TreeNode()
//	{
//		_val = 0;
//		_left = _right = nullptr;
//	}
//};
//
//inline void TestObjectPool()
//{
//	//申请释放的轮次
//	const size_t Rounds = 1000000;
//
//	//每轮申请释放多少次
//	const int N = 100000;
//
//	
//	std::vector<TreeNode*> v1;
//	v1.reserve(N);
//
//	size_t begin1 = clock();
//	for (size_t j = 0; j < Rounds; ++j)
//	{
//		for (int i = 0; i < N; i++)
//		{
//			v1.push_back(new TreeNode);
//		}
//
//		for (int i = 0; i < N; i++)
//		{
//			delete v1[i];
//		}
//		v1.clear();
//	}
//
//	size_t end1 = clock();
//	
//
//	std::vector<TreeNode*>v2;
//	v2.reserve(N);
//	ObjectPool<TreeNode>TNPool;
//
//	size_t begin2 = clock();
//	for (size_t j = 0; j < Rounds; j++)
//	{
//		for (int i = 0; i < N; i++)
//		{
//			v2.push_back(TNPool.New());
//		}
//
//		for (int i = 0; i < N; i++)
//		{
//			TNPool.Delete(v2[i]);
//		}
//	}
//	size_t end2 = clock();
//
//	cout << (end1 - begin1) << endl;
//	cout << (end2 - begin2) << endl;
//}