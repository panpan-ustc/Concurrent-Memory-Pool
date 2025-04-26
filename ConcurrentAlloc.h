#pragma once
#include "Common.h"
#include "ThreadCache.h"
#include "PageCache.h"


/*ÿ���߳����Լ���ThreadCache�����ڴ�����黹�ڴ����ʱ����ֱ�ӷ����Լ���ThreadCache����
��ȥ����Allocate������Deallocate�������������һ����װ�������������߳�ֱ�ӵ��÷�װ��Ľӿڼ���*/
static inline void* ConcurrentAlloc(size_t bytes_size)
{
	if (bytes_size > MAX_BYTES)
	{
		//���㳬�� 256KB ���ڴ��С�Ķ���ֵ
		size_t alignBytes = SizeClass::Roundup(bytes_size);

		//����ö���ֵ(B)��Ӧ��ҳ���Ƕ���
		size_t kpage = alignBytes >> PAGE_SHIFT;

		PageCache::GetInstance()->_pagemutex.lock();

		//С�ڵ��� 128ҳ �Ĵ��ڴ� ����ֱ���� Page Cache����
		Span* span = PageCache::GetInstance()->NewSpan(kpage);

		//���볬�� 256KB ���ڴ�ʱ����ʱ��õ���span�����ǲ����ڴ�����
		//���ǿ��Խ���һ��� span ����� objsize��Ϊ �䱾�� ������ڴ��С
		span->_objSize = bytes_size;
		
		PageCache::GetInstance()->_pagemutex.unlock();
		
		//�õ����뵽�Ĵ��ڴ����ʼ��ַ
		void* ptr = (void*)(span->_PageId << PAGE_SHIFT);

		return ptr;
	}


	//ͨ��TLS ÿ���̻߳���Լ���������ThreadCache����
	if (pTLSThreadCache == nullptr)
	{
		//����ɾ�̬���󣬷�ֹ����ʱ�����ض�������
	    static ObjectPool<ThreadCache>ThreadCachePool;
		
		//pTLSThreadCache = new ThreadCache;
		pTLSThreadCache = ThreadCachePool.New();
	}

	//��ȡ�̵߳��̺߳��Լ�pTLSThreadCacheָ��
	//cout << std::this_thread::get_id() << ":" << pTLSThreadCache << endl;
	
	
	return pTLSThreadCache->Allocate(bytes_size);
}

static inline void ConcurrentFree(void* ptr)
{
	Span* span = PageCache::GetInstance()->MapObjectToSpan(ptr);

	size_t size = span->_objSize;
	if (size > MAX_BYTES)
	{
		//����Page Cache�е�Ͱ��Ҫ����
		PageCache::GetInstance()->_pagemutex.lock();
		PageCache::GetInstance()->ReleaseSpanToPageCache(span);
		PageCache::GetInstance()->_pagemutex.unlock();
	}
	
	else
	{
		assert(pTLSThreadCache);

		//�̹߳黹�ڴ����
		pTLSThreadCache->Deallocate(ptr, size);
	}
}