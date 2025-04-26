#include "CentralCache.h"
#include "PageCache.h"
#include "Common.h"

// ���嵥������
CentralCache CentralCache::_sInst;



//������������ڣ���Ա�����˴˿ɼ��������໥���ã������ڶ����Ա����ʱ��������Ȼ������
//CentralCache��ĳ�Ա��������ȡһ��Span
Span* CentralCache::GetOneSpan(SpanList &list, size_t size) 
{
    //�Ȳ鿴��ǰ��spanlist���Ƿ���δȫ����С�ڴ����������span
    //����ӳ�䵽Central Cache ��ĳ����ϣͰ��Spanlist�����ҵ�һ�����ʵ�span���ظ��߳�Thread Cache
    Span* it = list.Begin();
    while (it != list.End())
    {
        //ֻҪ��������span��������������ǿ�(���߳���Ӧ��Ҫ���ڴ����)���ͷ��ظ�span���߳�
        if (it->_freelist != nullptr)
            return it;
        else
            it = it->_next;
    }
    
    
    //�ߵ�����߳�֮ǰ�ӵ�Ͱ�����Խ�� --- ���ӹ黹�ڴ������̺߳ͻ�ȡ�ڴ����֮ǰ�Ĳ�����
    //ֻ��ͬʱ��ȡͬһ��Ͱ���ڴ����֮����߳� �� ͬʱ�黹ͬһ��Ͱ���ڴ����֮����̲߳Żᷢ���߳̾���
    /*
     1.�ߵ���˵��Central Cache��Ͱlist��û���κ�һ���ǿյ�span�������������Ͱ���̲߳�����������������
     2.�����������������Ͱlist���ͷ�С�ڴ������̣߳���ͬ��������������߳�֮��Ĳ����Ƚ�����
    */
    
    list._mutex.unlock();
   
    
    
    /*�ߵ�����˵����
      �߳���Ҫ���ڴ�������� Central Cache �Ĺ�ϣͰ��Ϊ��(û��span)����ÿһ��span�е��ڴ�����Ѿ������ȥ
      ��ʱ��Ҫ�� Page Cache �������Ӧ���µķǿ�span��
    */

    //����Page Cacheʱ���� Page Cache��ȫ����
    PageCache::GetInstance()->_pagemutex.lock();
   
    
    //��PageCache��ȡ�߳������Ҫ��ҳ��С��span
    Span* span = PageCache::GetInstance()->NewSpan(SizeClass::NumMovePage(size));
    //�߳�������ġ���Ҫʹ�õ�Span�޸���״̬
    span->_isUse = true;
    span->_objSize = size;

    //�ͷ�ȫ����
    PageCache::GetInstance()->_pagemutex.unlock();

   
    
    //�Ի�ȡ���µ�span�����з֣�����Ҫ��������Ϊ���������̷߳��ʲ������span
    /*
    ������̶߳Դ� Page Cache ��õ�span����з� ���ҵ���Ӧ��Ͱ��list��֮ǰ�������߳��ǲ��ܷ��ʵ����
    span��
    */

    //���span����ʼҳ���ڵ���ʼ��ַ
    char* start = (char*)(span->_PageId << PAGE_SHIFT);

    //���ݸ�span��ռ��ҳ�������span�Ĵ�С(ҳ�� * 8KB/ (ҳ�� << 13)(�ֽ���))
    //ʹ��char*�ĺô���char*���͵�ָ�� + 1����������1 B���������ָ���ƫ��
    size_t bytes = span->_n << PAGE_SHIFT;
    char* end = start + bytes;

    
    
    //�ѻ�õĴ���ڴ�span�г�С�ڴ���󲢷ŵ�span���������Ϲҽ�����
    //��Ϊspan�µ�_freelist�ǲ���ͷ�ڵ�ĵ�����, ���Ȳ���һ���ڵ���Ϊͷ�ڵ�, ����β��
    //1. ����һ���������ӵ�Span�е�����������, size��һ���ڴ����Ĵ�С

    span->_freelist = start, NextObj(span->_freelist) = nullptr;
    start += size;
    //tail��βָ�룬ʵ������β��
    void* tail = span->_freelist;

    while (start < end)
    {
        NextObj(tail) = start;
        tail = NextObj(tail); // tail = start
        start += size;
    }

    //�����һ��β�ڵ��nextָ���ÿ�
    NextObj(tail) = nullptr;

   /*
    1.���µ�span�ڵ�ͷ�嵽list��ͷ���ĺ��棬ֻ�����ʱ�������߳�(�߳�2)�ſ��ܷ��ʵ����span;
    
    2.�����ʱ����Ҫ��Ͱ���м�������֤��ǰ�߳�(�߳�1)��õ�span�ܹ����������ҵõ�������ᱻ�����߳��õ�;
    
    3.�ߵ�����ʱ������������󣬾��� �߳�2 �Ѿ��õ�������Ҳ����Ϊ���Ͱ��û�п��е�span��ȥ����PAGE CACHE
      �� �߳�2 �ڷ��� PAGE CACHE ֮ǰ�ͻ�� �� �����
      �����ʹ�� �߳�3�� ��� �߳�1 �Ȼ����������ֻҪ�߳�1û�а����span�ҵ�list�У��κ��߳̿����ĵ�ǰͰ
      list�ж��ǿյģ��������̶߳������ΰ��������
    
    4.���ǵ�ǰ�߳���Ȼ�����õ���������� ��õ��µ�span �ҵ���ǰ Central Cache ��Ͱlist�У�����ɺ������أ�
    ֮������� FetchRangeObj �н���õ� �µ�span ���߳�1����Ķ�Ӧ�������ڴ���� ���ظ��Լ���thread cache��Ͱ�С�


    5.�ܽ�: ��һ������ Central Cache�� Ͱlist û�п��е�span ���̣߳��ڵ�ǰ������ ���� PAGE CACHE֮ǰ��Ͱ����
            �� ��span�ҵ�list֮ǰ���� �ܹ���֤ ��ͬ����(��ȡ/�黹)֮����̵߳Ĳ����ȣ����ҵ�һ���߳� ��Ȼ�ܹ�
            ��ȷ���õ��Լ������span�е��ڴ����
   */
    list._mutex.lock();
    list.PushFront(span);

    return span;
}



// CentralCache��ĳ�Ա����
size_t CentralCache::FetchRangeObj(void* &start, void* &end, size_t batchNum, size_t byte_size) 
{
    // �������Ļ�����Ͱ�ı��, ӳ��ԭ��ͬThread Cache��Ͱ�ŵ�ӳ��
    size_t index = SizeClass::Index(byte_size);
    
    
    //Ͱ��---������߳�ͬʱ�������Ļ����б��Ϊindex��Ͱ�ľ�������
    _spanlists[index]._mutex.lock();

    //���һ��span
    Span* span = GetOneSpan(_spanlists[index], byte_size);



    assert(span);//�ж�span��Ϊ��
    assert(span->_freelist);//�ж�span�µ���������Ϊ��


    //�ӻ�õ�span�µ�freelistȡ��batchNum���ڴ����
    start = span->_freelist;
    end = start;

    //endָ������ߵ�span�µĵ�min(actualNum, batchNum)���ڴ����
    //actualNum��¼ʵ�ʻ�õ��ڴ�������
    size_t actualNum = 1;

    /*
    ���һ: ��õ�span�µ�ʵ�ʵ��ڴ���������㹻batchNum��
    =>�ж�����1: ��span�з�����̵߳�ʵ���������ܳ�������ʼ�㷨�������batchNum��(actualNum <= batchNum)
    �����: ���ʵ����������batchNum��
    =>�ж�����2: ����end�ߵ���NextObj(end)Ϊ�ա�ʱ��ֹͣ����
    */
    
    while (actualNum < batchNum && NextObj(end) != nullptr)
    {
        end = NextObj(end);
        actualNum++;
    }

    //span�е�ʹ�ü�����Ҫ����actualNum��ÿ�����thread cache 1���ڴ�����Ҫ��ʹ�ü�����1
    span->_usecount += actualNum;

    span->_freelist = NextObj(end);
    NextObj(end) = nullptr;//end��nextָ���ÿ�


    _spanlists[index]._mutex.unlock();
    return actualNum;//����֮ǰ������������������߳��޷����ʸ�Ͱ
}




/*
����Thread Cache���� Central Cache �����ڴ����ʱ�����ܴӶ��Span�������;
eg:��һ�δ�Spanlists[index]�ĵ�һ��span�������������ڴ���󣬵�һ��Span��������֮��
   �´�������ʱ�������һ��span�����ڴ����
�������Щ�ڴ���������̺߳��̹߳黹ʱ��˳���ǲ�ȷ���ģ�(�黹������������ʹ�õ���ͷ�巨)
����Thread Cache������������ĳһ�������ڴ�����п������ڲ�ͬ��Spans��
*/

/*
����ÿ�������span��ϵͳ�Ķѿռ��������ļ���ҳ�棬��span�б��������ļ���ҳ����ʼҳ��: _PageId;
���ǿ��Ը��ݹ黹��ÿ���ڴ�������ʼ��ַ(ptr)���жϳ����span������һҳ���ڴ�(ptr >> 13(_PAGESHIFT));
���õ�����ڴ�������ڵ�ҳ�ţ��������ҳ�ſ����ж���������һ��span;
��ַ�ṹ: ҳ��(19λ) + ҳ��ƫ����(13λ)
*/
void CentralCache::ReleaseListToSpans(void* start, size_t byte_size)
{
    size_t index = SizeClass::Index(byte_size);

    //Ͱ��
    _spanlists[index]._mutex.lock();

    void* it = start;
    while (it != nullptr)
    {
        void* next = NextObj(it);
        //����ҳ�ź�span��ӳ���ϵ�����it������span
        
        Span* span = PageCache::GetInstance()->MapObjectToSpan(it);

        //ͷ�巨�����ڴ������뵽��Ӧspan������������
        NextObj(it) = span->_freelist;
        span->_freelist = it;
        span->_usecount--;

        //˵��span���зֳ�ȥ������С���ڴ涼������
        //���span�Ϳ����ٻ��ո�page cache, page cache�����ٳ�����ǰ��ҳ�ĺϲ�
        if (span->_usecount == 0)
        {
            //�� Central Cache ���߼�ɾ����ǰʹ�ü���Ϊ0 �� Span
            _spanlists[index].Erase(span);
            span->_freelist = nullptr;
            span->_prev = nullptr;
            span->_next = nullptr;

           /*���Ͱ����ԭ��:
            1.����Page Cache ֮ǰ���Խ�Ͱ���������Ϊ���ʱ��ǰ�̹߳黹��span�Ѿ���Ͱ��ɾ���ˣ�
              ������ʱ�򲻴��������߳�Ҳ�ܷ��ʵ����span�Ŀ���;
            
            2.���ʱ��ǰ�黹�ڴ�����Լ��黹span���߳���ʱ����Ҫ Central Cache�����Դ��������ʱ�ͷŵ�;
              ���֮������������̴߳����Ͱ��� ����span �� (��ȡ�ڴ����) ���� (�黹�ڴ����)��
              ���������߳�֮��Ĳ����ȡ�
           */
            _spanlists[index]._mutex.unlock();

            //��span �黹��Page Cache֮ǰ��Ҫ���� Page Cache�����Դ����Ҫ�������
            PageCache::GetInstance()->_pagemutex.lock();
            
            PageCache::GetInstance()->ReleaseSpanToPageCache(span);

            //�黹��֮����� �ͷ�Page Cache�����Դ�� ��Ҫ����
            PageCache::GetInstance()->_pagemutex.unlock();

            //������ Page Cache ֮����Ҫ��Ͱ�����ϣ��Ա��ڵ�ǰ�̺߳��������������Ͱ���span �黹������ڴ����
            _spanlists[index]._mutex.lock();

        }

        //�ҵ���һ����Ҫ�黹���ڴ����
        it = next;
    }
    _spanlists[index]._mutex.unlock();
}