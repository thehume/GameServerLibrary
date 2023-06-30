#pragma once

#define POOL_BUCKET_SIZE 1000
#define ERROR_HANDLE()

//#define ERROR_CHECK_FLAG


template <typename T>
class DataBlock
{
public:
	void* bucketAddress;
	T Data;
};

template <typename T>
class DataBucket
{
public:
	DataBlock<T> DataArray[POOL_BUCKET_SIZE];
	volatile LONG allocCount; //alloc 인덱스 순번, 0번부터 시작 최대 BUCKET_SIZE -1
	char space[64];
	volatile LONG useCount; //BUCKET_SIZE부터 시작, 0이 될시 사용완료
};


template <typename T>
class CMemoryPool
{
public:
	struct Block
	{
		Block* upperBound;
		volatile long long isUse;
		CMemoryPool<T>* check;
		T Data;
		Block* next;
		Block* lowerBound;
	};

	CMemoryPool(int BlockNum = 0, bool alloc = true)
	{
		//printf("메모리풀생성자 bool : %d, alloc : %d\n", BlockNum, alloc);
		_alloc = alloc;
		_freeNodePtr = NULL;
		_UseSize = 0;
		_capacity = 0;
		if (BlockNum > 0)
		{
			for (int i = 0; i < BlockNum; i++)
			{
				Block* newNode;
				if (alloc)
				{
					newNode = new Block;
				}
				else
				{
					newNode = (Block*)malloc(sizeof(Block));
				}
				newNode->next = (Block*)_freeNodePtr;
#ifdef ERROR_CHECK_FLAG
				newNode->isUse = 0;
				newNode->check = this;
				newNode->upperBound = newNode;
				newNode->lowerBound = newNode;
#endif
				_freeNodePtr = (INT64)newNode;
				_capacity++;
			}
		}
	}

	~CMemoryPool()
	{
		Block* pBlockTop = (Block*)((INT64)_freeNodePtr & 0x0000FFFFFFFFFFFF);
		if (_alloc)
		{
			while (pBlockTop != NULL)
			{
				Block* temp = pBlockTop;
				pBlockTop = pBlockTop->next;
				delete temp;
			}
		}
		else
		{
			while (pBlockTop != NULL)
			{
				Block* temp = pBlockTop;
				pBlockTop = pBlockTop->next;
				free(temp);
			}
		}
	}

	bool mAlloc(T** outParam)
	{
		Block* pBlockTop;
		INT64 tempFreeNode;
		INT64 newFreeNode;
		short stamp;
		do
		{
			tempFreeNode = _freeNodePtr;
			pBlockTop = (Block*)((INT64)tempFreeNode & 0x0000FFFFFFFFFFFF);
			if (pBlockTop == NULL)
			{
				Block* pBlock;
				if (_alloc)
				{
					pBlock = new Block;
#ifdef ERROR_CHECK_FLAG
					pBlock->upperBound = pBlock;
					pBlock->isUse = 1;
					pBlock->check = this;
					pBlock->lowerBound = pBlock;
#endif
					InterlockedIncrement(&_capacity);
					InterlockedIncrement(&_UseSize);
					*outParam = (T*)((char*)pBlock + 24);
				}
				else
				{
					pBlock = (Block*)malloc(sizeof(Block));
#ifdef ERROR_CHECK_FLAG
					pBlock->upperBound = pBlock;
					pBlock->isUse = 1;
					pBlock->check = this;
					pBlock->lowerBound = pBlock;
#endif
					InterlockedIncrement(&_capacity);
					InterlockedIncrement(&_UseSize);
					*outParam = new ((char*)pBlock + 24) T;
				}
				return true;
			}
			stamp = (short)((tempFreeNode & 0xFFFF000000000000) >> 48);
			stamp++;
			newFreeNode = (INT64)(pBlockTop->next) | (INT64)(stamp) << 48;
		} while (tempFreeNode != InterlockedCompareExchange64(&_freeNodePtr, newFreeNode, tempFreeNode));
#ifdef ERROR_CHECK_FLAG
		if (pBlockTop->isUse != 0)
		{
			ERROR_HANDLE();
		}
#endif
		pBlockTop->isUse = 1;
		if (_alloc)
		{

			*outParam = (T*)((char*)pBlockTop + 24);
		}
		else
		{
			*outParam = new ((char*)pBlockTop + 24) T;
		}
		InterlockedIncrement(&_UseSize);
		return true;
	}

	bool mFree(T* inParam)
	{
		//bound값이 변동되었을경우 로그찍기.
		//check 값이 this가 아닐경우 로그찍기.
		//isUse가 1이 아닐경우 로그찍기.
		Block* pBlock = (Block*)((char*)inParam - 24);

#ifdef ERROR_CHECK_FLAG
		if (pBlock->upperBound != pBlock->lowerBound)
		{
			ERROR_HANDLE();
			return false;
		}


		if (pBlock->check != this)
		{
			ERROR_HANDLE();
			return false;
		}

		if (pBlock->isUse != 1)
		{
			ERROR_HANDLE();
			return false;
		}
#endif
		if (!_alloc)
		{
			inParam->~T();
		}
#ifdef ERROR_CHECK_FLAG
		pBlock->isUse = 0;
#endif
		INT64 tempFreeNode;
		do
		{
			tempFreeNode = _freeNodePtr;
			pBlock->next = (Block*)(tempFreeNode & 0x0000FFFFFFFFFFFF);
		} while (tempFreeNode != InterlockedCompareExchange64(&_freeNodePtr, (INT64)pBlock | (tempFreeNode & (0xFFFF000000000000)), tempFreeNode));
		InterlockedDecrement(&_UseSize);
		return true;
	}

	LONG getUseSize()
	{
		return _UseSize;
	}

	volatile INT64 _freeNodePtr;
	volatile LONG _UseSize;
	volatile LONG _capacity;
	volatile bool _alloc;
};

template <typename T>
class CMemoryPoolBucket
{
public:

	CMemoryPoolBucket(int BlockNum = 0, bool alloc = true) : shared_Pool(CMemoryPool<DataBucket<T>>(BlockNum, alloc)) 
	{
		TLS_MemoryPoolIndex = TlsAlloc();
		//printf("버킷풀생성자\n");
	};

	//소멸자

	bool mAlloc(T** outParam)
	{
		DataBucket<T>* pBucket = (DataBucket<T>*)TlsGetValue(TLS_MemoryPoolIndex);
		if (pBucket == NULL)
		{
			borrowBucket();
			pBucket = (DataBucket<T>*)TlsGetValue(TLS_MemoryPoolIndex);
		}
		LONG temp = pBucket->allocCount;
		pBucket->allocCount++;
		if (pBucket->allocCount == POOL_BUCKET_SIZE)
		{
			TlsSetValue(TLS_MemoryPoolIndex, NULL);
		}
		*outParam = &pBucket->DataArray[temp].Data;
		return true;
	}

	bool mFree(T* inParam)
	{
		DataBucket<T>* pBucket = *(DataBucket<T>**)((char*)inParam - 8);
		LONG useCount = InterlockedDecrement(&pBucket->useCount);
		if(useCount ==0)
		{
			shared_Pool.mFree(pBucket);
		}
		return true;
	}

	LONG getUseSize()
	{
		return shared_Pool.getUseSize();
	}


private:
	void borrowBucket()
	{
		DataBucket<T>* pBucket;
		shared_Pool.mAlloc(&pBucket);
		for (int i = 0; i < POOL_BUCKET_SIZE; i++)
		{
			pBucket->DataArray[i].bucketAddress = (void*)pBucket;
		}
		pBucket->allocCount = 0;
		pBucket->useCount = POOL_BUCKET_SIZE;
		TlsSetValue(TLS_MemoryPoolIndex, pBucket);
	}


	volatile DWORD TLS_MemoryPoolIndex;
	char space[64];
	CMemoryPool<DataBucket<T>> shared_Pool;
};