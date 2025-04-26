#pragma once
#include "Common.h"
#include "ObjectPool.h"

//tcmalloc�еĻ�����ʵ��Դ�� --- ��� _PageidSpanMap ���������Լ�������ʱ���˷� ������Ч������

// Single-level array  ---(32λ�¿���)ֱ�Ӷ�ַ����һ����ϣ��(����), key = ҳ�ţ� value = ��ַ(ָ��)
// BITS = 32(64) - PAGE_SHIFT => ҳ�ŵ�λ���� ������ԭ��ҳ���ʵ��
template <int BITS>
class TCMalloc_PageMap1 
{
private:

	static const int LENGTH = 1 << BITS;//����ĳ����� (2^BITS),��Ӧ (2^BITSҳ)
	void** array_;//�����ÿ��Ԫ����һ��ָ��(��ַ)void*, ����������Ԫ�صĵ�ַ --- ָ��void*��һ��ָ��void**

	//array_[i]:���i��ҳ�����ڵ�Span��ָ��Span*��ռ���ڴ� 2^19 * 4B = 2MB
	

public:
	typedef uintptr_t Number;
	//tcmalloc�п��ٿռ���Ҫ������ռ�ĺ���ָ�� (malloc), ������Ҫ�ܿ�malloc��ʹ�ã�������ֱ�Ӳ�ʹ��
	//explicit TCMalloc_PageMap1(void* (*allocator)(size_t)) 
	explicit TCMalloc_PageMap1()
	{
		// sizeof(void*) = 4B => sizeof(void*) << BITS = 4B * (2^19) = 2MB�Ŀռ�
		size_t bytes_size = sizeof(void*) << BITS;

		//��ϵͳ����ҳ��Ŀռ䣬��Ҫ���ֽ���ת����ҳ�����ܵ��� SystemAlloc ����
		//����ͨ��ʵ�ֵĺ��� _roundup ���� 2MB ��ҳ��С(8KB) ���϶��뵽 ��Ӧ��ҳ�� ���ֽ���
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


// Two-level radix tree  --- 32λ�¿���
// �����ڶ���ҳ���ʵ��
template <int BITS>//BITS --- 19λ
class TCMalloc_PageMap2 
{
	//ԭ�������ͨ��һ��ҳ����Ϊһ��ҳ����±꣬Ȼ�����һ��ҳ���ҵ�ָ�����ҳ���ָ��
	//�ٸ��ݶ���ҳ����Ϊ����ҳ����±꣬���Ҷ���ҳ���ҵ����������ҳ��ӳ���Span*ָ��
	
	//�Ա�һ����������ô����ǿ��Զ�̬����ʱ�õ���ҳ��ӳ����Ҫ�Ŀռ䣬����һ���ӿ�ȫ��ҳ��ӳ����Ҫ�Ŀռ䣬
	//һ���̶��ϻ����˿ռ��ϵ��˷�
private:
	// Put 32 entries in the root and 2^(BITS - 32) entries in each leaf.
	// ��ַ = 19λ(ҳ��) + 13λ(ҳ��ƫ����) = 5λ(һ��ҳ��) + 14λ(����ҳ��) + 13λ(ҳ��ƫ����)
	// ROOT_BITS --- һ��ҳ��: 5λ
	static const int ROOT_BITS = 5;
	//һ��ҳ��(ҳĿ¼��), ��2^5�����������ʵ��
	static const int ROOT_LENGTH = 1 << ROOT_BITS;
	
	//LEAF_BITS --- ����ҳ��:14λ
	static const int LEAF_BITS = BITS - ROOT_BITS;
	//����ҳ��(ҳ��), ��2^14�����������ʵ��
	static const int LEAF_LENGTH = 1 << LEAF_BITS;

	// Leaf node --- ����ҳ��
	struct Leaf 
	{
	    void* values[LEAF_LENGTH];
	};

	//һ����ϣ�����ָ�����ҳ���ָ�� Leaf*, ֱ�ӽ�һ����ϣ��̬���ٺ�
	Leaf* root_[ROOT_LENGTH]; // Pointers to 32 child nodes


	void* (*allocator_)(size_t); // Memory allocator


public:
	typedef uintptr_t Number;
	
	//explicit TCMalloc_PageMap2(void* (*allocator)(size_t)) 
	explicit TCMalloc_PageMap2()
	{
		//allocator_ = allocator;
		
		memset(root_, 0, sizeof(root_));

		//����Ԥ���ٿռ亯����ֱ�ӽ����п����õ��ĵĶ�����ϣ���ٺã� 2MB
		PreallocateMoreMemory();
	}
	
	//�ӹ�ϣ���л�ȡҳ��k ��ӳ���ָ��
	void* get(Number k) const 
	{
		const Number i1 = k >> LEAF_BITS;//ȡ��ҳ�ŵ�ǰ5λ����һ��ҳ��

		const Number i2 = k & (LEAF_LENGTH - 1);//ȡ��ҳ�ŵĺ�14λ���ʶ���ҳ��

		if ((k >> BITS) > 0 || root_[i1] == NULL) 
		{
			return NULL;
		}
		return root_[i1]->values[i2];
	}
	
	//��ҳ��kӳ���ָ��v���뵽��ϣ����
	void set(Number k, void* v) 
	{
		const Number i1 = k >> LEAF_BITS;//����һ��ҳ��
		const Number i2 = k & (LEAF_LENGTH - 1);//�������ҳ��

		assert(i1 < ROOT_LENGTH);

		root_[i1]->values[i2] = v;//�����ϣ��
	}

	//��ҳ��k�ڶ�����ϣ�е�ӳ��ɾ��
	void erase(Number k)
	{
		const Number i1 = k >> LEAF_BITS;//����һ��ҳ��
		const Number i2 = k & (LEAF_LENGTH - 1);//�������ҳ��

		assert(i1 < ROOT_LENGTH);

		root_[i1]->values[i2] = nullptr;
	}
	
	bool Ensure(Number start, size_t n) 
	{
		//����ʼҳ��start ��ʼ����� n��ҳ �ڻ������н���ӳ��
		for (Number key = start; key <= start + n - 1;) 
		{
			const Number i1 = key >> LEAF_BITS;
			
			// Check for overflow  ---- ���һ��ҳ���Ƿ�Ϸ�
			if (i1 >= ROOT_LENGTH)
			{
				return false;
			}
				
			// ֻ�е�ǰҳ�� key ��һ��ҳ�����ڵ�һ��ҳ���ڵĶ���ҳ��ָ��Ϊ��ʱ���������뿪�ٶ���ҳ��
			// Make 2nd level node if necessary --- �¿��ٶ���ҳ��Ŀռ�
			if (root_[i1] == NULL) 
			{
				//apply for the memory of 2nd level node (Leaf)
				//Leaf* leaf = reinterpret_cast<Leaf*>((*allocator_)(sizeof(Leaf)));

				//ʹ�þ�̬����ؽ��п���Leaf�ڵ�Ŀռ䣬��̬�ĺô��Ƿ��ھ�̬������ַ�ռ���ֻ����һ��
				static ObjectPool<Leaf>LeafPool;

				Leaf* leaf = LeafPool.New();

				//�޷�����
				if (leaf == NULL) return false;

				//��ʼ���ռ�
				memset(leaf, 0, sizeof(*leaf));

				//build up the map between i1 level node and Leaf node
				root_[i1] = leaf;
			}

			// Advance key past whatever is covered by this leaf node
			//һ��ҳ�ż���1
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
// ����ҳ�� Ϊ 64λϵͳ����
// 64 = 51 + 13 = 18(һ��ҳ��) + 18(����ҳ��) + 15(����ҳ��) + 13
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

	//һ��ҳ��ָ��
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
			//����ҳ�ż���1
			key = ((key >> LEAF_BITS) + 1) << LEAF_BITS;
		}
		return true;
	}
	void PreallocateMoreMemory() 
	{}
};