#pragma once
#include "Common.h"
#include "ThreadCache.h"
#include "PageCache.h"


/*每个线程向自己的ThreadCache申请内存对象或归还内存对象时，不直接访问自己的ThreadCache对象
再去调用Allocate函数或Deallocate函数，在这里进一步封装这两步操作，线程直接调用封装后的接口即可*/
static inline void* ConcurrentAlloc(size_t bytes_size)
{
	if (bytes_size > MAX_BYTES)
	{
		//计算超过 256KB 的内存大小的对齐值
		size_t alignBytes = SizeClass::Roundup(bytes_size);

		//计算该对齐值(B)对应的页数是多少
		size_t kpage = alignBytes >> PAGE_SHIFT;

		PageCache::GetInstance()->_pagemutex.lock();

		//小于等于 128页 的大内存 申请直接向 Page Cache申请
		Span* span = PageCache::GetInstance()->NewSpan(kpage);

		//申请超过 256KB 的内存时，这时候得到的span下面是不挂内存对象的
		//于是可以将这一类的 span 下面的 objsize设为 其本身 管理的内存大小
		span->_objSize = bytes_size;
		
		PageCache::GetInstance()->_pagemutex.unlock();
		
		//得到申请到的大内存的起始地址
		void* ptr = (void*)(span->_PageId << PAGE_SHIFT);

		return ptr;
	}


	//通过TLS 每个线程获得自己的无锁的ThreadCache对象
	if (pTLSThreadCache == nullptr)
	{
		//定义成静态对象，防止链接时出现重定义问题
	    static ObjectPool<ThreadCache>ThreadCachePool;
		
		//pTLSThreadCache = new ThreadCache;
		pTLSThreadCache = ThreadCachePool.New();
	}

	//获取线程的线程号以及pTLSThreadCache指针
	//cout << std::this_thread::get_id() << ":" << pTLSThreadCache << endl;
	
	
	return pTLSThreadCache->Allocate(bytes_size);
}

static inline void ConcurrentFree(void* ptr)
{
	Span* span = PageCache::GetInstance()->MapObjectToSpan(ptr);

	size_t size = span->_objSize;
	if (size > MAX_BYTES)
	{
		//访问Page Cache中的桶需要加锁
		PageCache::GetInstance()->_pagemutex.lock();
		PageCache::GetInstance()->ReleaseSpanToPageCache(span);
		PageCache::GetInstance()->_pagemutex.unlock();
	}
	
	else
	{
		assert(pTLSThreadCache);

		//线程归还内存对象
		pTLSThreadCache->Deallocate(ptr, size);
	}
}