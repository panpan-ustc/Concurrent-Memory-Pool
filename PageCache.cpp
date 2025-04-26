#include "PageCache.h"


PageCache PageCache::_sInstance;//初始化单例


/*不能使用 usecount == 0 判断 Span 是否在Page Cache中的 原因:
	  1. 对于 usecount == 0 且 在 Page Cache中的 Span， 这些是可以合并的；
	  2. 对于 usecount == 0 且 在 Central Cache 中的 Span:
		 (1)如果 Span是 分配过内存对象 后因为 线程 归还内存对象导致的 usecount减为0，这些Span理论上是可以
			合并的，因为这些Span 虽然暂时还没有被归还到 Page Cache中，但是他后面也会归还到 Page Cache中
			并参与相邻 Span 之间的合并；

		 (2)但是如果 Span 是其他线程刚 通过 Central Cache 向 Page Cache申请的，此时刚得到这个Span，他的usecount
			也等于 0，但是这个 Span 是要放到 Central Cache 中的，并且是要分配给 申请的线程 使用的，故这一类的
		    Span 是不能参与 空闲Span 之间的合并的。
*/

/*
  1.在没有将 Page Cache 中的 Span 在哈希表中建立 相应映射时；
    不能使用Page Cache 中的哈希表是否存在映射关系 判断 Span 是否在Page Cache中的 原因:
     (1).因为每一个分配给 Central Cache 的 Span 占有内存的所有页面， 都会在 哈希表中建立映射关系；
	    已申请且没有被分配给 Central Cache 的 Span (已申请的 且 仍然留在 Page Cache)在哈希表中不会建立映射；

	 (2).但是并不代表 没有建立映射的 页面组成的 Span就在 Page Cache中，因为这些页面有可能还没有申请出来。

  2.在Span 中加入 状态标志位 isUse 后；
    (1).此时可以将 Page Cache 中的 Span 在哈希表中建立 相应映射；
	(2).此时哈希表中存在的 Span 只有两种可能: 挂在 Page Cache中、已经分配给 Central Cache
	    于是可以通过 哈希表的映射 + isUse标志位 排除掉分配给 Central Cache 的Span 的可能，这样就可以
		筛选出 挂在 PAGE CACHE 中的 Span
*/


//释放空闲span回到Pagecache，并合并相邻的、未被分配给 Central Cache的span
void PageCache::ReleaseSpanToPageCache(Span* span)
{
	//解决超级大内存(大于128页)的归还问题,直接还给堆
	if (span->_n > NPAGES)
	{
		void* ptr = (void*)(span->_PageId << PAGE_SHIFT);
		SystemFree(ptr);
		//delete span;
		_SpanPool.Delete(span);
		return;
	}
	
	//对于≤128页的Span，直接还给 Page Cache进行内存合并 并 管理起来
	//对span前后的页，进行尝试性的合并，合并后缓解内存碎片(外碎片)问题

	/*合并时的要点:
	1.合并时需要将当前归还的Span在哈希表中的所有映射给删除(不删除其他线程归还span时合并这个Span会出现野指针问题);
	  以及合并时前面相邻的Span 的两个映射 和 后面相邻的Span 在哈希表中建立的两个映射 给删除，
	  并将合并后的新的大Span 在哈希表建立 首尾页号的映射关系。以保证 后续线程归还 Span 时，
	  避免重复合并空闲 Span 的操作以及野指针问题.
	
	2.合并时需要考虑当前归还的Span 和前后相邻的Span合并后，大小是否会超过 128页 的情况；
	  发现两个 Span 合并时 超过 128页 的大小，就停止合并
	  eg: 初始时，线程1申请 256KB 的内存对象，对应到Page Cache中申请了一个128页大小的Span，并切分了一个64页的Span
	      给 Central Cache转而给线程1使用；
		  
		  线程2申请 8B 的内存对象，Page Cache中会从64页的Span 中切分出1页给给 Central Cache转而给线程2使用；

		  线程3申请 256KB 的内存对象，Page Cache 中没有64页大小的 Span，于是又新申请一个 128页大小的Span，
		  并切分了一个64页的Span给 Central Cache转而给线程1使用；

		  且前后两个申请的 128页大小的Span 在堆上是连续的；
		  当线程1、2使用的内存对象全部归还后，第一个128页大小的 Span就 完成了合并 并放入 Page Cache中，
		  当线程3归还后，也会进行合并，此时和前面 128页大小的 Span 合并时会 超过128页大小。

	*/
	
	//将当前归还的Span在哈希表中的所有映射全部删除
	
	for (size_t i = 0; i < span->_n; i++)
	{
		PAGE_ID currentId = span->_PageId;
		_PageidSpanMap.erase(currentId + i);
	}

	
	//只合并Page Cache 中已申请的内存
	
	//向前合并
	while (1)
	{
		PAGE_ID prevId = span->_PageId - 1;
		Span* prevSpan = (Span*)_PageidSpanMap.get(prevId);

		/*三个停止合并的条件
		条件1: 当前Span 前(后)面的页面没有被Page Cache 所申请；
		条件2: 当前Span 前(后)面的页面正在 Central Cache 中被使用；
		条件3: 当前Span 前(后)面合并后的大小超过 128页，无法在Page Cache 中管理
		*/
		//如果前面相邻的页面没有在哈希表中建立映射，表示前面的页面所在内存没有被Page Cache申请
		//此时不用合并
		if (prevSpan  == nullptr)
		{
			break;
		}
		

		//前面相邻的 Span 是仍然在 Central Cache 中被使用的 Span，合并前面相邻Span的操作停止
		if (prevSpan->_isUse == true)
		{	
			break;
		}

		//如果前面相邻的Span 的页数 加上 当前Span 的页数 > 128，此时Page Cache的哈希桶没办法管理，停止合并
		if (prevSpan->_n + span->_n > NPAGES)
		{
			break;
		}
		
		//前面相邻的 Span 是在Page Cache 中的内存、未被正在使用且合并后的大小不会溢出，可以合并

		span->_PageId = prevSpan->_PageId;
		span->_n += prevSpan->_n;

		//先将前面已经合并的 prevSpan 从 Page Cache的哈希桶中逻辑上删除，
		//以免物理上删除后，其他线程访问Page Cache时，访问到 prevSpan 发生野指针情况
		_spanlists[prevSpan->_n].Erase(prevSpan);

		//并且删除prevSpan在哈希表中的首尾页号的映射
		_PageidSpanMap.erase(prevSpan->_PageId);
		_PageidSpanMap.erase(prevSpan->_PageId + prevSpan->_n - 1);

		//前面已经合并的Span对象管理的内存已经被span接手管理，故前面合并的Span对象的实体也就没必要存在了
		//故可以对prevSpan对象的实体进行物理上删除
		
	    //delete span;将span归还给定长对象池
		_SpanPool.Delete(prevSpan);
	}

	//向后合并
	while (1)
	{
		PAGE_ID  nextId = span->_PageId + span->_n;
		Span* nextSpan = (Span*)_PageidSpanMap.get(nextId);
		
		//哈希表中没有映射, 页号为nextId的内存未被Page Cache申请下来, 该内存仍然属于系统
		if (nextSpan == nullptr)
		{
			break;
		}

		//后面的Span仍然在Central Cache中被使用, 不能合并
		if (nextSpan->_isUse == true)
		{
			break;
		}

		//两个span合并起来的大小超过 Page Cache最大可以管理的 Span大小, 停止合并
		if (nextSpan->_n + span->_n > NPAGES)
		{
			break;
		}

		//可以合并, 起始页号不变
		span->_n += nextSpan->_n;

		_spanlists[nextSpan->_n].Erase(nextSpan);

		_PageidSpanMap.erase(nextSpan->_PageId);
		_PageidSpanMap.erase(nextSpan->_PageId + nextSpan->_n - 1);
		
		//delete nextSpan
		_SpanPool.Delete(nextSpan);
	}
	
	//由于整个合并过程都是基于线程归还的span进行的，而这个span之前的isUse标志位是true
	//需要更新为false
	span->_isUse = false;

	//再将合并后的新的大span插入对应 Page Cache 中的哈希桶
	_spanlists[span->_n].PushFront(span);

	//再将合并后的新的大Span在哈希表中建立映射
	if(_PageidSpanMap.Ensure(span->_PageId, 1)) 
		_PageidSpanMap.set(span->_PageId, span);
	if (_PageidSpanMap.Ensure(span->_PageId + span->_n - 1, 1))
		_PageidSpanMap.set(span->_PageId + span->_n - 1, span);
}


//从Page Cache中获取k页大小的span
Span* PageCache::NewSpan(size_t k)
{
	//assert(k >= 1 && k <= NPAGES);

	assert(k >= 1);

	if (k > NPAGES)
	{
		//申请的页数大于 128页，直接向系统堆申请
		//Span* SuperBigSpan = new span;
		Span* SuperBigSpan = _SpanPool.New();
		void* ptr = SystemAlloc(k);
		SuperBigSpan->_PageId = (PAGE_ID)ptr >> PAGE_SHIFT;
		SuperBigSpan->_n = k;

		/*
		1.在哈希表中记录这个超级大的span 和 起始页号的映射，以便于线程归还这个大内存时，
		  可以通过起始地址找到这个span;
		2.当线程归还小的Span时，由于这个 Span过大，故不会发生合并
		*/
		if(_PageidSpanMap.Ensure(SuperBigSpan->_PageId, 1))
			_PageidSpanMap.set(SuperBigSpan->_PageId,  SuperBigSpan);

		return SuperBigSpan;
	}

	//先检查第k个桶里面有没有span，有直接返回，无需切分
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
	
	//按照桶编号的大小从小到大依次检查一下后面的桶里面有没有span，如果有，可以将他进行切分
	//例如找到第i号桶的span，将该i页大小的span切分成一个 k页大小的 span 和 一个 i-k 页大小的span
	//k页大小的 span 返回给 central cache， i - k页大小的 span 挂到第i - k号桶中去
	for (size_t i = k + 1; i <= NPAGES; i++)
	{
		if (!_spanlists[i].Empty())
		{
			Span* iSpan = _spanlists[i].PopFront();//i页大小的span
			
			//k页大小的span

			//Span* kSpan = new span;
			Span* kSpan = _SpanPool.New();

			//在iSpan的头部切下一个k页的小span下来
			//k页的span返回
			//iSpan 映射到对应的位置
			kSpan->_PageId = iSpan->_PageId;
			kSpan->_n = k;

			iSpan->_PageId += k;
			iSpan->_n = i - k;

			_spanlists[i - k].PushFront(iSpan);

			/*将新申请的、切分后的 (i - k)页大小的 且留在 Page Cache中的 iSpan 在哈希表中建立映射，
			  以便于后续合并空闲 Span 时查找相应的信息；
			  
			  1.哈希表给线程使用时，不会建立错误映射；
			    当线程 归还内存对象 给 Central Cache 时才会用到哈希表；此时线程是要获取 每个内存对象所属的、
				且在 Central Cache中的 Span，而线程 归还的那些内存对象一定是 已经分配给 Central Cache的；
				故在 Page Cache 中的 Span 以及 其在哈希表中建立的映射 一定不会被访问。
			  
			  2.由于在 相邻Span 合并时，只需要用到 挂在 Page Cache 中的 Span 的起始页号 和 末尾页号；
			    于是 只需要在哈希表中建立 起始页号、末尾页号 和 Span* 之间的映射关系即可。
				分析: 
				  (1) 往前合并时，只需 用当前空闲的 Span 的起始页号 - 1,就可以得到前一个Span 的末尾页号；
				      通过末尾页号的映射就可以得到 前一个相邻的 prevSpan，并通过 prevSpan 的使用状态位
					  isUse 可以判断出它是否是 挂在 Page Cache 中的Span。

				  (2) 往后合并时，只需 用当前空闲的 Span 的起始页号 + 所占页数,就可以得到后一个Span 的
				      起始页号，并通过得到的起始页号的映射关系就可以得到 后一个相邻的 nextSpan，并通过
					  nextSpan 的使用状态位 isUse就可以判断出 它是否是 挂在Page Cache 的Span
			*/
			//建立 iSpan 的起始页号的映射
			if(_PageidSpanMap.Ensure(iSpan->_PageId, (size_t)1)) _PageidSpanMap.set(iSpan->_PageId,  iSpan);

			//建立 iSpan 的末尾页号的映射
			if(_PageidSpanMap.Ensure(iSpan->_PageId + iSpan->_n - 1, 1)) _PageidSpanMap.set(iSpan->_PageId + iSpan->_n - 1, iSpan);


			/*
			在Page Cache 每次分配 新的Span 给 Central Cache 时，建立已分配的 Span 和 他所占据的内存的每一个页面的页号的映射,
			以便于后续在回收 Thread Cache 的自由链表中的 内存对象时，可以根据内存对象页号 更快的查找到 内存对象 所属的Span
			*/
			//建立 kSpan 占据的_n个页面的Pageid 和 Span的一个映射关系
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
	
	// 走到这个位置说明没有大页的span
	// 就去找堆要一个128页的span
	Span* bigSpan = _SpanPool.New();
	void* ptr = SystemAlloc(NPAGES);
	bigSpan->_PageId = (PAGE_ID)ptr >> PAGE_SHIFT;
	bigSpan->_n = NPAGES;

	_spanlists[bigSpan->_n].PushFront(bigSpan);

	//这里使用递归，是避免重复上面已经实现的切分bigSpan的代码
	return NewSpan(k);
}


Span* PageCache::MapObjectToSpan(void* obj)
{
	/*由于有线程在读哈希表，也有线程在写这个哈希表,故需要对哈希表加锁，
	  因为修改哈希表的时候哈希表内的数据结构会变化，而哈希表本身这种数据结构变化的时候底层没有加锁；
	  故如果有线程这时候读取哈希表，会出现地址映射不正确的情况(修改并没有完成)。
	  这是由于哈希表这种数据结构本身实现的局限性导致的，而内存池中线程对于数据的访问并没有冲突

	1.写线程在写哈希表的时候都是调用NewSpan这个函数，这个函数需要访问 Page Cache，
	  故在写之前就已经加上了Page Cache这个大锁;
	2.读线程在读哈希表的时候没有加锁，而读线程都是通过MapObjectToSpan这个映射函数读取哈希表；
	  故在MapObjectToSpan这个函数内加上Page Cache这个大锁就行
	*/

	//把锁给unique_lock这个类，这样加锁的好处就是出了这个函数的作用域，他会自动解锁
	//std::unique_lock<std::mutex> lock(_pagemutex);
	
	//case1: New Span 申请一个新的Span 会修改基数树，Release Span 归还Span 时会修改基数树
	//case2: 读取基数树中的Span指针的数据只有在 查找内存对象所在的Span时才会发生
	//而case1中的Span 要不刚在Page Cache中申请出来 / 即将挂在 Page Cache 中 / 已经从Central Cache中释放
	//case2 中的Span 只有可能在Central Cache中，故这两类操作不可能访问同一份数据，于是读写线程之间没有互斥的关系
	//读线程 与 读线程之间对基数树访问时，也没有互斥(数据冲突/数据冒险)的关系
	//写线程 与 写线程之间对基数树访问时，同时也会对Page Cache发生访问，因此他们之间的互斥是由Page Cache这份大锁保证的
	//故MapObjectToSpan这个函数内不需要加锁

	//计算内存对象的页号
	PAGE_ID pageid = (PAGE_ID)obj >> PAGE_SHIFT;

	Span* ret = (Span*)_PageidSpanMap.get(pageid);

	assert(ret != nullptr);

	return ret;
}