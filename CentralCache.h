#pragma once
#include "Common.h"


//����central cacheռ�ݵ��ڴ��ܴ��һᱻƵ��ʹ�ã���ʹ�õ���ģʽ������������ֻ����central cache���һ��ʵ��
/*
����ģʽ��ȷ��һ����ֻ��һ��ʵ�������ṩһ��ȫ�ַ��ʵ�(ʹ��static���Σ���֤��ʵ���������ھ�̬��������
          �ᱻ����)���������ʵ����
ʵ�ַ�ʽ:
����ʽ(��̬ʽ): �������ʱ��������������ʵ����
����ʽ(��̬ʽ): �ڵ�һ��ʹ��ʱ�Ŵ���ʵ����ʵ���ӳټ���(���߳��������Ҫ�Դ���ʵ���ĺ������м���)

����һЩƵ��ʹ���Ҵ����ɱ��ϸ�(���ڴ����)�Ķ���ʹ�õ���ģʽ���Ա����ظ��������������ܿ�������ʡϵͳ��Դ
*/
class CentralCache
{
public:
	//����ģʽ����Ҫ����һ�����г�Ա��������ô����ĵ���ʵ������
	//���ڲ��õ��Ƕ���ģʽ���ʸ���ĵ����������ʱ���Ѿ�������������main����ִ��ǰ���Ѿ�����
	//�ʲ����ڶ��߳̾��������⣬����ֱ�ӷ��ؼ���
	//������ʽ�ǳ�������ʱ�Żᴴ������ʵ�����󣬹��ڶ��߳��»���ڴ�����εĿ���
	/*
	����ʽ:
	static CentralCache* GetInstance()
	{
		//���ܶ���߳�ͬʱ�ж�_sInstΪ��
	    if(_sInst == nullptr)
		{
			//˫�ؼ������
			std::lock_gurad<std::mutex>guard(_mutex);//����
			//��һ���ж�Ϊ�յ��̻߳�����������߳�����
			if (_sInst == nullptr)
			{
				_sInst = new Singleton();//������ĳ�Ա�����ǿ��Ե��ù��캯������ʵ������
			}
		}
	}
	*/
	static CentralCache* GetInstance()
	{
		return &_sInst;
	}


	//��ȡһ���ǿյ�Span
	Span* GetOneSpan(SpanList &list, size_t byte_size);


	//�����Ļ����ȡһ������������thread cache������ڴ�����thread cache
	//����batchNum��thread cache��Ҫ���ڴ����ĸ�����ÿ������Ĵ�С��byte_size��С
	//start�Ǻ���ͨ�����÷���thread cache�����Ļ�����Լ���Ͱȡ���������ڴ������ɵ������ͷָ��
	//end�Ǻ���ͨ�����÷���thread cache�����Ļ�����Լ���Ͱȡ���������ڴ������ɵ������βָ��
	//�����ķ���ֵ�����ʵ�ʷ����thread cache���ڴ����ĸ�������Ϊ�п������Ļ���Ĺ�ϣͰ������ʣ���ڴ���󲻹�thread cache�����n��
	size_t FetchRangeObj(void* &start, void* &end, size_t batchNum, size_t byte_size);

	//��Thread Cache �����������е�һ�������Ķ����ͷŵ�span���
	void ReleaseListToSpans(void* start, size_t byte_size);


private:
	//Central Cache�е�Ͱ����
	//CentralCache��ʼ���б����κ����ݣ����������Զ�����spanlist�ĳ�ʼ��������spanlist��Ķ�����г�ʼ��
	
	//��index��Ͱ��Span��С = һ�η�����̵߳�����ڴ���������� * ��ӦindexͰ�µ��ڴ�����С
	SpanList _spanlists[NFREELISTS];


private:
	//��Central Cache�Ĺ��캯�����ó�˽�г�Ա��������ֹ�ⲿʹ��Ĭ�Ϲ��캯��������CentralCache��Ķ���
	//��˱�֤�˵���ģʽ
	CentralCache()
	{}

	//ʹ�á�= delete���﷨����ȷ�ĸ�֪������������ʹ�ÿ������캯��������CentralCache��Ķ���
	//��ֹ�õ������ʹ�ÿ������캯�����д���CentralCache��Ķ���
	CentralCache(const CentralCache&) = delete;

	static CentralCache _sInst;//�����ĵ�һʵ�������޷����޸�
	
	//�����������̰߳�ȫ
	//static std::mutex _mutex;
};