#pragma once
#include <iostream>
#include <vector>
#include <time.h>
#include <assert.h>
#include <thread>
#include <mutex>//互斥锁
#include <algorithm>
#include <unordered_map>

using std::cout;
using std::endl;


static const size_t MAX_BYTES = 256 * 1024;//ThreadCache的最大可申请内存
static const size_t NFREELISTS = 208;//ThreadCache 和 CentralCache中哈希桶的数量
static const size_t NPAGES = 128;//PageCache中哈希桶的数量，最大的span管理128页的内存大小
static const size_t PAGE_SHIFT =13;//一个页的大小是8KB(2^13B)，用于右移运算来计算字节为单位的内存所占页数



//条件编译解决不同系统下页号的表示范围问题
//_WIN32：用于判断是否是windows系统(对于跨平台程序，如可能既运行在windows也运行在linux系统)
/*_WIN64：用于判断编译环境是x86还是x64
  在x86(win32)配置下，_WIN32有定义，_WIN64没有定义
  在x64配置下，_WIN32和_WIN64都有定义
 */
//故在写如下条件编译时，需要先将_WIN64的判断放在前面，在x64环境下才会进入_WIN64的编译条件
//否则，如果_WIN32放在前面，无论在哪个编译环境下，都会先进入_WIN32的编译条件
#ifdef _WIN64
	typedef unsigned long long PAGE_ID;//WIN64系统下PAGE_ID页号使用unsigned long long的变量
	const size_t PtrBits = 64;//64位系统下的地址位数
#include <Windows.h>
#elif _WIN32
	typedef size_t PAGE_ID;//WIN32系统下PAGE_ID页号使用size_t类型的变量
	const size_t PtrBits = 32;//32位系统下的地址位数
#include <Windows.h>
#else
	//linux、macos
#endif


//直接去堆上按页申请空间
inline static void* SystemAlloc(size_t kpage)
{
#ifdef _WIN64
	//左移13位等价于乘以2^13，即8K，申请kpage * 8K字节，即kapge页
	void* ptr = VirtualAlloc(0, kpage * (1 << PAGE_SHIFT), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#elif _WIN32
	//左移13位等价于乘以2^13，即8K，申请kpage * 8K字节，即kapge页
	void* ptr = VirtualAlloc(0, kpage * (1 << PAGE_SHIFT), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
	//linux下brk mmap等
#endif
	if (ptr == nullptr)
		throw std::bad_alloc();
	return ptr;
}

//将大内存还给堆
inline static void SystemFree(void* ptr)
{
#ifdef _WIN64
	VirtualFree(ptr, 0, MEM_RELEASE);
#elif _WIN32
	VirtualFree(ptr, 0, MEM_RELEASE);
#else
	//linux下sbrk unmmap等
#endif
}


//取到内存对象obj的前4或8个字节的内容，用一个指针返回
//指针对象对应一块内存空间，返回的就是一个void*的对象，其实就是返回一块void*大小的内存空间
//返回引用才能对这块空间进行写，否则只是使用临时变量进行的一份拷贝，具有常性(const)，只能读
inline void*& NextObj(void* obj)
{
	return *((void**)obj);
}

//管理切分好的小内存空间(内存对象)的自由链表
class Freelist
{
public:
	//插入一个内存对象
	void Push(void* obj)
	{
		//头插
		assert(obj);
		NextObj(obj) = _freelist;
		_freelist = obj;
		
		++_size;//插入内存对象时更新计数器size的值
	}

	//插入多个内存对象组成的链表
	void PushRange(void* start, void* end, size_t nums)
	{
		NextObj(end) = _freelist;
		_freelist = start;
		

		//测试验证 + 条件断点
		/*int i = 0;
		void* cur = start;

		while (cur)
		{
			cur = NextObj(cur);
			++i;
		}

		if (nums != i)
		{
			int x = 0;
		}*/

		_size += nums;
	}

	void* Pop()
	{
		assert(_freelist);
		//头删
		void* obj = _freelist;
		
		_freelist = NextObj(_freelist);

		--_size;//弹出内存对象时更新计数器size
		return obj;
	}

	//从自由链表中取出批量个内存对象还给中心缓存
	void PopRange(void*& start, void*& end, size_t batchNum)
	{
		assert(batchNum <= _size);

		start = _freelist;
		end = _freelist;

		size_t n = 1;
		while (n < batchNum)
		{
			end = NextObj(end);
			n++;
		}

		_freelist = NextObj(end);
		NextObj(end) = nullptr;

		_size -= batchNum;//取走batchNum个，更新内存对象剩余个数size
	}

	bool Empty()
	{
		return _freelist == nullptr;
	}

	//用于ThreadCache从中心缓存获取对象的个数时，慢开始算法的实现
	//类似于计网中慢开始算法的拥塞窗口大小
	size_t& MaxSize()
	{
		return _MaxSize;
	}

	//计数器，返回自由链表为空时一次向Central Cache批量申请的批量数
	size_t CurrentBatchNum()
	{
		return _MaxSize;
	}

	size_t Size()
	{
		return _size;
	}


private:
	void* _freelist = nullptr;
	size_t _MaxSize = 1;//记录每次自由链表中没有内存对象时向Central Cache一次批量申请的个数
	size_t _size = 0;//记录自由链表中的可用(空闲)内存对象的个数
};

//计算内存对象大小的对齐映射规则
class SizeClass
{
public:
	//不定长对齐哈希桶
	// freelist[0]:8 Bytes内存对象 -> 
	// freelist[1]:16 Bytes内存对象 ->
	//   ...
	// freelist[15]:128 Bytes内存对象 ->
	// freelist[16]:144 Bytes内存对象 ->
	// freelist[17]:160 Bytes内存对象 ->
	// ...
	// 
	// 对齐数t: 数量是t的整数倍
	// 最小的内存对象必须是8(64位系统下才能存下一个指针)
	// 整体控制在最多10%左右的内碎片浪费
	// 计算对应区间[a, b]内的哈希桶数量: 
	// 若对齐数是t，则从a - 1开始每t个单位就有一个哈希桶，故[a, b]内共有: (b - (a - 1)) / t个哈希桶
	// 如: 1 - 128按8对齐，故从0开始每8个单位就是一个哈希桶，于是总共的哈希桶数量 = 128 - 0 / 8 = 16
	// 129 - 1024按16对齐，故从128开始每16个单位就是一个哈希桶，总共的哈希桶数量就是从128开始到1024的增量中有多少个16
	// 即 （1024 - 128）/ 16 = 896 / 16 = 56个
	// [1,128]               8byte对齐数           freelist[0,16) 16
	// [128+1,1024]          16byte对齐数          freelist[16,72) 56
	// [1024+1,8*1024]       128byte对齐数         freelist[72,128) 56
	// [8*1024+1,64*1024]    1024byte对齐数        freelist[128,184) 56
	// [64*1024+1,256*1024]  8*1024byte对齐数      freelist[184,208) 24
	// 共 208个哈希桶
	// 若size ∈ [a, b]，对齐数是t，则key = (size / t + 1) * t
	// key = a - 1 + t * down[(size - (a - 1) + t - 1 ) / t] = a - 1 + t * down[(size - a + t) / t](down[x]为下取整)
	//     = t * (a - 1 /t + down[(size - a + t) / t]) = t * (a / t + down[size - a + t) / t) = t * down(size + t / t) = t * down(size / t + 1)
	// eg: 如129，映射到的哈希桶为 key = 128 + 16 * 1 = 128 + 16 * up[(129 - 128) / 16] = 128 + 16 * down((129 - 128 + 15) / 16)
	//                                 =“144 Bytes”号的哈希桶

	//size是对应申请的内存对象大小，AlignNum是size所在哈希映射区间的对齐数, 
	/*size_t _Roundup(size_t size, size_t AlignNum)
	{
		//start是所在区间的起始值,
		//return (start - 1) + AlignNum * ((size - start + AlignNum) / AlignNum);
		//allocate是分配的字节数
		size_t allocate;
		
		if (size % AlignNum)
		{
			//size不是对齐数的整数倍，则向对齐数的整数倍向上取整
			//size = k * (alignnum) + r => allocate = (k + 1)* alignnum
			allocate =  (size / AlignNum + 1) * AlignNum;
		}
		else
		{
			allocate = size;
		}
		return allocate;
	}*/

	//alignNum是对齐数(对齐单位)
	static inline size_t _Roundup(size_t size, size_t alignNum)
	{
		//size = k * alignNum + r(0 <= r < alignNum) = k * t + r
		//要将size映射到t的整数倍且该整数倍要 >= size,可以采用如下方法 
		//size + t - 1 = k * t + r + t - 1 = (k + 1) * t + r - 1
		//注: 直接加t会将r = 0的数也向上映射到(k + 1) * t的关键字处
		//若r = 0， (k + 1) * t + r - 1 = k * t + (t - 1)
		//若r > 0,  (k + 1) * t + (r - 1) (0 <= r - 1 < t - 1)
		//此时只需要将余数从上式中除去就是所求的结果
		//由于余数的结果一定是小于t的，故只要将size + t - 1的二进制后面小于t的数位中的值全部取为0即可
		//如: t = 16 = 2^4, 只需将size + t - 1中(2^3 - 2^0)这4位全部与上0且其他高位不变，就可以得到最后的结果
		//这样做等价于(size + t - 1) & (~(t - 1))
		return ((size + alignNum - 1) & ~(alignNum - 1));
	}

	//对齐值的计算 -- 结果以Bytes 为单位
	static inline size_t Roundup(size_t size)
	{
		//assert(size <= MAX_BYTES);

		if (size <= 128)
		{
			return _Roundup(size, 8);
		}
		else if (size <= 1024)
		{
			return _Roundup(size, 16);
		}
		else if (size <= 8 * 1024)
		{
			return _Roundup(size, 128);
		}
		else if (size <= 64 * 1024)
		{
			return _Roundup(size, 1024);
		}
		else if (size <= 256 * 1024)
		{
			return _Roundup(size, 8 * 1024);
		}
		//解决大于256KB的内存申请时的对齐值的问题
		else 
		{
			//申请的内存大小 > 256KB，直接向 Page Cache申请 或 向 系统堆 申请
			//以 1页为对齐单位进行对齐，如: 257KB 会根据对齐算法 对齐到 1页大小整数倍的上取整 = 33页
			//返回值是字节数
			return _Roundup(size, 1 << PAGE_SHIFT);
		}
	}

	//计算出每个对齐值在对应区间内的桶的映射偏移量
	static inline size_t _Index(size_t add_bytes, size_t alignbits)
	{
		//eg: 129Bytes的对齐值是144,144应该映射到对应区间内的0号桶，这个区间的对齐单位是16B
		//加上前面所有区间的桶数--16个桶，映射到最终的16号桶
		//add_bytes是相对于区间起点 - 1的增量值[128 + 1, 1024]的区间起点 - 1是128
		//129相对于该值的增量是1，在区间[128 + 1, 143 = 128 + 15]都会映射到上述大区间内中的0号桶
		//return (add_bytes + alignNum - 1) / alignNum - 1;(上取整 - 1) <=>下取整
		//若申请的是 144B，则也应该映射到 0号桶，此时若用144 - 128 = 16 / 16 = 1;
		//故对于整除的情况，上取整 = 下取整，此时结果应该是下取整 - 1 = 上取整 - 1
		//故采用“上取整 - 1” 的算法可以考虑到 整除 和 非整除 两种情况
		return ((add_bytes + ((size_t)1 << alignbits) - 1) >> alignbits) - 1;
	}

	//计算映射的那一个自由链表桶
	static inline size_t Index(size_t bytes)
	{
		assert(bytes <= MAX_BYTES);

		//计算出每个区间的链表数量
		static int group_array[4] = { 16, 56, 56, 56 };

		if (bytes <= 128)
		{
			//该区间的对齐值8 = 2^3
			return _Index(bytes, 3);
		}

		else if (bytes <= 1024)
		{
			return _Index(bytes - 128, 4) + group_array[0];
		}

		else if (bytes <= 8 * 1024)
		{
			return _Index(bytes - 1024, 7) + group_array[0] + group_array[1];
		}

		else if (bytes <= 64 * 1024)
		{
			return _Index(bytes - 8 * 1024, 10) + group_array[0] + group_array[1] + group_array[2];
		}

		else if (bytes <= 256 * 1024)
		{
			return _Index(bytes - 64 * 1024, 10) + group_array[0] + group_array[1] + group_array[2] + group_array[3];
		}
	}


	//一次thread cache从中心缓存获取最多多少个对象
	//即获取的最多内存对象的上限(用于慢开始算法，类似于计网的拥塞阈值大小)
	static size_t NumMoveSize(size_t size)
	{
		assert(size > 0);

		// num ∈[2, 512]: 一次批量移动多少个对象的(慢开始)上限值

		size_t num = MAX_BYTES / size;

		//申请的内存对象很大，一次从中心缓存批量给2个对象
		if (num < 2)
			num = 2;

		//申请的内存对象很小，一次从中心缓存批量给512个对象
		if (num > 512)
			num = 512;

		//申请的内存对象适中，一次给num个对象
		return num;
	}

	//根据Thread Cache向 Central Cache申请的内存对象大小size
	//如果Central Cache 中没有切分好的、管理size大小内存对象的Span, 则Central Cache 向 Page Cache 申请一个空闲的Span
	//该Span占据的页数需要根据size来映射
	//计算Central Cache中spanlist 没有非空的span对象时，一次向Page Cache获取几个页大小的span对象
	//计算一次向系统获取几个页
	//单个对象 8byte
	//...
	//单个对象 256byte
	static size_t NumMovePage(size_t size)
	{
		/*
		 按照Central Cache 一次可能分配给 Thread Cache 的 最多的 size大小的内存对象个数
		(即Thread Cache 一次批量要的最大数量), 来计算一次获取的页数
		*/
		
		//计算出Central Cache 一次分配给 Thread Cache 的最多的size大小的内存对象个数--- 最大上限批量
		size_t num = NumMoveSize(size);

		//计算出一次Central Cache 分配给 Thread Cache最多的这么多个size大小内存对象所占的内存大小是 npage(Bytes)
		size_t npage = num * size;

		//计算出npage(Bytes) 内存对应的页数，即从 Page Cache分配这么多页大小的span给 Central Cache
		npage >>= PAGE_SHIFT;//PAGE_SHIFT是13 (一页 = 8KB = 2^13 B, npage / 8KB <=> npage >> 13)

		//如Thread Cache申请8byte的内存对象，一次最多从 中心缓存 分配给512个对象
		// npage = 512 * 8 Byte = 4KB < 8KB => npage >>= 8KB (npage / 8KB) = 0
		// 此时分配 1个页 的 span 给 中心缓存 , 即可切出 1024个对象
		if (npage == 0)
		{
			npage = 1;
		}
		return npage;
	}

};

//central cache结构：
//freelist[0](8bytes)-> span1 <=> span2 <=> ... <=>nullptr
//每个span又会被切成若干小的内存对象(具体大小对应于哈希桶的映射对象大小)
//span不仅给central cache用，也会给page cache用(Central Cache将span归还给page cache)，所以放到common.h中

//span节点的定义，管理以页为单位的大块内存(用于centralcache中的哈希桶的链式结构)
/*
span节点：
	  (prev)<-[span]->(next)
				|
				v(_freelist)
*/
struct Span
{
	PAGE_ID _PageId = 0;//该span所占据的大块内存的起始页号
	size_t _n = 0; //大块内存占据的页数

	
	//双向链表
	Span* _next = nullptr;
	Span* _prev = nullptr;
	//span直接的链接的实现区别于freelist中的小内存对象链接的实现，是用带头节点的循环双向链表的方式实现
	/*
	因为当一个span划分出的若干小内存对象被ThreadCache全部归还后，Central Cache需要将该span占据的大块内存
	归还给上一层的Page Cache中，既在Central Cache中相应的哈希桶中删除该span
	而在所有链式结构中，只有循环双向链表删除任意一个节点的时间复杂度是O(1)
	*/


	
	size_t _objSize = 0;//记录当前Span中切好的小内存对象的大小
	/*用于解决释放内存对象时，避免传内存对象大小的参数
	通过内存对象的起始地址，找到所在页号，根据页号查找哈希表，找到该页所在Span
	进而根据这个Span 切分出来的内存对象的大小，可以确定释放的内存对象的大小
	*/
	
	
	//使用计数，用于记录span中在相应的哈希桶中，被切好的若干小块内存已经被分配给Thread Cache的数量
	size_t _usecount = 0;
	/*
	eg:在8Bytes的哈希桶中，span = 1Page， 故被切成1024块(一页 = 8KB)
	当_usecount = 0时，该span切成的1024块小内存没有使用一块，当_usecount = 1024时，该span切成的1024块小内存已经全部被使用完，
	Thread Cache向该span申请8Byte的小内存时，该span的_usecount++,表示已经被使用的数量加1，
	
	Thread Cache向该span归还8Byte的小内存时，该span的_usecount--，表示已经归还了1块小内存，
	即使用的数量减1
	当1024块小内存对象全部归还后，_usecount = 0, Central Cache需要将该span占据的大块内存归还给PageCache
	*/

	//span下面挂着若干小的内存对象
	void* _freelist = nullptr;//从span中切好的小块内存的自由链表


	bool _isUse = false;//判断当前 Span 是否在使用，用于Page Cache 合并空闲(未使用)的相邻Span
};


//Span的带头结点的循环双向链表的实现
/*
Spanlist: [head] <=> [first node] <=> [second node] <=> ... <=> [last node]
span节点：
	  (prev)<-[span]->(next)
				|
				v(_freelist)

*/
class SpanList
{
public:
	//初始化
	SpanList()
	{
		_head = new Span;
		_head->_prev = _head, _head->_next = _head;
	}

	//Begin指向链表中第一个有效节点
	Span* Begin()
	{
		return _head->_next;
	}

	//End指向链表中最后一个有效节点的后一个位置 --- 即头结点
	Span* End()
	{
		return _head;
	}

	bool Empty()
	{
		return _head->_next == _head;
	}

	/*size_t Count()
	{
		Span* it = Begin();
		size_t cnt = 0;
		while (it != End())
		{
			++cnt;
			it = it->_next;
		}
		return cnt;
	}*/
	
	//在spanlist中头插一个新的span节点
	void PushFront(Span* newSpan)
	{
		Insert(Begin(), newSpan);//this-> Begin()
	}

	Span* PopFront()
	{
		Span* front = _head->_next;
		Erase(front);
		return front;
	}

	//在Spanlist中的pos位置插入一个newSpan节点(在pos节点前面一个节点后面插入一个newspan节点)
	void Insert(Span* pos, Span* newSpan)
	{
		assert(pos);
		assert(newSpan);

		Span* prev = pos->_prev;

		//prev <=> newSpan <=> pos
		prev->_next = newSpan;
		newSpan->_prev = prev;


		pos->_prev = newSpan;
		newSpan->_next = pos;	
	}

	//删除pos位置的span节点, 
	//因为需要将从spanlist中删除的span对应的大块内存归还给PageCache，故不需要物理上delete span
	void Erase(Span* pos)
	{
		assert(pos);
		assert(pos != _head);

		Span* prev = pos->_prev, * next = pos->_next;

		//prev <=> next
		prev->_next = next, next->_prev = prev;

	}

	

private:
	Span* _head;//定义一个头节点指针

public:
	std::mutex _mutex;//桶锁
	
};