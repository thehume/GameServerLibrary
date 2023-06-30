#include <Windows.h>
#include "ringbuffer.h"

//링버퍼가 가득찬경우  -1을 반환하도록

//size - 1만큼 사용가능
CRingBuffer::CRingBuffer(void)
{
    size = 80001;

    begin = (char*)VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (begin == NULL)
    {
        int* p = nullptr;
        *p = 0;
    }
    //begin = (char*)malloc(size);
    end = begin + size - 1;
    front = begin;
    rear = begin;
    usableSize = size - 1;
}

//size - 1 만큼 사용가능
CRingBuffer::CRingBuffer(int BufferSize)
{
    size = BufferSize;
    begin = (char*)VirtualAlloc(NULL, BufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (begin == NULL)
    {
        int* p = nullptr;
        *p = 0;
    }
    //begin = (char*)malloc(size);
    end = begin + size - 1;
    front = begin;
    rear = begin;
    usableSize = size - 1;
}

CRingBuffer::~CRingBuffer()
{
    int ret = VirtualFree(begin, 0, MEM_RELEASE);
    if (ret == 0)
    {
        int* p = nullptr;
        *p = 0;
    }
    //free(begin);
}


//그냥 총크기
int CRingBuffer::GetBufferSize(void)
{
    return usableSize;
}

//사용중인크기 (최대 : 버퍼크기 -1)
int CRingBuffer::GetUseSize(void)
{
    char* temp_front = front;
    char* temp_rear = rear;

    if (temp_front == temp_rear)
    {
        return 0;
    }

    if (temp_front == end)
    {
        temp_front = begin;
    }

    if (temp_front <= temp_rear)
    {
        return temp_rear - temp_front;
    }

    else
    {
        return usableSize - (temp_front - temp_rear);
    }
}

//쓸수있는크기 (최대 : 버퍼크기 -1)
int CRingBuffer::GetFreeSize(void)
{

    if (front == begin)
    {
        return usableSize - this->GetUseSize();
    }
    else
    {
        return usableSize - 1 - this->GetUseSize();
    }
}

int CRingBuffer::DirectEnqueueSize(void)
{
    char* temp_front = front;
    char* temp_rear = rear;

    if (temp_front <= temp_rear)
    {
        if (temp_rear == end)
        {
            return temp_front - begin - 1; // -1이 나올수도 있음
        }
        else
        {
            return end - temp_rear;
        }
    }

    else
    {
        return temp_front - temp_rear - 1;
    }
}

int CRingBuffer::DirectDequeueSize(void)
{
    char* temp_front = front;
    char* temp_rear = rear;

    if (temp_front == end && temp_rear == end)
    {
        return 0;
    }

    if (temp_front == end)
    {
        temp_front = begin;
    }

    if (temp_front <= temp_rear)
    {
        return temp_rear - temp_front;
    }

    else
    {
        return end - temp_front;
    }

}

int CRingBuffer::Enqueue(char* chpData, int Size)
{
    char* temp_front = front;
    char* temp_rear = rear;

    if (temp_front == end)
    {
        temp_front = begin;
    }

    if (temp_rear == end)
    {
        temp_rear = begin;
    }

    if (Size <= 0)
    {
        return 0;
    }
    if (this->GetFreeSize() < Size)
    {
        return -1;
    }

    else
    {
        if (temp_front <= temp_rear)
        {
            if (end - temp_rear >= Size)
            {
                memcpy(temp_rear + 1, chpData, Size);
                rear = temp_rear + Size;
                return Size;
            }
            else
            {
                int first_length = end - temp_rear;
                int second_length = Size - first_length;
                memcpy(temp_rear + 1, chpData, first_length);
                memcpy(begin+1, chpData + first_length, second_length);
                rear = begin + second_length;
                return Size;
            }
        }

        else
        {
            memcpy(temp_rear + 1, chpData, Size);
            rear = temp_rear + Size;
            return Size;
        }
    }
}

int CRingBuffer::Dequeue(char* chpDest, int dequeue_size)
{
    int Size = dequeue_size;
    if (Size <= 0)
    {
        return 0;
    }

    if (this->GetUseSize() < Size)
    {
        return -1;
    }

    char* temp_front = front;
    char* temp_rear = rear;

    if (temp_front == temp_rear)
    {
        return 0;
    }

    if (temp_front == end)
    {
        temp_front = begin;
    }
    //뽑을수 있다.

        if (temp_front <= temp_rear)
        {
            memcpy(chpDest, temp_front + 1, Size);
            front = temp_front + Size;
            return Size;
        }

        else
        {
            if (end - temp_front >= Size)
            {
                memcpy(chpDest, temp_front + 1, Size);
                front = temp_front + Size;
                return Size;
            }

            else
            {
                int first_length = end - temp_front;
                int second_length = Size - first_length;
                memcpy(chpDest, temp_front + 1, first_length);
                memcpy(chpDest + first_length, begin+1, second_length);
                front = begin + second_length;
                return Size;
            }
        }
}

int	CRingBuffer::Peek(char* chpDest, int peek_size)
{
    int Size = peek_size;
    if (this->GetUseSize() < Size)
    {
        return -1;
    }

    if (Size <= 0)
    {
        return 0;
    }
    //뽑을수 있다.

    char* temp_front = front;
    char* temp_rear = rear;


        if (temp_front <= temp_rear)
        {
            memcpy(chpDest, temp_front + 1, Size);
            return Size;
        }

        else
        {
            if (end - temp_front >= Size)
            {
                memcpy(chpDest, temp_front + 1, Size);
                return Size;
            }

            else
            {
                int first_length = end - temp_front;
                int second_length = Size - first_length;
                memcpy(chpDest, temp_front + 1, first_length);
                memcpy(chpDest + first_length, begin+1, second_length);
                return Size;
            }
        }

}

int CRingBuffer::MoveRear(int Size)
{
    char* temp_front = front;
    char* temp_rear = rear;

    if (Size <= 0)
    {
        return 0;
    }

    else
    {
        if (temp_rear == end)
        {
            temp_rear = begin;
        }

        if (end - temp_rear >= Size)
        {
            rear = temp_rear + Size;
            return Size;
        }

        else
        {
            int first_length = end - temp_rear;
            int second_length = Size - first_length;
            rear = begin + second_length;
            return Size;
        }
    }
}

int CRingBuffer::MoveFront(int Size)
{
    char* temp_front = front;
    char* temp_rear = rear;

    if (temp_front == end)
    {
        temp_front = begin;
    }
    if (Size <= 0)
    {
        return 0;
    }

    if (Size > this->size)
    {
        return -1;
    }
    else
    {
        if (Size <= end-temp_front)
        {
            front = temp_front + Size;
            return Size;
        }

        else
        {
            int first_length = end - temp_front;
            int second_length = Size - first_length;
            front = begin + second_length;
            return Size;

        }
    }
}

void CRingBuffer::IfEndMoveToFront(char** pCurAddress)
{
    if (*pCurAddress >= end)
    {
        *pCurAddress = begin + 1;
    }
}


void CRingBuffer::ClearBuffer(void)
{
    front = begin;
    rear = begin;
}

//아래 함수들은 주의해서 사용. 실제 가리키는 위치 +1을 반환
char* CRingBuffer::GetFrontBufferPtr(void)
{
    char* temp_front = front;
    if (temp_front == end)
    {
        return begin + 1;
    }
    else
    {
        return temp_front + 1;
    }
}

char* CRingBuffer::GetRearBufferPtr(void)
{
    char* temp_rear = rear;
    if (temp_rear == end)
    {
        return begin + 1;
    }
    else
    {
        return temp_rear + 1;
    }
}

char* CRingBuffer::GetBeginPtr(void)
{
    return (this->begin) +1;
}