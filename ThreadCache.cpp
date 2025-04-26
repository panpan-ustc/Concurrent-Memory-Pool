#include "ThreadCache.h"
#include "CentralCache.h"


//size = alignsize
void* ThreadCache::FetchFromCentralCache(size_t index, size_t size)
{
	// ����ʼ���������㷨--�����ĸ��ڴ�������(batchNum --- thread cacheһ���������������)
	// 1.�ʼ����һ�������Ļ���Ҫ̫�࣬��ΪҪ̫������ò���
	// 2.������������size��С�ڴ��������ôbatchNum�ͻ᲻��������ֱ������
	// 3.sizeԽ��һ�������Ļ���Ҫ�� batchNum���޾�ԽС��batchNum��ԽС
	// 4.sizeԽС��һ�������Ļ���Ҫ�� batchNum���޾�Խ�󣬵���batchNum�ͻ᲻�������������
	// batchNum == Maxsize() ---> batchNum == NumMoveSize�� ��ʱMaxSize��������
	size_t batchNum = min(_freelists[index].MaxSize(), SizeClass::NumMoveSize(size));


	//����̺߳��治����size��С���ڴ��������󣬶�Ӧӳ���index��Ͱ��MaxSize�����ͻ᲻������
	//ֱ����������NumMoveSize(size)
	//_freelists[index].MaxSize() < NumMoveSize(size) <=> _freelists[index].MaxSize() == batchNum
	if (_freelists[index].MaxSize() == batchNum)
	{
		_freelists[index].MaxSize() += 1;
	}

	void* start = nullptr;
	void* end = nullptr;

	//�����Ļ����л�ȡ��ʵ���ڴ����ĸ���---actualNum, Central Cache �еĶ�ӦͰ�µ�Span
	//��һ�����������ڴ����
	size_t actualNum = CentralCache::GetInstance()->FetchRangeObj(start, end, batchNum, size);
	
	//Central Cache ��Ҫ���̵߳�Thread Cache ���ٷ�����1��
	assert(actualNum >= 1);
	
	//��thread cache�����Ļ����ȡ���ڴ����ĵ�һ�����󷵻ظ��̣߳�����Ĺҽӵ���Ӧ������������
	if (actualNum == 1)
	{
		assert(start == end);
		return start;
	}
	else
	{
		//���������ĵ�һ�����ظ��߳�, ������actualnum - 1������ҵ�Thread Cache����������Ͱ��
		void* second = NextObj(start);
		//�����Ļ�������thread cache�ĵڶ����ڴ�������һ���ڴ������ɵ�������뵽����������

		_freelists[index].PushRange(second, end, actualNum - 1);

		//����һ���ڴ����ֱ�ӷ�����߳�
		return start;
	}
}


void* ThreadCache::Allocate(size_t size)
{
	assert(size <= MAX_BYTES);
	//size + t - 1
	//�õ�����208��Ͱ�������õ��Ķ����ķ�����ڴ����Ĵ�С���ֽ���
	size_t alignBytes = SizeClass::Roundup(size);

	//��������Ͱ��
	size_t index = SizeClass::Index(size);

	if (!_freelists[index].Empty())
	{
		return _freelists[index].Pop();
	}
	else
	{
		//���߳���Ҫ���ڴ�������ڵ���������(ThreadCache)ʣ��������������CentralCache��ȡ�ڴ����
		return ThreadCache::FetchFromCentralCache(index, alignBytes);
	}
}

//�黹���ڴ�����Сһ���Ƕ������ڴ�����С
void ThreadCache::Deallocate(void* ptr, size_t size)
{
	assert(ptr);
	assert(size <= MAX_BYTES);

	//�ҳ���Ӧ����������Ͱ���
	size_t index = SizeClass::Index(size);
	//���뵽��Ӧ����������Ͱ��
	_freelists[index].Push(ptr);

	/*
	1.�߳��ͷ��ڴ����ʱΪ�˱�����е��ڴ����Ķѻ�����Ҫ��һ���������ڴ�����ͷŸ�Central Cache
	  ��һ���ͷŸ�Page Cache�����кϲ�����span����ڴ���Ƭ�������⣬�Ӷ��ϲ������ڴ�������span

	2.tcmalloc�ͷſ�������������: (Thread Cache)�е������� �� (Thread Cache)ռ�ݵ����ڴ��С
	  tcmalloc���¼ÿһ���̵߳�Thread Cache�п����ڴ������ܴ�С�����ж��Ƿ񳬳�һ������(2MB)��
	          �����ͽ��л���
	*/
	
	
	//������������еĿ����ڴ����ĸ�����������ʱһ��������������ڴ����ĸ�������ʾ�����ڴ����̫�����ò���
	//��ʱ���Խ���Щ�����ڴ����ĸ����ͷŸ�Central Cache(������Ƹ����ӵ��жϹ黹�������㷨)
	if (_freelists[index].Size() >= _freelists[index].CurrentBatchNum())
	{
		ListTooLong(_freelists[index], size);
	}
}


//��������̫����ʱ�򣬴�����������ȡ��batchNum���ڴ���󻹸�Central cache
//����ȫȡ�꣬��Ϊ�߳̿����ڹ黹�Ĺ����л������Ҫ�ڴ�
void ThreadCache::ListTooLong(Freelist& list, size_t size)
{
	void* start = nullptr;
	void* end = nullptr;

	list.PopRange(start, end, list.CurrentBatchNum());

	//������������ȡ��һ�����������������ǿգ����ǿ���ͨ��start�������β�嵽��Ӧ��Central Cache��span��
	CentralCache::GetInstance()->ReleaseListToSpans(start, size);
}