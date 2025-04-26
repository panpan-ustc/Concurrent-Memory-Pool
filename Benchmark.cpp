#include "ConcurrentAlloc.h"


//ntimes: һ��������ͷŵĴ����� rounds: ������ͷŵ�����
//nworks: �߳���
void BenchmarkMalloc(size_t ntimes, size_t nworks, size_t rounds)
{
	std::vector<std::thread> vthread(nworks);

	//��֤ͳ��ʱ���ԭ����
	std::atomic<size_t> malloc_costtime = 0;
	std::atomic<size_t> free_costtime = 0;


	for (size_t k = 0; k < nworks; ++k)
	{
		vthread[k] = std::thread([&, k]() {
			std::vector<void*> v;
			v.reserve(ntimes);
			for (size_t j = 0; j < rounds; ++j)
			{
				size_t begin1 = clock();
				for (size_t i = 0; i < ntimes; i++)
				{
					//v.push_back(malloc(16));
					v.push_back(malloc((16 + i) % 8192 + 1));
				}
				size_t end1 = clock();
				size_t begin2 = clock();
				for (size_t i = 0; i < ntimes; i++)
				{
					free(v[i]);
				}
				size_t end2 = clock();
				v.clear();
				malloc_costtime += (end1 - begin1);
				free_costtime += (end2 - begin2);
			}
			});
	}
	for (auto& t : vthread)
	{
		t.join();
	}
	
	printf("%zu���̲߳���ִ��%zu�ִΣ�ÿ�ִ�malloc %zu��: ���ѣ�%zu ms\n",
		nworks, rounds, ntimes, (size_t)malloc_costtime);
	printf("%zu���̲߳���ִ��%zu�ִΣ�ÿ�ִ�free %zu��: ���ѣ�%zu ms\n",
		nworks, rounds, ntimes, (size_t)free_costtime);
	printf("%zu���̲߳���malloc&free %zu�Σ��ܼƻ��ѣ�%zu ms\n",
		nworks, nworks * rounds * ntimes, (size_t)(malloc_costtime + free_costtime));
}

// ���ִ������ͷŴ��� �߳��� �ִ�
void BenchmarkConcurrentMalloc(size_t ntimes, size_t nworks, size_t rounds)
{
	std::vector<std::thread> vthread(nworks);
	std::atomic<size_t> malloc_costtime = 0;
	std::atomic<size_t> free_costtime = 0;
	for (size_t k = 0; k < nworks; ++k)
	{
		vthread[k] = std::thread([&]() {
			std::vector<void*> v;
			v.reserve(ntimes);
			for (size_t j = 0; j < rounds; ++j)
			{
				size_t begin1 = clock();
				for (size_t i = 0; i < ntimes; i++)
				{
					//v.push_back(ConcurrentAlloc(16));
					v.push_back(ConcurrentAlloc((16 + i) % 8192 + 1));
				}
				size_t end1 = clock();
				size_t begin2 = clock();
				for (size_t i = 0; i < ntimes; i++)
				{
					ConcurrentFree(v[i]);
				}
				size_t end2 = clock();
				v.clear();
				malloc_costtime += (end1 - begin1);
				free_costtime += (end2 - begin2);
			}
			});
	}
	for (auto& t : vthread)
	{
		t.join();
	}
	printf("%zu���̲߳���ִ�� %zu�ִΣ�ÿ�ִ�concurrent alloc %zu��: ���ѣ�%zu ms\n",
		nworks, rounds, ntimes, (size_t)malloc_costtime);
	printf("%zu���̲߳���ִ�� %zu�ִΣ�ÿ�ִ�concurrent dealloc %zu��: ���ѣ�%zu ms\n",
		nworks, rounds, ntimes, (size_t)free_costtime);
	printf("%zu���̲߳���concurrent alloc&dealloc %zu�Σ��ܼƻ��ѣ�%zu ms\n",
		nworks, nworks * rounds * ntimes, (size_t)(malloc_costtime + free_costtime));
}
int main()
{
	size_t n = 10000;
	
	cout << "===========================================================================" << endl;
	BenchmarkConcurrentMalloc(n, 50, 5000);
	cout << endl << endl;
	cout << "===========================================================================" << endl;

	
	BenchmarkMalloc(n, 50, 5000);
	cout << "===========================================================================" << endl;
	return 0;
}