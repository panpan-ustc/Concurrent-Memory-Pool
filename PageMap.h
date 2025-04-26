#pragma once
#include "Common.h"
#include "ObjectPool.h"

//tcmalloc中的基数树实现源码 --- 解决 _PageidSpanMap 加锁解锁以及其他的时间浪费 带来的效率问题

// Single-level array  ---(32位下可用)直接定址法的一个哈希表(数组), key = 页号， value = 地址(指针)
// BITS = 32(64) - PAGE_SHIFT => 页号的位数， 类似于原生页表的实现
template <int BITS>
class TCMalloc_PageMap1 
{
private:

	static const int LENGTH = 1 << BITS;//数组的长度是 (2^BITS),对应 (2^BITS页)
	void** array_;//数组的每个元素是一个指针(地址)void*, 数组名是首元素的地址 --- 指向void*的一个指针void**

	//array_[i]:存放i号页面所在的Span的指针Span*，占用内存 2^19 * 4B = 2MB
	

public:
	typedef uintptr_t Number;
	//tcmalloc中开辟空间需要传申请空间的函数指针 (malloc), 由于需要避开malloc的使用，故这里直接不使用
	//explicit TCMalloc_PageMap1(void* (*allocator)(size_t)) 
	explicit TCMalloc_PageMap1()
	{
		// sizeof(void*) = 4B => sizeof(void*) << BITS = 4B * (2^19) = 2MB的空间
		size_t bytes_size = sizeof(void*) << BITS;

		//向系统申请页表的空间，需要将字节数转换成页数才能调用 SystemAlloc 函数
		//可以通过实现的函数 _roundup ，将 2MB 按页大小(8KB) 向上对齐到 相应的页数 的字节数
		size_t alignBytes = SizeClass::_Roundup(bytes_size, 1 << PAGE_SHIFT);

		//array_ = reinterpret_cast<void**>((*allocator)(sizeof(void*) << BITS));

		array_ = SystemAlloc(alignBytes >> PAGE_SHIFT);

		memset(array_, 0, sizeof(void*) << BITS);
	}
	// Return the current value for KEY. Returns NULL if not yet set,
	// or if k is out of range.
	void* get(Number k) const 
	{
		if ((k >> BITS) > 0) 
		{
			return NULL;
		}
		return array_[k];
	}
	// REQUIRES "k" is in range "[0,2^BITS-1]".
	// REQUIRES "k" has been ensured before.
	//
	// Sets the value 'v' for key 'k'.
	void set(Number k, void* v) 
	{
		array_[k] = v;
	}
};


// Two-level radix tree  --- 32位下可用
// 类似于二级页表的实现
template <int BITS>//BITS --- 19位
class TCMalloc_PageMap2 
{
	//原理就是先通过一级页号作为一级页表的下标，然后查找一级页表找到指向二级页表的指针
	//再根据二级页号作为二级页表的下标，查找二级页表，找到最后由完整页号映射的Span*指针
	
	//对比一层基数树，好处就是可以动态开暂时用到的页号映射需要的空间，不会一下子开全部页号映射需要的空间，
	//一定程度上缓解了空间上的浪费
private:
	// Put 32 entries in the root and 2^(BITS - 32) entries in each leaf.
	// 地址 = 19位(页号) + 13位(页内偏移量) = 5位(一级页号) + 14位(二级页号) + 13位(页内偏移量)
	// ROOT_BITS --- 一级页号: 5位
	static const int ROOT_BITS = 5;
	//一级页表(页目录表), 有2^5个表项，用数组实现
	static const int ROOT_LENGTH = 1 << ROOT_BITS;
	
	//LEAF_BITS --- 二级页号:14位
	static const int LEAF_BITS = BITS - ROOT_BITS;
	//二级页表(页表), 有2^14个表项，用数组实现
	static const int LEAF_LENGTH = 1 << LEAF_BITS;

	// Leaf node --- 二级页表
	struct Leaf 
	{
	    void* values[LEAF_LENGTH];
	};

	//一级哈希表，存放指向二级页表的指针 Leaf*, 直接将一级哈希表静态开辟好
	Leaf* root_[ROOT_LENGTH]; // Pointers to 32 child nodes


	void* (*allocator_)(size_t); // Memory allocator


public:
	typedef uintptr_t Number;
	
	//explicit TCMalloc_PageMap2(void* (*allocator)(size_t)) 
	explicit TCMalloc_PageMap2()
	{
		//allocator_ = allocator;
		
		memset(root_, 0, sizeof(root_));

		//调用预开辟空间函数，直接将所有可能用到的的二级哈希表开辟好， 2MB
		PreallocateMoreMemory();
	}
	
	//从哈希表中获取页号k 所映射的指针
	void* get(Number k) const 
	{
		const Number i1 = k >> LEAF_BITS;//取出页号的前5位访问一级页表

		const Number i2 = k & (LEAF_LENGTH - 1);//取出页号的后14位访问二级页表

		if ((k >> BITS) > 0 || root_[i1] == NULL) 
		{
			return NULL;
		}
		return root_[i1]->values[i2];
	}
	
	//将页号k映射的指针v插入到哈希表中
	void set(Number k, void* v) 
	{
		const Number i1 = k >> LEAF_BITS;//计算一级页号
		const Number i2 = k & (LEAF_LENGTH - 1);//计算二级页号

		assert(i1 < ROOT_LENGTH);

		root_[i1]->values[i2] = v;//插入哈希表
	}

	//将页号k在二级哈希中的映射删除
	void erase(Number k)
	{
		const Number i1 = k >> LEAF_BITS;//计算一级页号
		const Number i2 = k & (LEAF_LENGTH - 1);//计算二级页号

		assert(i1 < ROOT_LENGTH);

		root_[i1]->values[i2] = nullptr;
	}
	
	bool Ensure(Number start, size_t n) 
	{
		//将起始页号start 开始往后的 n个页 在基数树中建立映射
		for (Number key = start; key <= start + n - 1;) 
		{
			const Number i1 = key >> LEAF_BITS;
			
			// Check for overflow  ---- 检查一级页号是否合法
			if (i1 >= ROOT_LENGTH)
			{
				return false;
			}
				
			// 只有当前页号 key 的一级页号所在的一级页表内的二级页表指针为空时，才新申请开辟二级页表
			// Make 2nd level node if necessary --- 新开辟二级页表的空间
			if (root_[i1] == NULL) 
			{
				//apply for the memory of 2nd level node (Leaf)
				//Leaf* leaf = reinterpret_cast<Leaf*>((*allocator_)(sizeof(Leaf)));

				//使用静态对象池进行开辟Leaf节点的空间，静态的好处是放在静态区，地址空间中只会有一份
				static ObjectPool<Leaf>LeafPool;

				Leaf* leaf = LeafPool.New();

				//无法开辟
				if (leaf == NULL) return false;

				//初始化空间
				memset(leaf, 0, sizeof(*leaf));

				//build up the map between i1 level node and Leaf node
				root_[i1] = leaf;
			}

			// Advance key past whatever is covered by this leaf node
			//一级页号加上1
			key = ((key >> LEAF_BITS) + 1) << LEAF_BITS;
		}
		return true;
	}
	void PreallocateMoreMemory() 
	{
		// Allocate enough to keep track of all possible pages
		Ensure(0, 1 << BITS);
	}
};


// Three-level radix tree
// 三级页表 为 64位系统服务
// 64 = 51 + 13 = 18(一级页号) + 18(二级页号) + 15(三级页号) + 13
template <int BITS>
class TCMalloc_PageMap3 
{
private:
	// How many bits should we consume at each interior level
	static const int INTERIOR_BITS = (BITS + 2) / 3; // Round-up
	static const int INTERIOR_LENGTH = 1 << INTERIOR_BITS;
	// How many bits should we consume at leaf level
	static const int LEAF_BITS = BITS - 2 * INTERIOR_BITS;
	static const int LEAF_LENGTH = 1 << LEAF_BITS;
	// Interior node
	struct Node 
	{
		Node* ptrs[INTERIOR_LENGTH];
	};
	// Leaf node
	struct Leaf 
	{
		void* values[LEAF_LENGTH];
	};

	//一级页表指针
	Node* root_;// Root of radix tree


	void* (*allocator_)(size_t);// Memory allocator


	Node* NewNode() 
	{
		static ObjectPool<Node>NodePool;
		Node* result = NodePool.New();
		if (result != nullptr) 
		{
			memset(result, 0, sizeof(*result));
		}
		return result;
	}
public:
	typedef uintptr_t Number;
	//explicit TCMalloc_PageMap3(void* (*allocator)(size_t)) 
	explicit TCMalloc_PageMap3()
	{
		//allocator_ = allocator;
		root_ = NewNode();
	}
	void* get(Number k) const 
	{
		const Number i1 = k >> (LEAF_BITS + INTERIOR_BITS);
		const Number i2 = (k >> LEAF_BITS) & (INTERIOR_LENGTH - 1);
		const Number i3 = k & (LEAF_LENGTH - 1);
		if ((k >> BITS) > 0 ||root_->ptrs[i1] == nullptr || root_->ptrs[i1]->ptrs[i2] == nullptr)
		{
			return nullptr;
		}
		return reinterpret_cast<Leaf*>(root_->ptrs[i1]->ptrs[i2])->values[i3];
	}
	void set(Number k, void* v) 
	{
		assert(k >> BITS == 0);
		const Number i1 = k >> (LEAF_BITS + INTERIOR_BITS);
		const Number i2 = (k >> LEAF_BITS) & (INTERIOR_LENGTH - 1);
		const Number i3 = k & (LEAF_LENGTH - 1);
		reinterpret_cast<Leaf*>(root_->ptrs[i1]->ptrs[i2])->values[i3] = v;
	}

	void erase(Number k)
	{
		const Number i1 = k >> (LEAF_BITS + INTERIOR_BITS);
		const Number i2 = (k >> LEAF_BITS) & (INTERIOR_LENGTH - 1);
		const Number i3 = k & (LEAF_LENGTH - 1);
		if ((k >> BITS) > 0 || root_->ptrs[i1] == nullptr || root_->ptrs[i1]->ptrs[i2] == nullptr)
		{
			assert(false);
		}
		reinterpret_cast<Leaf*>(root_->ptrs[i1]->ptrs[i2])->values[i3] = nullptr;
	}

	bool Ensure(Number start, size_t n) 
	{
		for (Number key = start; key <= start + n - 1;) 
		{
			const Number i1 = key >> (LEAF_BITS + INTERIOR_BITS);
			const Number i2 = (key >> LEAF_BITS) & (INTERIOR_LENGTH - 1);
			// Check for overflow
			if (i1 >= INTERIOR_LENGTH || i2 >= INTERIOR_LENGTH)
				return false;
			// Make 2nd level node if necessary
			if (root_->ptrs[i1] == nullptr)
			{
				Node* n = NewNode();
				if (n == nullptr) return false;
				root_->ptrs[i1] = n;
			}
			// Make leaf node if necessary
			if (root_->ptrs[i1]->ptrs[i2] == nullptr)
			{
				static ObjectPool<Leaf>LeafPool;
				Leaf* leaf = LeafPool.New();
				if (leaf == nullptr) return false;
				memset(leaf, 0, sizeof(*leaf));
				root_->ptrs[i1]->ptrs[i2] = reinterpret_cast<Node*>(leaf);;
			}
			// Advance key past whatever is covered by this leaf node
			//二级页号加上1
			key = ((key >> LEAF_BITS) + 1) << LEAF_BITS;
		}
		return true;
	}
	void PreallocateMoreMemory() 
	{}
};