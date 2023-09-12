#pragma once

template <typename T>
class LockFreeQueue
{
public:
	LockFreeQueue()
	{
		SYSTEM_INFO sysInfo;
		GetSystemInfo(&sysInfo);
		LPVOID MaxUserMemoryPtr = sysInfo.lpMaximumApplicationAddress;
		if ((INT64)MaxUserMemoryPtr != 0x00007ffffffeffff)
		{
			int* p = nullptr;
			*p = 0;
		}

		nodeCount = 0;
		Node* pInitNode;
		NodePool.mAlloc(&pInitNode);
		pInitNode->next = NULL;
		front = (INT64)pInitNode;
		rear = front;

	}
	~LockFreeQueue()
	{
		Node* pNode = (Node*)((INT64)front & 0x0000FFFFFFFFFFFF);
		while(pNode != NULL)
		{
			Node* temp = pNode;
			pNode = pNode->next;
			NodePool.mFree(temp);
		}

	}

	struct Node
	{
		Node* next;
		T data;
	};

	bool Enqueue(T data)
	{
		Node* pNewNode;
		NodePool.mAlloc(&pNewNode);
		pNewNode->data = data;
		pNewNode->next = NULL;

		while (1)
		{
			INT64 tempRear = (INT64)rear;
			Node* pNode = (Node*)((INT64)tempRear & 0x0000FFFFFFFFFFFF);
			short stamp = (short)((tempRear & 0xFFFF000000000000) >> 48);
			stamp++;
			Node* pNext = pNode->next;

			if (pNext == NULL)
			{
				if (InterlockedCompareExchangePointer((PVOID*)&pNode->next, pNewNode, pNext) == pNext)
				{
					//if pNewNode의 next가 pNode인경우 중단
					INT64 newRear = (INT64)(pNewNode) | (INT64)(stamp) << 48;
					InterlockedCompareExchange64(&rear, newRear, tempRear);
					break;
				}
				else
				{
					pNext = pNode->next;
					if (pNext != NULL)
					{
						INT64 newRear = (INT64)(pNext) | (INT64)(stamp) << 48;
						InterlockedCompareExchange64(&rear, newRear, tempRear);
					}
				}
			}
			else
			{
				INT64 newRear = (INT64)(pNext) | (INT64)(stamp) << 48;
				InterlockedCompareExchange64(&rear, newRear, tempRear);
			}
		}
		InterlockedIncrement(&nodeCount);
		return true;
	}

	bool Dequeue(T* pData)
	{
		if (nodeCount <= 0)
		{
			return false;
		}

		volatile LONG ret = InterlockedDecrement(&nodeCount);
		
		while (1)
		{
			INT64 tempFront = (INT64)front;
			Node* pFront = (Node*)((INT64)tempFront & 0x0000FFFFFFFFFFFF);
			short frontStamp = (short)((tempFront & 0xFFFF000000000000) >> 48);
			frontStamp++;
			Node* pNext = pFront->next;
			if (pNext == NULL)
			{
				continue;

			}
			else
			{
				T data = pNext->data;
				volatile INT64 newFront = (INT64)pNext | (INT64)(frontStamp) << 48;
				if (InterlockedCompareExchange64(&front, newFront, tempFront) == tempFront)
				{
					*pData = data;
					volatile INT64 tempRear = rear;
					volatile Node* pRear = (Node*)((INT64)tempRear & 0x0000FFFFFFFFFFFF);
					INT64 check = -1;
					if (pRear->next == pNext)
					{
						check = 0;
						short rearStamp = (short)((tempRear & 0xFFFF000000000000) >> 48);
						rearStamp++;
						INT64 newRear = (INT64)(pNext) | (INT64)(rearStamp) << 48;
						InterlockedCompareExchange64(&rear, newRear, tempRear);
					}
					NodePool.mFree(pFront);
					break;
				}
			}
		}
		return true;

	}

	volatile INT64 front;//deQ 위치
	volatile INT64 rear;//enQ 위치
	volatile LONG nodeCount = 0;
	char space[64];
	CMemoryPool<Node> NodePool;
};