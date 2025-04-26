#pragma once
#include "Common.h"
#include "ObjectPool.h"

class ThreadCache
{
public:
	//������ͷ��ڴ����
	void* Allocate(size_t size);


	//�ͷ�ʱ��Ҫsize����ȷ���黹�ڴ����ڵĹ�ϣͰ
	//ÿ����ϣͰ��Ӧһ����������
	void Deallocate(void* ptr, size_t size);

	//�����Ļ����ȡ�ڴ����(index --��Ҫ���ڴ�������ڹ�ϣͰ�ı�ţ�size --����������ڴ����Ĵ�С)
	void* FetchFromCentralCache(size_t index, size_t size);

	// �ͷŶ���ʱ���������ʱ�������ڴ�ص����Ļ���,size��list���ڴ����Ĵ�С
	void ListTooLong(Freelist& list, size_t size);



private:
	//ÿ������������һ������ʵ�֣�������װ��ÿ�����������ͷָ���Լ���صĲ��롢�������пյȲ���
	Freelist _freelists[NFREELISTS];
};

//TLS - thread local storage
/*�ֲ߳̾��洢��TLS������һ�ֱ����Ĵ洢��������������������ڵ��߳�����ȫ�ֿɷ��ʵģ�
���ǲ��ܱ������̷߳��ʵ��������ͱ��������ݵ��̶߳����ԡ�����֪�Ľ���ȫ�ֱ�����
�������̶߳����Է��ʵģ������Ͳ��ɱ�����Ҫ�������ƣ������˿��Ƴɱ��ʹ��븴�Ӷȡ�*/

/*ʹ��TLS���Ա��������߳�ͬʱ��Ҫ�����Լ���ThreadCacheʱ�������Ķ�ThreadCache����ͬʱ���ʴ�����һЩ����
�����������⣬��ΪThreadCache�������ڽ��̣������̵����ݻ��ߴ����Ǹ����̹߳����*/

//windows��TLS ��ʵ��
//ÿ���̶߳����Լ���ThreadCache����; pTLSTheadCache��ָ���Լ���ThreadCache�����ָ��
//��ʼΪ�գ��̴߳����󶼻����Լ���һ��pTLSThreadCacheָ�룬����󴴽��ö���

//static��֤���ε�ȫ�ֱ���ֻ�ڵ�ǰ�ļ��ɼ�,���Ա�֤ͷ�ļ��ڶ����ȫ�ֱ������ߺ����ڲ�ͬ��cpp�ļ���չ����
//������ʱ������ֱ������ߺ����ض��������
static _declspec(thread) ThreadCache* pTLSThreadCache = nullptr;