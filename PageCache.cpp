#include "PageCache.h"


PageCache PageCache::_sInstance;//��ʼ������


/*����ʹ�� usecount == 0 �ж� Span �Ƿ���Page Cache�е� ԭ��:
	  1. ���� usecount == 0 �� �� Page Cache�е� Span�� ��Щ�ǿ��Ժϲ��ģ�
	  2. ���� usecount == 0 �� �� Central Cache �е� Span:
		 (1)��� Span�� ������ڴ���� ����Ϊ �߳� �黹�ڴ�����µ� usecount��Ϊ0����ЩSpan�������ǿ���
			�ϲ��ģ���Ϊ��ЩSpan ��Ȼ��ʱ��û�б��黹�� Page Cache�У�����������Ҳ��黹�� Page Cache��
			���������� Span ֮��ĺϲ���

		 (2)������� Span �������̸߳� ͨ�� Central Cache �� Page Cache����ģ���ʱ�յõ����Span������usecount
			Ҳ���� 0��������� Span ��Ҫ�ŵ� Central Cache �еģ�������Ҫ����� ������߳� ʹ�õģ�����һ���
		    Span �ǲ��ܲ��� ����Span ֮��ĺϲ��ġ�
*/

/*
  1.��û�н� Page Cache �е� Span �ڹ�ϣ���н��� ��Ӧӳ��ʱ��
    ����ʹ��Page Cache �еĹ�ϣ���Ƿ����ӳ���ϵ �ж� Span �Ƿ���Page Cache�е� ԭ��:
     (1).��Ϊÿһ������� Central Cache �� Span ռ���ڴ������ҳ�棬 ������ ��ϣ���н���ӳ���ϵ��
	    ��������û�б������ Central Cache �� Span (������� �� ��Ȼ���� Page Cache)�ڹ�ϣ���в��Ὠ��ӳ�䣻

	 (2).���ǲ������� û�н���ӳ��� ҳ����ɵ� Span���� Page Cache�У���Ϊ��Щҳ���п��ܻ�û�����������

  2.��Span �м��� ״̬��־λ isUse ��
    (1).��ʱ���Խ� Page Cache �е� Span �ڹ�ϣ���н��� ��Ӧӳ�䣻
	(2).��ʱ��ϣ���д��ڵ� Span ֻ�����ֿ���: ���� Page Cache�С��Ѿ������ Central Cache
	    ���ǿ���ͨ�� ��ϣ���ӳ�� + isUse��־λ �ų�������� Central Cache ��Span �Ŀ��ܣ������Ϳ���
		ɸѡ�� ���� PAGE CACHE �е� Span
*/


//�ͷſ���span�ص�Pagecache�����ϲ����ڵġ�δ������� Central Cache��span
void PageCache::ReleaseSpanToPageCache(Span* span)
{
	//����������ڴ�(����128ҳ)�Ĺ黹����,ֱ�ӻ�����
	if (span->_n > NPAGES)
	{
		void* ptr = (void*)(span->_PageId << PAGE_SHIFT);
		SystemFree(ptr);
		//delete span;
		_SpanPool.Delete(span);
		return;
	}
	
	//���ڡ�128ҳ��Span��ֱ�ӻ��� Page Cache�����ڴ�ϲ� �� ��������
	//��spanǰ���ҳ�����г����Եĺϲ����ϲ��󻺽��ڴ���Ƭ(����Ƭ)����

	/*�ϲ�ʱ��Ҫ��:
	1.�ϲ�ʱ��Ҫ����ǰ�黹��Span�ڹ�ϣ���е�����ӳ���ɾ��(��ɾ�������̹߳黹spanʱ�ϲ����Span�����Ұָ������);
	  �Լ��ϲ�ʱǰ�����ڵ�Span ������ӳ�� �� �������ڵ�Span �ڹ�ϣ���н���������ӳ�� ��ɾ����
	  �����ϲ�����µĴ�Span �ڹ�ϣ���� ��βҳ�ŵ�ӳ���ϵ���Ա�֤ �����̹߳黹 Span ʱ��
	  �����ظ��ϲ����� Span �Ĳ����Լ�Ұָ������.
	
	2.�ϲ�ʱ��Ҫ���ǵ�ǰ�黹��Span ��ǰ�����ڵ�Span�ϲ��󣬴�С�Ƿ�ᳬ�� 128ҳ �������
	  �������� Span �ϲ�ʱ ���� 128ҳ �Ĵ�С����ֹͣ�ϲ�
	  eg: ��ʼʱ���߳�1���� 256KB ���ڴ���󣬶�Ӧ��Page Cache��������һ��128ҳ��С��Span�����з���һ��64ҳ��Span
	      �� Central Cacheת�����߳�1ʹ�ã�
		  
		  �߳�2���� 8B ���ڴ����Page Cache�л��64ҳ��Span ���зֳ�1ҳ���� Central Cacheת�����߳�2ʹ�ã�

		  �߳�3���� 256KB ���ڴ����Page Cache ��û��64ҳ��С�� Span��������������һ�� 128ҳ��С��Span��
		  ���з���һ��64ҳ��Span�� Central Cacheת�����߳�1ʹ�ã�

		  ��ǰ����������� 128ҳ��С��Span �ڶ����������ģ�
		  ���߳�1��2ʹ�õ��ڴ����ȫ���黹�󣬵�һ��128ҳ��С�� Span�� ����˺ϲ� ������ Page Cache�У�
		  ���߳�3�黹��Ҳ����кϲ�����ʱ��ǰ�� 128ҳ��С�� Span �ϲ�ʱ�� ����128ҳ��С��

	*/
	
	//����ǰ�黹��Span�ڹ�ϣ���е�����ӳ��ȫ��ɾ��
	
	for (size_t i = 0; i < span->_n; i++)
	{
		PAGE_ID currentId = span->_PageId;
		_PageidSpanMap.erase(currentId + i);
	}

	
	//ֻ�ϲ�Page Cache ����������ڴ�
	
	//��ǰ�ϲ�
	while (1)
	{
		PAGE_ID prevId = span->_PageId - 1;
		Span* prevSpan = (Span*)_PageidSpanMap.get(prevId);

		/*����ֹͣ�ϲ�������
		����1: ��ǰSpan ǰ(��)���ҳ��û�б�Page Cache �����룻
		����2: ��ǰSpan ǰ(��)���ҳ������ Central Cache �б�ʹ�ã�
		����3: ��ǰSpan ǰ(��)��ϲ���Ĵ�С���� 128ҳ���޷���Page Cache �й���
		*/
		//���ǰ�����ڵ�ҳ��û���ڹ�ϣ���н���ӳ�䣬��ʾǰ���ҳ�������ڴ�û�б�Page Cache����
		//��ʱ���úϲ�
		if (prevSpan  == nullptr)
		{
			break;
		}
		

		//ǰ�����ڵ� Span ����Ȼ�� Central Cache �б�ʹ�õ� Span���ϲ�ǰ������Span�Ĳ���ֹͣ
		if (prevSpan->_isUse == true)
		{	
			break;
		}

		//���ǰ�����ڵ�Span ��ҳ�� ���� ��ǰSpan ��ҳ�� > 128����ʱPage Cache�Ĺ�ϣͰû�취����ֹͣ�ϲ�
		if (prevSpan->_n + span->_n > NPAGES)
		{
			break;
		}
		
		//ǰ�����ڵ� Span ����Page Cache �е��ڴ桢δ������ʹ���Һϲ���Ĵ�С������������Ժϲ�

		span->_PageId = prevSpan->_PageId;
		span->_n += prevSpan->_n;

		//�Ƚ�ǰ���Ѿ��ϲ��� prevSpan �� Page Cache�Ĺ�ϣͰ���߼���ɾ����
		//����������ɾ���������̷߳���Page Cacheʱ�����ʵ� prevSpan ����Ұָ�����
		_spanlists[prevSpan->_n].Erase(prevSpan);

		//����ɾ��prevSpan�ڹ�ϣ���е���βҳ�ŵ�ӳ��
		_PageidSpanMap.erase(prevSpan->_PageId);
		_PageidSpanMap.erase(prevSpan->_PageId + prevSpan->_n - 1);

		//ǰ���Ѿ��ϲ���Span���������ڴ��Ѿ���span���ֹ�����ǰ��ϲ���Span�����ʵ��Ҳ��û��Ҫ������
		//�ʿ��Զ�prevSpan�����ʵ�����������ɾ��
		
	    //delete span;��span�黹�����������
		_SpanPool.Delete(prevSpan);
	}

	//���ϲ�
	while (1)
	{
		PAGE_ID  nextId = span->_PageId + span->_n;
		Span* nextSpan = (Span*)_PageidSpanMap.get(nextId);
		
		//��ϣ����û��ӳ��, ҳ��ΪnextId���ڴ�δ��Page Cache��������, ���ڴ���Ȼ����ϵͳ
		if (nextSpan == nullptr)
		{
			break;
		}

		//�����Span��Ȼ��Central Cache�б�ʹ��, ���ܺϲ�
		if (nextSpan->_isUse == true)
		{
			break;
		}

		//����span�ϲ������Ĵ�С���� Page Cache�����Թ���� Span��С, ֹͣ�ϲ�
		if (nextSpan->_n + span->_n > NPAGES)
		{
			break;
		}

		//���Ժϲ�, ��ʼҳ�Ų���
		span->_n += nextSpan->_n;

		_spanlists[nextSpan->_n].Erase(nextSpan);

		_PageidSpanMap.erase(nextSpan->_PageId);
		_PageidSpanMap.erase(nextSpan->_PageId + nextSpan->_n - 1);
		
		//delete nextSpan
		_SpanPool.Delete(nextSpan);
	}
	
	//���������ϲ����̶��ǻ����̹߳黹��span���еģ������span֮ǰ��isUse��־λ��true
	//��Ҫ����Ϊfalse
	span->_isUse = false;

	//�ٽ��ϲ�����µĴ�span�����Ӧ Page Cache �еĹ�ϣͰ
	_spanlists[span->_n].PushFront(span);

	//�ٽ��ϲ�����µĴ�Span�ڹ�ϣ���н���ӳ��
	if(_PageidSpanMap.Ensure(span->_PageId, 1)) 
		_PageidSpanMap.set(span->_PageId, span);
	if (_PageidSpanMap.Ensure(span->_PageId + span->_n - 1, 1))
		_PageidSpanMap.set(span->_PageId + span->_n - 1, span);
}


//��Page Cache�л�ȡkҳ��С��span
Span* PageCache::NewSpan(size_t k)
{
	//assert(k >= 1 && k <= NPAGES);

	assert(k >= 1);

	if (k > NPAGES)
	{
		//�����ҳ������ 128ҳ��ֱ����ϵͳ������
		//Span* SuperBigSpan = new span;
		Span* SuperBigSpan = _SpanPool.New();
		void* ptr = SystemAlloc(k);
		SuperBigSpan->_PageId = (PAGE_ID)ptr >> PAGE_SHIFT;
		SuperBigSpan->_n = k;

		/*
		1.�ڹ�ϣ���м�¼����������span �� ��ʼҳ�ŵ�ӳ�䣬�Ա����̹߳黹������ڴ�ʱ��
		  ����ͨ����ʼ��ַ�ҵ����span;
		2.���̹߳黹С��Spanʱ��������� Span���󣬹ʲ��ᷢ���ϲ�
		*/
		if(_PageidSpanMap.Ensure(SuperBigSpan->_PageId, 1))
			_PageidSpanMap.set(SuperBigSpan->_PageId,  SuperBigSpan);

		return SuperBigSpan;
	}

	//�ȼ���k��Ͱ������û��span����ֱ�ӷ��أ������з�
	if (!_spanlists[k].Empty())
	{
		Span* kSpan = _spanlists[k].Begin();
		PAGE_ID start = kSpan->_PageId, end = kSpan->_PageId + kSpan->_n - 1;

		if (_PageidSpanMap.Ensure(start, kSpan->_n))
		{
			for (PAGE_ID i = start; i <= end; i++)
			{
				_PageidSpanMap.set(i, kSpan);
			}
		}
		
		return _spanlists[k].PopFront();
	}
	
	//����Ͱ��ŵĴ�С��С�������μ��һ�º����Ͱ������û��span������У����Խ��������з�
	//�����ҵ���i��Ͱ��span������iҳ��С��span�зֳ�һ�� kҳ��С�� span �� һ�� i-k ҳ��С��span
	//kҳ��С�� span ���ظ� central cache�� i - kҳ��С�� span �ҵ���i - k��Ͱ��ȥ
	for (size_t i = k + 1; i <= NPAGES; i++)
	{
		if (!_spanlists[i].Empty())
		{
			Span* iSpan = _spanlists[i].PopFront();//iҳ��С��span
			
			//kҳ��С��span

			//Span* kSpan = new span;
			Span* kSpan = _SpanPool.New();

			//��iSpan��ͷ������һ��kҳ��Сspan����
			//kҳ��span����
			//iSpan ӳ�䵽��Ӧ��λ��
			kSpan->_PageId = iSpan->_PageId;
			kSpan->_n = k;

			iSpan->_PageId += k;
			iSpan->_n = i - k;

			_spanlists[i - k].PushFront(iSpan);

			/*��������ġ��зֺ�� (i - k)ҳ��С�� ������ Page Cache�е� iSpan �ڹ�ϣ���н���ӳ�䣬
			  �Ա��ں����ϲ����� Span ʱ������Ӧ����Ϣ��
			  
			  1.��ϣ����߳�ʹ��ʱ�����Ὠ������ӳ�䣻
			    ���߳� �黹�ڴ���� �� Central Cache ʱ�Ż��õ���ϣ����ʱ�߳���Ҫ��ȡ ÿ���ڴ���������ġ�
				���� Central Cache�е� Span�����߳� �黹����Щ�ڴ����һ���� �Ѿ������ Central Cache�ģ�
				���� Page Cache �е� Span �Լ� ���ڹ�ϣ���н�����ӳ�� һ�����ᱻ���ʡ�
			  
			  2.������ ����Span �ϲ�ʱ��ֻ��Ҫ�õ� ���� Page Cache �е� Span ����ʼҳ�� �� ĩβҳ�ţ�
			    ���� ֻ��Ҫ�ڹ�ϣ���н��� ��ʼҳ�š�ĩβҳ�� �� Span* ֮���ӳ���ϵ���ɡ�
				����: 
				  (1) ��ǰ�ϲ�ʱ��ֻ�� �õ�ǰ���е� Span ����ʼҳ�� - 1,�Ϳ��Եõ�ǰһ��Span ��ĩβҳ�ţ�
				      ͨ��ĩβҳ�ŵ�ӳ��Ϳ��Եõ� ǰһ�����ڵ� prevSpan����ͨ�� prevSpan ��ʹ��״̬λ
					  isUse �����жϳ����Ƿ��� ���� Page Cache �е�Span��

				  (2) ����ϲ�ʱ��ֻ�� �õ�ǰ���е� Span ����ʼҳ�� + ��ռҳ��,�Ϳ��Եõ���һ��Span ��
				      ��ʼҳ�ţ���ͨ���õ�����ʼҳ�ŵ�ӳ���ϵ�Ϳ��Եõ� ��һ�����ڵ� nextSpan����ͨ��
					  nextSpan ��ʹ��״̬λ isUse�Ϳ����жϳ� ���Ƿ��� ����Page Cache ��Span
			*/
			//���� iSpan ����ʼҳ�ŵ�ӳ��
			if(_PageidSpanMap.Ensure(iSpan->_PageId, (size_t)1)) _PageidSpanMap.set(iSpan->_PageId,  iSpan);

			//���� iSpan ��ĩβҳ�ŵ�ӳ��
			if(_PageidSpanMap.Ensure(iSpan->_PageId + iSpan->_n - 1, 1)) _PageidSpanMap.set(iSpan->_PageId + iSpan->_n - 1, iSpan);


			/*
			��Page Cache ÿ�η��� �µ�Span �� Central Cache ʱ�������ѷ���� Span �� ����ռ�ݵ��ڴ��ÿһ��ҳ���ҳ�ŵ�ӳ��,
			�Ա��ں����ڻ��� Thread Cache �����������е� �ڴ����ʱ�����Ը����ڴ����ҳ�� ����Ĳ��ҵ� �ڴ���� ������Span
			*/
			//���� kSpan ռ�ݵ�_n��ҳ���Pageid �� Span��һ��ӳ���ϵ
			PAGE_ID start = kSpan->_PageId, end = kSpan->_PageId + kSpan->_n - 1;

			if (_PageidSpanMap.Ensure(start, kSpan->_n))
			{
				for (PAGE_ID i = start; i <= end; i++)
				{
					_PageidSpanMap.set(i, kSpan);
				}
			}

			return kSpan;
		}
	}
	
	// �ߵ����λ��˵��û�д�ҳ��span
	// ��ȥ�Ҷ�Ҫһ��128ҳ��span
	Span* bigSpan = _SpanPool.New();
	void* ptr = SystemAlloc(NPAGES);
	bigSpan->_PageId = (PAGE_ID)ptr >> PAGE_SHIFT;
	bigSpan->_n = NPAGES;

	_spanlists[bigSpan->_n].PushFront(bigSpan);

	//����ʹ�õݹ飬�Ǳ����ظ������Ѿ�ʵ�ֵ��з�bigSpan�Ĵ���
	return NewSpan(k);
}


Span* PageCache::MapObjectToSpan(void* obj)
{
	/*�������߳��ڶ���ϣ��Ҳ���߳���д�����ϣ��,����Ҫ�Թ�ϣ�������
	  ��Ϊ�޸Ĺ�ϣ���ʱ���ϣ���ڵ����ݽṹ��仯������ϣ�����������ݽṹ�仯��ʱ��ײ�û�м�����
	  ��������߳���ʱ���ȡ��ϣ������ֵ�ַӳ�䲻��ȷ�����(�޸Ĳ�û�����)��
	  �������ڹ�ϣ���������ݽṹ����ʵ�ֵľ����Ե��µģ����ڴ�����̶߳������ݵķ��ʲ�û�г�ͻ

	1.д�߳���д��ϣ���ʱ���ǵ���NewSpan������������������Ҫ���� Page Cache��
	  ����д֮ǰ���Ѿ�������Page Cache�������;
	2.���߳��ڶ���ϣ���ʱ��û�м����������̶߳���ͨ��MapObjectToSpan���ӳ�亯����ȡ��ϣ��
	  ����MapObjectToSpan��������ڼ���Page Cache�����������
	*/

	//������unique_lock����࣬���������ĺô����ǳ�����������������������Զ�����
	//std::unique_lock<std::mutex> lock(_pagemutex);
	
	//case1: New Span ����һ���µ�Span ���޸Ļ�������Release Span �黹Span ʱ���޸Ļ�����
	//case2: ��ȡ�������е�Spanָ�������ֻ���� �����ڴ�������ڵ�Spanʱ�Żᷢ��
	//��case1�е�Span Ҫ������Page Cache��������� / �������� Page Cache �� / �Ѿ���Central Cache���ͷ�
	//case2 �е�Span ֻ�п�����Central Cache�У�����������������ܷ���ͬһ�����ݣ����Ƕ�д�߳�֮��û�л���Ĺ�ϵ
	//���߳� �� ���߳�֮��Ի���������ʱ��Ҳû�л���(���ݳ�ͻ/����ð��)�Ĺ�ϵ
	//д�߳� �� д�߳�֮��Ի���������ʱ��ͬʱҲ���Page Cache�������ʣ��������֮��Ļ�������Page Cache��ݴ�����֤��
	//��MapObjectToSpan��������ڲ���Ҫ����

	//�����ڴ�����ҳ��
	PAGE_ID pageid = (PAGE_ID)obj >> PAGE_SHIFT;

	Span* ret = (Span*)_PageidSpanMap.get(pageid);

	assert(ret != nullptr);

	return ret;
}