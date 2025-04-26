#include "CentralCache.h"
#include "PageCache.h"
#include "Common.h"

// 定义单例对象
CentralCache CentralCache::_sInst;



//在类的作用域内，成员函数彼此可见，可以相互调用，这里在定义成员函数时作用域仍然是类域
//CentralCache类的成员函数，获取一个Span
Span* CentralCache::GetOneSpan(SpanList &list, size_t size) 
{
    //先查看当前的spanlist中是否还有未全部将小内存对象分配完的span
    //遍历映射到Central Cache 中某个哈希桶的Spanlist链表，找到一个合适的span返回给线程Thread Cache
    Span* it = list.Begin();
    while (it != list.End())
    {
        //只要遍历到的span下面的自由链表不是空(有线程相应需要的内存对象)，就返回该span给线程
        if (it->_freelist != nullptr)
            return it;
        else
            it = it->_next;
    }
    
    
    //走到这该线程之前加的桶锁可以解掉 --- 增加归还内存对象的线程和获取内存对象之前的并发度
    //只有同时获取同一个桶中内存对象之间的线程 和 同时归还同一个桶中内存对象之间的线程才会发生线程竞争
    /*
     1.走到这说明Central Cache的桶list中没有任何一个非空的span，故其他申请该桶的线程不会引发竞争的问题
     2.如果不解锁，其他往桶list中释放小内存对象的线程，会同样被阻塞，因此线程之间的并发度降低了
    */
    
    list._mutex.unlock();
   
    
    
    /*走到这里说明：
      线程需要的内存对象所在 Central Cache 的哈希桶中为空(没有span)或者每一个span中的内存对象都已经分配出去
      此时需要向 Page Cache 中申请对应的新的非空span。
    */

    //访问Page Cache时加上 Page Cache的全局锁
    PageCache::GetInstance()->_pagemutex.lock();
   
    
    //向PageCache获取线程最多需要的页大小的span
    Span* span = PageCache::GetInstance()->NewSpan(SizeClass::NumMovePage(size));
    //线程新申请的、将要使用的Span修改其状态
    span->_isUse = true;
    span->_objSize = size;

    //释放全局锁
    PageCache::GetInstance()->_pagemutex.unlock();

   
    
    //对获取的新的span进行切分，不需要加锁，因为这会儿其他线程访问不到这个span
    /*
    在这个线程对从 Page Cache 获得的span完成切分 并挂到对应的桶的list上之前，其他线程是不能访问到这个
    span的
    */

    //获得span的起始页所在的起始地址
    char* start = (char*)(span->_PageId << PAGE_SHIFT);

    //根据该span所占的页数计算出span的大小(页数 * 8KB/ (页数 << 13)(字节数))
    //使用char*的好处是char*类型的指针 + 1，即往后走1 B，方便控制指针的偏移
    size_t bytes = span->_n << PAGE_SHIFT;
    char* end = start + bytes;

    
    
    //把获得的大块内存span切成小内存对象并放到span自由链表上挂接起来
    //因为span下的_freelist是不带头节点的单链表, 故先插入一个节点作为头节点, 方便尾插
    //1. 先切一块下来链接到Span中的自由链表中, size是一个内存对象的大小

    span->_freelist = start, NextObj(span->_freelist) = nullptr;
    start += size;
    //tail是尾指针，实现链表尾插
    void* tail = span->_freelist;

    while (start < end)
    {
        NextObj(tail) = start;
        tail = NextObj(tail); // tail = start
        start += size;
    }

    //将最后一个尾节点的next指针置空
    NextObj(tail) = nullptr;

   /*
    1.将新的span节点头插到list的头结点的后面，只有这个时候其他线程(线程2)才可能访问到这个span;
    
    2.故这个时候需要对桶进行加锁，保证当前线程(线程1)获得的span能够最后就是由我得到，否则会被其他线程拿到;
    
    3.走到这里时，在这里加锁后，就算 线程2 已经拿到锁，他也会因为这个桶里没有空闲的span而去访问PAGE CACHE
      而 线程2 在访问 PAGE CACHE 之前就会把 锁 解掉；
      解掉后即使有 线程3等 会比 线程1 先获得锁，但是只要线程1没有把这个span挂到list中，任何线程看到的当前桶
      list中都是空的；故其他线程都会依次把锁解掉；
    
    4.于是当前线程仍然可以拿到这个锁并将 获得的新的span 挂到当前 Central Cache 的桶list中，并完成函数返回；
    之后可以在 FetchRangeObj 中将获得的 新的span 中线程1所需的对应数量的内存对象 返回给自己的thread cache的桶中。


    5.总结: 第一个发现 Central Cache的 桶list 没有空闲的span 的线程，在当前函数中 访问 PAGE CACHE之前解桶锁、
            在 将span挂到list之前加锁 能够保证 不同工作(获取/归还)之间的线程的并发度，并且第一个线程 仍然能够
            正确的拿到自己申请的span中的内存对象
   */
    list._mutex.lock();
    list.PushFront(span);

    return span;
}



// CentralCache类的成员函数
size_t CentralCache::FetchRangeObj(void* &start, void* &end, size_t batchNum, size_t byte_size) 
{
    // 计算中心缓存中桶的编号, 映射原理同Thread Cache中桶号的映射
    size_t index = SizeClass::Index(byte_size);
    
    
    //桶锁---解决多线程同时访问中心缓存中编号为index的桶的竞争问题
    _spanlists[index]._mutex.lock();

    //获得一个span
    Span* span = GetOneSpan(_spanlists[index], byte_size);



    assert(span);//判断span不为空
    assert(span->_freelist);//判断span下的自由链表不为空


    //从获得的span下的freelist取出batchNum个内存对象
    start = span->_freelist;
    end = start;

    //end指针最后走到span下的第min(actualNum, batchNum)个内存对象
    //actualNum记录实际获得的内存对象个数
    size_t actualNum = 1;

    /*
    情况一: 获得的span下的实际的内存对象数量足够batchNum个
    =>判断条件1: 从span中分配给线程的实际数量不能超过慢开始算法计算出的batchNum个(actualNum <= batchNum)
    情况二: 如果实际数量不足batchNum个
    =>判断条件2: ，则end走到“NextObj(end)为空”时就停止遍历
    */
    
    while (actualNum < batchNum && NextObj(end) != nullptr)
    {
        end = NextObj(end);
        actualNum++;
    }

    //span中的使用计数需要加上actualNum，每分配给thread cache 1个内存对象就要将使用计数加1
    span->_usecount += actualNum;

    span->_freelist = NextObj(end);
    NextObj(end) = nullptr;//end的next指针置空


    _spanlists[index]._mutex.unlock();
    return actualNum;//返回之前必须解锁，否则其他线程无法访问该桶
}




/*
由于Thread Cache在向 Central Cache 申请内存对象时，可能从多个Span中申请过;
eg:第一次从Spanlists[index]的第一个span中申请批量的内存对象，第一个Span申请完了之后，
   下次在申请时会从下面一个span申请内存对象；
申请的这些内存对象分配给线程后，线程归还时的顺序是不确定的；(归还到自由链表中使用的是头插法)
于是Thread Cache中自由链表中某一段链的内存对象有可能属于不同的Spans。
*/

/*
由于每次申请的span是系统的堆空间中连续的几个页面，且span中保存连续的几个页的起始页号: _PageId;
于是可以根据归还的每个内存对象的起始地址(ptr)，判断出这个span属于哪一页的内存(ptr >> 13(_PAGESHIFT));
即得到这个内存对象所在的页号，根据这个页号可以判断它属于哪一个span;
地址结构: 页号(19位) + 页内偏移量(13位)
*/
void CentralCache::ReleaseListToSpans(void* start, size_t byte_size)
{
    size_t index = SizeClass::Index(byte_size);

    //桶锁
    _spanlists[index]._mutex.lock();

    void* it = start;
    while (it != nullptr)
    {
        void* next = NextObj(it);
        //根据页号和span的映射关系，获得it所属的span
        
        Span* span = PageCache::GetInstance()->MapObjectToSpan(it);

        //头插法，将内存对象插入到对应span的自由链表中
        NextObj(it) = span->_freelist;
        span->_freelist = it;
        span->_usecount--;

        //说明span的切分出去的所有小块内存都回来了
        //这个span就可以再回收给page cache, page cache可以再尝试做前后页的合并
        if (span->_usecount == 0)
        {
            //从 Central Cache 中逻辑删除当前使用计数为0 的 Span
            _spanlists[index].Erase(span);
            span->_freelist = nullptr;
            span->_prev = nullptr;
            span->_next = nullptr;

           /*解除桶锁的原因:
            1.访问Page Cache 之前可以将桶锁解掉，因为这个时候当前线程归还的span已经从桶里删掉了，
              于是这时候不存在其他线程也能访问到这个span的可能;
            
            2.这个时候当前归还内存对象以及归还span的线程暂时不需要 Central Cache这个资源，可以暂时释放掉;
              解掉之后可以让其他线程从这个桶里的 其他span 中 (获取内存对象) 或者 (归还内存对象)；
              可以增加线程之间的并发度。
           */
            _spanlists[index]._mutex.unlock();

            //将span 归还给Page Cache之前需要访问 Page Cache这个资源，需要对其加锁
            PageCache::GetInstance()->_pagemutex.lock();
            
            PageCache::GetInstance()->ReleaseSpanToPageCache(span);

            //归还完之后可以 释放Page Cache这个资源， 需要解锁
            PageCache::GetInstance()->_pagemutex.unlock();

            //访问完 Page Cache 之后需要将桶锁锁上，以便于当前线程后续继续访问这个桶里的span 归还后面的内存对象
            _spanlists[index]._mutex.lock();

        }

        //找到下一个需要归还的内存对象
        it = next;
    }
    _spanlists[index]._mutex.unlock();
}