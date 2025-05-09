#include "ConcurrentAlloc.h"


//ntimes: 一轮申请和释放的次数， rounds: 申请和释放的轮数
//nworks: 线程数
void BenchmarkMalloc(size_t ntimes, size_t nworks, size_t rounds)
{
	std::vector<std::thread> vthread(nworks);

	//保证统计时间的原子性
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
	
	printf("%zu个线程并发执行%zu轮次，每轮次malloc %zu次: 花费：%zu ms\n",
		nworks, rounds, ntimes, (size_t)malloc_costtime);
	printf("%zu个线程并发执行%zu轮次，每轮次free %zu次: 花费：%zu ms\n",
		nworks, rounds, ntimes, (size_t)free_costtime);
	printf("%zu个线程并发malloc&free %zu次，总计花费：%zu ms\n",
		nworks, nworks * rounds * ntimes, (size_t)(malloc_costtime + free_costtime));
}

// 单轮次申请释放次数 线程数 轮次
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
	printf("%zu个线程并发执行 %zu轮次，每轮次concurrent alloc %zu次: 花费：%zu ms\n",
		nworks, rounds, ntimes, (size_t)malloc_costtime);
	printf("%zu个线程并发执行 %zu轮次，每轮次concurrent dealloc %zu次: 花费：%zu ms\n",
		nworks, rounds, ntimes, (size_t)free_costtime);
	printf("%zu个线程并发concurrent alloc&dealloc %zu次，总计花费：%zu ms\n",
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