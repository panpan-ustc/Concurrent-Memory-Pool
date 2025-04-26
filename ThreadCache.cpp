#include "ThreadCache.h"
#include "CentralCache.h"


//size = alignsize
void* ThreadCache::FetchFromCentralCache(size_t index, size_t size)
{
	// 慢开始反馈调节算法--批量的给内存对象个数(batchNum --- thread cache一次批量申请的数量)
	// 1.最开始不会一次向中心缓存要太多，因为要太多可能用不完
	// 2.如果不断有这个size大小内存的需求，那么batchNum就会不断增长，直到上限
	// 3.size越大，一次向中心缓存要的 batchNum上限就越小，batchNum就越小
	// 4.size越小，一次向中心缓存要的 batchNum上限就越大，但是batchNum就会不断增大到这个上限
	// batchNum == Maxsize() ---> batchNum == NumMoveSize， 此时MaxSize不起作用
	size_t batchNum = min(_freelists[index].MaxSize(), SizeClass::NumMoveSize(size));


	//如果线程后面不断有size大小的内存对象的需求，对应映射的index号桶的MaxSize变量就会不断增大
	//直到增大到上限NumMoveSize(size)
	//_freelists[index].MaxSize() < NumMoveSize(size) <=> _freelists[index].MaxSize() == batchNum
	if (_freelists[index].MaxSize() == batchNum)
	{
		_freelists[index].MaxSize() += 1;
	}

	void* start = nullptr;
	void* end = nullptr;

	//从中心缓存中获取的实际内存对象的个数---actualNum, Central Cache 中的对应桶下的Span
	//不一定有批量个内存对象
	size_t actualNum = CentralCache::GetInstance()->FetchRangeObj(start, end, batchNum, size);
	
	//Central Cache 需要给线程的Thread Cache 至少分配有1个
	assert(actualNum >= 1);
	
	//将thread cache从中心缓存获取的内存对象的第一个对象返回给线程，其余的挂接到对应的自由链表中
	if (actualNum == 1)
	{
		assert(start == end);
		return start;
	}
	else
	{
		//将多个对象的第一个返回给线程, 其他的actualnum - 1个对象挂到Thread Cache的自由链表桶中
		void* second = NextObj(start);
		//将中心缓存分配给thread cache的第二个内存对象到最后一个内存对象组成的链表插入到自由链表中

		_freelists[index].PushRange(second, end, actualNum - 1);

		//将第一个内存对象直接分配给线程
		return start;
	}
}


void* ThreadCache::Allocate(size_t size)
{
	assert(size <= MAX_BYTES);
	//size + t - 1
	//得到按照208个桶对齐规则得到的对齐后的分配的内存对象的大小，字节数
	size_t alignBytes = SizeClass::Roundup(size);

	//计算所在桶号
	size_t index = SizeClass::Index(size);

	if (!_freelists[index].Empty())
	{
		return _freelists[index].Pop();
	}
	else
	{
		//该线程需要的内存对象所在的自由链表(ThreadCache)剩余容量不够，向CentralCache获取内存对象
		return ThreadCache::FetchFromCentralCache(index, alignBytes);
	}
}

//归还的内存对象大小一定是对齐后的内存对象大小
void ThreadCache::Deallocate(void* ptr, size_t size)
{
	assert(ptr);
	assert(size <= MAX_BYTES);

	//找出对应的自由链表桶编号
	size_t index = SizeClass::Index(size);
	//插入到对应的自由链表桶中
	_freelists[index].Push(ptr);

	/*
	1.线程释放内存对象时为了避免空闲的内存对象的堆积，需要将一定数量的内存对象释放给Central Cache
	  进一步释放给Page Cache，进行合并相邻span解决内存碎片化的问题，从而合并出大内存容量的span

	2.tcmalloc释放考虑了两个因素: (Thread Cache)中的链表长度 和 (Thread Cache)占据的总内存大小
	  tcmalloc会记录每一个线程的Thread Cache中空闲内存对象的总大小，并判断是否超出一定上限(2MB)，
	          超出就进行回收
	*/
	
	
	//如果自由链表中的空闲内存对象的个数超过它此时一次能批量申请的内存对象的个数，表示空闲内存对象太多了用不到
	//此时可以将这些空闲内存对象的个数释放给Central Cache(可以设计更复杂的判断归还条件的算法)
	if (_freelists[index].Size() >= _freelists[index].CurrentBatchNum())
	{
		ListTooLong(_freelists[index], size);
	}
}


//自由链表太长的时候，从自由链表中取出batchNum个内存对象还给Central cache
//不用全取完，因为线程可能在归还的过程中还会继续要内存
void ThreadCache::ListTooLong(Freelist& list, size_t size)
{
	void* start = nullptr;
	void* end = nullptr;

	list.PopRange(start, end, list.CurrentBatchNum());

	//从自由链表中取出一段链，这段链的最后是空，于是可以通过start将这段链尾插到对应的Central Cache的span中
	CentralCache::GetInstance()->ReleaseListToSpans(start, size);
}