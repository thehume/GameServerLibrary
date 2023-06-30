#pragma once

template <typename T>
class LockFreeStack
{
public:
	LockFreeStack()
	{
		SYSTEM_INFO sysInfo;
		GetSystemInfo(&sysInfo);
		LPVOID MaxUserMemoryPtr = sysInfo.lpMaximumApplicationAddress;
		if ((INT64)MaxUserMemoryPtr != 0x00007ffffffeffff)
		{
			int* p = nullptr;
			*p = 0;
		}
	}
	~LockFreeStack()
	{
		Node* pTop = (Node*)((INT64)top & 0x0000FFFFFFFFFFFF);
		while (pTop != NULL)
		{
			Node* temp = pTop;
			pTop = pTop->next;
			NodePool.mFree(temp);
		}
	}

	struct Node
	{
		Node* next;
		T data;
	};
	
	void push(T data)
	{		
		Node* pNewNode;
		NodePool.mAlloc(&pNewNode);
		pNewNode->data = data;

		volatile INT64 tempTop;
		do
		{
			tempTop = top;
			pNewNode->next = (Node*)(tempTop & 0x0000FFFFFFFFFFFF);
		}
		while(tempTop != InterlockedCompareExchange64(&top, (INT64)pNewNode | (tempTop & (0xFFFF000000000000)), tempTop));
		InterlockedIncrement(&nodeCount);
		
	}
	BOOL pop(T* pData)
	{

		if (nodeCount <= 0)
		{
			return FALSE;
		}
		InterlockedDecrement(&nodeCount);
		Node* pTop;
		volatile INT64 tempTop;
		volatile INT64 newTop;
		volatile short stamp;
		do
		{
			tempTop = top;
			pTop = (Node*)((INT64)tempTop & 0x0000FFFFFFFFFFFF);
			if (pTop == NULL)
			{
				InterlockedIncrement(&nodeCount);
				return FALSE;
			}
			stamp = (short)((tempTop & 0xFFFF000000000000) >> 48);
			stamp++;
			newTop = (INT64)(pTop->next) | (INT64)(stamp) << 48;
		} while (tempTop != InterlockedCompareExchange64(&top, newTop, tempTop));
		

		*pData = pTop->data;
		NodePool.mFree(pTop);
		return TRUE;
	}

	volatile INT64 top = NULL;
	volatile LONG nodeCount = 0;
	char space[64];
	CMemoryPoolBucket<Node> NodePool;
};