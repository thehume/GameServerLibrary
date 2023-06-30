#pragma once

class CRingBuffer
{
public:
    CRingBuffer(void);
    CRingBuffer(int BufferSize);
    ~CRingBuffer();

    int GetBufferSize(void);

    int GetUseSize(void); //현재 사용중인 용량
    int GetFreeSize(void); //남은 용량

    int DirectEnqueueSize(void); // 끊어지지 않은 길이
    int DirectDequeueSize(void); // 끊어지지 않은 길이

    int Enqueue(char* chpData, int Size);
    int Dequeue(char* chpDest, int dequeue_size);

    int	Peek(char* chpDest, int peek_size);

    int MoveRear(int Size);
    int MoveFront(int Size);
    void IfEndMoveToFront(char** pCurAddress);

    void ClearBuffer(void);

    char* GetFrontBufferPtr(void);
    char* GetRearBufferPtr(void);
    char* GetBeginPtr(void);

private:
    int size;
    int usableSize;
    char* begin;
    char* end;
    char* front;
    char* rear;
};