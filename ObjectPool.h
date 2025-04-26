#pragma once
#include "Common.h"

//�жϳ������л���
#ifdef _WIN32
#include <Windows.h>
#else 
	//linux��ͷ�ļ�
#endif

//�����ڴ��---��һ�����
#define K 1024
const size_t PreapplyBytes = 8 * 1024 * K;//Ԥ�����ڴ��С
const size_t PageSize = 8 * K;//ҳ��С

template<class T>
class ObjectPool
{
public:
	T* New()
	{
		T* obj = nullptr;
		//���Ȱѻ��������ڴ������ٴ��ظ�����---ͷɾ��
		if (_freelist)
		{
			//��void**��������ã��õ�����һ��void*��С���ڴ棬��void*���͵Ķ���
			void* next = *(void**)_freelist;
			obj = (T*)_freelist;
			_freelist = next;
		}
		else
		{
			//���Ԥ�����ʣ���ڴ治���Է����һ�����������
		    //���ó�������ʱ����ֱ�ӽ���һ�������ʣ������һ���ڴ涪������������
			if (_remainBytes < sizeof(T))
			{
				//_memory = (char*)malloc(PreapplyBytes);
				_memory = (char*)SystemAlloc(PreapplyBytes >> 13);//�ȼ��ڳ���2^(13)�����ҳ��
				_remainBytes = PreapplyBytes;

				//����ʧ�ܣ����쳣
				if (_memory == nullptr)
				{
					throw std::bad_alloc();
				}
			}

			obj = (T*)_memory;

			//��������ڴ�Ķ����С����4��8�ֽڵ�����
			//����ᵼ���ڶ�Ӧϵͳ�¹黹�ڴ�ʱ���ڴ��ͷ��û���㹻�Ĵ�С�洢nextָ��
			//�����������������ڴ�����ڴ�ռ�Ķ����С��ֱ��ǿ�з��������Ӧһ��ָ���С�Ŀռ�
			size_t objsize = sizeof(T);
			if (sizeof(T) < sizeof(void*))
			{
				 objsize = sizeof(void*);
			}
			_memory += objsize;
			_remainBytes -= objsize;
		}

		//��λnew����ʾ����T�Ĺ��캯����ʼ������
		new(obj)T;

		return obj;
	}

	void Delete(T* obj)
	{
		//��ʾ�������������������
		obj->~T();

		//�������߼��жϿ��Բ���ͬһ�����ʵ��,����Ҫ���������ʼ�����
		//ͷ��
		*(void**)obj = _freelist;
		_freelist = obj;


		//ɾ��һ�����󣬹���黹���ڴ��˼·����ʹ��һ����������
		//���黹���ڴ��ǰ4��8(����ϵͳ����)�ֽڱ�����������һ���ڴ��ָ�룬�黹ʱʹ��ͷ�巨���뵽������
		//���ָ���С��ȷ�������⣬���Բ���ǿ������ת���ɶ���ָ���ٽ����õķ�ʽ���
		/*if (_freelist == nullptr)
		{
			_freelist = obj;
			//��objǿתΪָ��int��С�ڴ�(����)��ָ��
			//*(int*)obj = nullptr;//ȡ����ΪT�Ķ���obj�ڴ��е�ǰint��С��ֵΪ0�������ָ��

			//��objǿתΪָ��void*��С�ڴ�(����/����)��ָ�룬void*��С��32λϵͳ����4B��64Ϊϵͳ����8B
			//�����ǿתΪʲô���Ͳ�����Ҫ��ֻҪ��ǿתΪһ������ָ�붼�Ǻ����
			//��Ϊһ��ָ���С����32λϵͳ����4B��64Ϊϵͳ����8B
			*(void**)obj = nullptr;
		}

		else
		{
			//ͷ��
			*(void**)obj = _freelist;
			_freelist = obj;
		}*/
	}



private:
	//����������Ķ����ڴ��
	char* _memory = nullptr;//ָ���ڴ�ص�ָ��
	size_t _remainBytes = 0;//�ڴ�����зֹ�����ʣ���ֽ����������ж��ڴ���Ƿ���

private:
	//����黹�������ڴ����ÿ�������СΪsizeof(T)
	void* _freelist = nullptr;
	
};

//struct TreeNode
//{
//	int _val;
//	TreeNode* _left, * _right;
//
//	TreeNode()
//		:_val(0), _left(nullptr), _right(nullptr)
//	{}
//
//	~TreeNode()
//	{
//		_val = 0;
//		_left = _right = nullptr;
//	}
//};
//
//inline void TestObjectPool()
//{
//	//�����ͷŵ��ִ�
//	const size_t Rounds = 1000000;
//
//	//ÿ�������ͷŶ��ٴ�
//	const int N = 100000;
//
//	
//	std::vector<TreeNode*> v1;
//	v1.reserve(N);
//
//	size_t begin1 = clock();
//	for (size_t j = 0; j < Rounds; ++j)
//	{
//		for (int i = 0; i < N; i++)
//		{
//			v1.push_back(new TreeNode);
//		}
//
//		for (int i = 0; i < N; i++)
//		{
//			delete v1[i];
//		}
//		v1.clear();
//	}
//
//	size_t end1 = clock();
//	
//
//	std::vector<TreeNode*>v2;
//	v2.reserve(N);
//	ObjectPool<TreeNode>TNPool;
//
//	size_t begin2 = clock();
//	for (size_t j = 0; j < Rounds; j++)
//	{
//		for (int i = 0; i < N; i++)
//		{
//			v2.push_back(TNPool.New());
//		}
//
//		for (int i = 0; i < N; i++)
//		{
//			TNPool.Delete(v2[i]);
//		}
//	}
//	size_t end2 = clock();
//
//	cout << (end1 - begin1) << endl;
//	cout << (end2 - begin2) << endl;
//}