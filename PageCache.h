#pragma once

#include "Common.h"
#include "ObjectPool.h"//使用定长内存池绕开new/malloc的使用
#include "PageMap.h"



/*Page Cache类的设计模式:
PageCache也要设计成单例模式中，因为在整个项目中PageCache只会有一个，另外，PageCache所占内存较大，带来的系统开销较大，故不能有多份实例
*/

/*Page Cache 与 Central Cache的联动:
eg1:某个线程引起的Central Cache 向 Page Cache有 2页大小的 Span 的需求时，Page Cache 中为空时，
   向系统申请一个128页大小的内存组成一个 大Span；再把128页大小的 大Span 切分成 2 Pages 的 span 
   和 126 Pages 的 span；
   2 Pages的 span 返回给 Central cache 使用， 126 Pages的 span 挂到 Page Cache 的第126号桶中。

eg2:某个线程引起的Central Cache 向 Page Cache有 2页大小的Span 的需求时，Page Cache 中没有2页大小的
    span 时，于是会从Page Cache 中 3页的桶、4页的桶...一直往后搜索，直到找到一个不为空的桶；
	如 3页大小的桶不为空，从桶中取一个 3页大小的span切分成 2页大小 和 1页大小的span；
	将 2页大小的span 分配给 线程， 1页大小的span挂到 1号桶中。
*/

/*Page Cache 、Central Cache 与 Thread Cache的联动
1.当某个线程将不需要的内存对象挂到 Thread Cache 中的哈希桶时，此时挂的若干个内存对象还给Central Cache
  的对应的span；

2.当 Central Cache 中的某个span中的 usecount 等于0时，表示该 span 切分给 Thread Cache 的小内存对象全
  都回来了或者该span中的内存对象一直没有被使用过，此时可以将该 span 归还给 Page Cache；

3.Page Cache 通过 Central Cache 归还的 span 中的页号以及 span 占据的页数，查看该 span占据的内存空间
  的前后相邻页所在的 span 是否空闲(span 中的 usecount 计数器为0), 
  若前面页所在的 span 空闲，则与前面的 span 合并；若后面页所在的 span 空闲，则与后面的 span 合并；
  
4.Page Cache 可以合并出更大的内存空间(内存对象span), 从而解决内存小碎片问题; 以便于解决Central Cache
 (Thread Cache)申请更大的连续内存空间(内存对象span)时，没有相应大小的连续空间的问题。
*/

/*Page Cache 锁的设计模式:
在高并发内存池设计中，Page Cache 的锁之所以设计为全局锁而非哈希桶粒度的锁，主要基于以下几点关键原因：

---

### 1. **合并操作的原子性需求**
   - **内存合并的必要性**：Page Cache 的核心职责之一是管理大块内存页，并在释放内存时**合并相邻的空闲页**以避免碎片化。
						   合并操作通常需要检查相邻页的状态（如前后的 `span`），这可能导致跨多个哈希桶的操作。
   - **多锁死锁风险**：若每个哈希桶有独立锁，合并时需同时锁定多个桶的锁。
                       例如，线程1合并 `span A` 和 `span B` 时，若先锁 `A` 的桶再锁 `B` 的桶，
					   而另一线程2反向操作，可能导致死锁。全局锁通过强制串行化规避了这一风险。

---

### 2. **访问频率与锁竞争的权衡**
   - **低频访问场景**：相比频繁分配小内存的 Thread Cache 和 Central Cache，Page Cache 管理的是页级大内存块（如 4KB 或更大），
                       其分配/释放频率较低，全局锁的竞争压力相对较小，对并发度没有太多影响。
   - **锁粒度的性价比**：细粒度锁（哈希桶级别）虽能减少竞争，但会增加锁管理的复杂度（如锁数量、获取顺序）。
                         对于低频操作，全局锁的简单性在性能和维护成本上更具优势。

---

### 3. **实现复杂度的简化**
   - **跨桶操作的成本**：若采用哈希桶锁，每次合并操作需遍历多个桶并动态加锁，代码逻辑复杂度显著增加（例如需实现锁的按序获取、重试机制等）。
					     全局锁通过强制原子性简化了合并逻辑。
   - **数据结构一致性**：Page Cache 可能需要维护全局状态（如空闲页总数、大块内存的拓扑关系），全局锁天然保证了对这些状态的原子访问，无需额外同步机制。

---

### 4. **与 Central Cache 的对比**
   - **Central Cache 的细粒度锁**：Central Cache 为每个内存大小类（size class）使用独立锁，因为其高频分配小对象，且操作仅限单个桶内（如从 `size=64B` 的桶分配内存）。
   - **Page Cache 的全局性**：Page Cache 的合并操作本质上是全局的（需跨越多个页或桶），而 Central Cache 的操作是局部的（固定到某个 size class），因此锁粒度设计自然不同。

---

### 5. **实际案例参考**
   - **TCMalloc 的 PageHeap**：在 Google 的 TCMalloc 实现中，类似 Page Cache 的组件 `PageHeap` 使用全局锁管理页分配，而 `CentralFreeList`（类似 Central Cache）使用细粒度锁。这印证了全局锁在页级管理的合理性。

---


### 6. **切分操作的原子性需求**
   - 当线程1 和 线程2 分别通过 Central Cache 向 Page Cache申请 2页大小的 span 和 126 页大小的 span时，
     且Page Cache 中 2页大小 和 126页大小的 的哈希桶为空；
   - 此时 Page Cache 会向 堆申请 128页大小的 内存组成 span，并将该span 切分成 2页 和 126页大小的 span；
   
   - 若设计成桶锁，则 2号桶会被线程1锁住 和 126 号桶被线程2锁住；那么 Page Cache 申请的 128 页大小的
     span切分成两个 span， 但无法挂到 2号桶 和 126号桶上，此时会发生死锁。

---

### 7. **分配span时桶锁带来的高开销以及死锁**
   - 当线程1通过 Central Cache 向 Page Cache申请 2页大小的 span 时，若Page Cache 的桶中只有126 页大小的
	  span，则线程1需要线性的从2号桶 一路加锁 加到126号桶才行，这样不断地加锁带来的开销是非常高的。
   - 故将 Page Cache 的锁设计成一把全局锁，可以避免不断加锁解锁带来的高开销。
   - 并且 在这样的 Page Cache 分配内存的设计模式下，桶锁很容易引起死锁；
	 eg: 线程1申请 2号页的桶， 线程2 申请3号页的桶，若2号桶为空，则线程1往后申请3号页的桶时会发生死锁。
---

### 总结
Page Cache 的全局锁设计是**在性能、实现复杂度、碎片化控制之间权衡的结果**：通过牺牲一定并发性（因低频访问影响较小），换取合并操作的安全性和代码实现的简洁性。
对于需要高并发的细粒度操作（如小对象分配），锁粒度需细化（如 Central Cache），而大块内存管理的场景更适合粗粒度锁。
*/



/*Page Cache解决外碎片问题:
1.内碎片是由于要建立哈希桶方便管理内存对象并尽可能减少桶的数量而造成的，但是内碎片的问题仅仅是暂时的；
  且在当前内存对齐算法下，内碎片最多仅占10%，故内碎片问题可以忽略不计。
  eg: 线程1申请6B的内存对象，但进行内存对象对齐后会映射到8B的内存对象，虽然造成了2B的内碎片，但是
	  由于线程1后续会归还申请的 8B 的内存对象，故后续8B的内存对象仍然会分配给申请 8B内存对象的线程；
	  故哈希桶造成的内存碎片问题仅仅是暂时的。

2.外碎片是由于Page Cache向系统申请连续的若干页的大内存，然后将这个大内存切分成若干个小内存Span，并
  分配给 Central Cache 而造成的产生若干无法利用的小内存碎片，后续如果有大内存 Span申请的需求时，会
  造成重新向系统申请大内存的操作。
  故外碎片问题如果在从 Central Cache 回收 Span 时不及时解决，造成的内存浪费问题是非常大的，并且外碎片
  的存活周期是长期的；

3.于是Page Cache需要 在回收 Span时就要进行相邻 Span 的合并，以便于合并出更大的内存块，解决外部碎片的问题。
*/

/*
1.整个高并发内存池中能向系统申请实际内存的只有 Page Cache，申请到的内存通过Span对象中的 页号 和 页数进行
  逻辑上的管理，通过自由链表中的链接指针进行物理上的管理；

2.当Span 对象管理的内存 被合并，该对象也就没有必要存在了。

*/

class PageCache
{
public:
	static PageCache* GetInstance()
	{
		return &_sInstance;
	}
	
	//从Page Cache中获取k页大小的span
	Span* NewSpan(size_t k);


	// 释放空闲span回到Pagecache，并合并相邻的span
	void ReleaseSpanToPageCache(Span* span);

	//通过哈希表_PageidSpanMap建立页号 和 span 的映射
	Span* MapObjectToSpan(void* obj);



private:
	PageCache()
	{}

	PageCache(const PageCache&) = delete;

	static PageCache _sInstance;//饿汉模式

	//从1号下标开始映射哈希桶
	SpanList _spanlists[NPAGES + 1];//128个哈希桶，最大的span能切出4个256KB的对象

	//建立分配给 Central Cache 的 Span占据的_n个页面的每个页面的页号 和 Span 的一个映射关系
	//std::unordered_map<PAGE_ID,  Span*>_PageidSpanMap;

	TCMalloc_PageMap3 <PtrBits - PAGE_SHIFT> _PageidSpanMap;

	//使用定长内存池解决 创建span对象时使用new 的问题，以便于在高并发内存池中避开 使用new 和 delete的使用
	ObjectPool<Span>_SpanPool;

public:
	std::mutex _pagemutex;//全局锁
};
