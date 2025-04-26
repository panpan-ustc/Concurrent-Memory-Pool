#include "Common.h"
#include "ConcurrentAlloc.h"
#include "CentralCache.h"
#include <vector>

void Alloc1()
{
	for (size_t i = 0; i < 5; ++i)
	{
		void* ptr = ConcurrentAlloc(6);
	}
}

void Alloc2()
{
	for (size_t i = 0; i < 5; ++i)
	{
		void* ptr = ConcurrentAlloc(7);
	}
}

inline void TLSTest()
{
	//�����̱߳��������
	//����һ���߳�t1,����ִ��Alloc1���������ݣ����ݺ���ָ��(������)
	std::thread t1(Alloc1);

	//����һ���߳�t2,����ִ��Alloc1���������ݣ����ݺ���ָ��(������)
	std::thread t2(Alloc2);

	//���̵߳���join���������ȴ�t1�߳�ִ�����
	//join�������Ḻ������߳�ִ����ɺ�(����join�������߳�ִ��ǰ),�߳�ռ�õ�ջ�ռ���ֲ߳̾��洢��Դ�Ļ��գ������ڴ�й¶
	t1.join();

	//���̵߳���t2�̵߳�join�������ȴ�t2�߳�ִ�����
	t2.join();
}

inline void TestConcurrentAlloc1()
{

	//���赱ǰ��������b�����Ѿ������� 1 + 2 + ... + (b - 1) = b(b-1)/2
	//��ǰ�߳��ܹ���Thread Cache ����Ĵ����� [(b-1)(b-2)/2 + 1, b(b-1)/2]
	//����ÿ�� Thread Cache �黹b���ڴ���� �� Central Cache��
	//�����Ѿ�����k�Σ��򹲻���k*b��(k �� N); �� k*b �� b(b-1)/2 => k �� (b-1)/2
	//���Ҽ���� k + 1���ǲ��ᷢ���ģ���k�ι黹��ʣ�µ����������Է����� k+1 �ι黹
	//���� k + 1 > (b-1)/2�� ��k = [(b-1)/2]((b-1)/2����ȡ��)
	//��k = (b-1)/2 => b = 2*k + 1;
	//��bΪ����ʱ��һ������kʹ�� k = (b-1)/2, ��ʱһ����������ȫ����ʱ��Central Cache��span�еļ�������Ϊ0
	size_t begin = clock();
	void* p1 = ConcurrentAlloc(6);
	void* p2 = ConcurrentAlloc(8);
	void* p3 = ConcurrentAlloc(1);
	void* p4 = ConcurrentAlloc(7);
	void* p5 = ConcurrentAlloc(8);
	void* p6 = ConcurrentAlloc(5);
	void* p7 = ConcurrentAlloc(4);

	ConcurrentFree(p1);
	ConcurrentFree(p2);
	ConcurrentFree(p3);
	ConcurrentFree(p4);
	ConcurrentFree(p5);
	ConcurrentFree(p6);
	ConcurrentFree(p7);

	size_t end = clock();
	cout << (end - begin) << endl;
}

inline void TestConcurrentAlloc2()
{
	size_t begin = clock();
	void* p1 = malloc(6);
	void* p2 = malloc(8);
	void* p3 = malloc(1);
	void* p4 = malloc(7);
	void* p5 = malloc(8);
	void* p6 = malloc(5);
	void* p7 = malloc(4);

	free(p1);
	free(p2);
	free(p3);
	free(p4);
	free(p5);
	free(p6);
	free(p7);
	size_t end = clock();
	cout << (end - begin) << endl;
}

void Testmalloc()
{
	std::vector<void*>p;
	size_t begin1 = clock();
	for (size_t i = 1; i <= 10000; i++)
	{
		p.push_back(malloc(i));
		//cout << p[i - 1] << endl;
	}

	while (!p.empty())
	{
		void* p1 = p.back();
		size_t size = p.size() - 1;
		p.pop_back();
		free(p1);
	}
	size_t end1 = clock();
	cout << "malloc:  " << (end1 - begin1) << endl;
}


inline void MultiThreadAlloc1()
{
	std::vector<void*>ptr1;
	for (size_t i = 0; i < 7; i++)
	{
		void* ptr = ConcurrentAlloc(6);
		ptr1.push_back(ptr);
	}

	for (auto p : ptr1)
	{
		ConcurrentFree(p);
	}
}

inline void MultiThreadAlloc2()
{
	std::vector<void*>ptr2;
	for (size_t i = 0; i < 7; i++)
	{
		void* ptr = ConcurrentAlloc(7);
		ptr2.push_back(ptr);
	}

	for (auto p : ptr2)
	{
		ConcurrentFree(p);
	}
}

inline void TestMultiThread()
{
	std::thread t1(MultiThreadAlloc1);


	std::thread t2(MultiThreadAlloc2);

	t1.join();
	t2.join();
}

//����߳�������ڴ�����С > 256KB(32ҳ)������
/*
1.������ڴ�����С ��[32*8KB + 1, 128*8KB], ֱ���� Page Cache ����
2.������ڴ�����С > 128ҳ�� ֱ���������
*/
void BigAlloc()
{
	void* p1 = ConcurrentAlloc(257 * 1024);

	ConcurrentFree(p1);

	void* p2 = ConcurrentAlloc(129 * 8 * 1024);

	ConcurrentFree(p2);

}


int main()
{
	TestConcurrentAlloc1();

	TestConcurrentAlloc2();

	TestMultiThread();

	BigAlloc();
	return 0;
}