#include <Windows.h>
#include <random>
#include "MemoryPoolBucket.h"
#include "Packet.h"

using namespace std;

CMemoryPoolBucket<CPacket> CPacket::PacketPool;

thread_local mt19937 generator(random_device{}());
uniform_int_distribution<int> range(0, 255);

CPacket::CPacket()
{
	BufferSize = BUFFER_DEFAULT;
	DataSize = 0;
	refCount = 0;
	begin = (char*)malloc(BufferSize);
	end = begin + BufferSize - 1;
	readPos = begin;
	writePos = begin;
	encodeFlag = FALSE;
}

CPacket::~CPacket()
{
	free(begin);
}

void CPacket::addRef(long addNum)
{
	InterlockedExchangeAdd(&refCount, addNum);
}
long CPacket::subRef(void)
{
	return InterlockedDecrement(&refCount);
}

void CPacket::Release(void)
{
	free(begin);
}

void CPacket::Clear(void)
{
	readPos = begin;
	writePos = begin + dfNETWORK_HEADER_SIZE;
	DataSize = dfNETWORK_HEADER_SIZE;
}

void CPacket::ClearNetwork(void)
{
	readPos = begin;
	writePos = begin;
	DataSize = 0;
}

int CPacket::GetBufferSize(void)
{
	return BufferSize; //BufferSize-1까지 넣을수있다.
}

int CPacket::GetDataSize(void)
{
	return writePos - readPos;
}

int CPacket::GetLeftUsableSize(void)
{
	return end - writePos;
}

void CPacket::AddDataSize(int size)
{
	DataSize += size;
}

void CPacket::SubDataSize(int size)
{
	DataSize -= size;
}

char* CPacket::GetWriteBufferPtr(void)
{
	return writePos;
}

char* CPacket::GetReadBufferPtr(void)
{
	return readPos;
}

int CPacket::MoveWritePos(int Size)
{
	if (Size <= 0)
	{
		return 0;
	}

	if (end - writePos < Size)
	{
		return -1;
	}

	writePos += Size;
	DataSize += Size;

	return Size;

}

int CPacket::MoveReadPos(int Size)
{
	if (Size <= 0)
	{
		return 0;
	}

	if (writePos - readPos < Size)
	{
		return -1;
	}

	readPos += Size;
	DataSize -= Size;

	return Size;
}

BOOL CPacket::Encode()
{
	if (DataSize <= 5)
	{
		return FALSE;
	}

	st_header* headerPos = (st_header*)this->begin;
	headerPos->code = dfNETWORK_CODE;
	headerPos->len = writePos - readPos - dfNETWORK_HEADER_SIZE;
	unsigned char randkey = range(generator);
	headerPos->randkey = randkey;

	int Sum = 0;
	unsigned char* payloadPos = (unsigned char*)this->begin + dfNETWORK_HEADER_SIZE;
	for (int i = 0; i < DataSize - dfNETWORK_HEADER_SIZE; i++)
	{
		Sum += *payloadPos;
		payloadPos++;
	}
	headerPos->checksum = Sum % 256;

	unsigned char* encodingPos = &headerPos->checksum;
	unsigned char lastE = 0;
	unsigned char lastP = 0;
	for (int i = 0; i < DataSize - dfNETWORK_HEADER_SIZE + 1; i++)
	{
		lastP = (*encodingPos) ^ (lastP + randkey + i + 1);
		*encodingPos = lastP ^ (lastE + dfNETWORK_KEY + i + 1);
		lastE = *encodingPos;
		encodingPos++;
	}

	InterlockedExchange((LONG*)&encodeFlag, TRUE);
	return TRUE;
}

BOOL CPacket::Decode()
{
	if (DataSize <= 5)
	{
		return FALSE;
	}

	st_header* headerPos = (st_header*)this->begin;

	unsigned char* decodingPos = &headerPos->checksum;
	unsigned char key = dfNETWORK_KEY;
	unsigned char randkey = headerPos->randkey;
	unsigned char lastE = 0;
	unsigned char lastP = 0;
	unsigned char nowP = 0;
	for (int i = 0; i < headerPos->len + 1; i++)
	{
		nowP = *decodingPos ^ (lastE + key + i + 1);
		lastE = *decodingPos;
		*decodingPos = nowP ^ (lastP + randkey + i + 1);
		lastP = nowP;
		decodingPos++;
	}
	//checksum 재생성후 확인
	int Sum = 0;
	unsigned char* payloadPos = (unsigned char*)headerPos + sizeof(st_header);
	for (int i = 0; i < headerPos->len; i++)
	{
		Sum += *payloadPos;
		payloadPos++;
	}
	unsigned char checkSum = Sum % 256;
	if (checkSum != headerPos->checksum)
	{
		return FALSE;
	}

	return TRUE;
}



CPacket* CPacket::mAlloc()
{
	CPacket* temp;
	PacketPool.mAlloc(&temp);
	temp->refCount = 0;
	temp->encodeFlag = FALSE;
	temp->MoveWritePos(dfNETWORK_HEADER_SIZE);
	return temp;
}

bool CPacket::mFree(CPacket* inParam)
{
	return PacketPool.mFree(inParam);
}

LONG CPacket::getPoolUseSize()
{
	return PacketPool.getUseSize();
}

CPacket& CPacket::operator = (CPacket& SrcPacket)
{
	BufferSize = SrcPacket.BufferSize;
	DataSize = SrcPacket.DataSize;

	begin = (char*)malloc(BufferSize);
	end = begin + BufferSize - 1;

	memcpy(begin, SrcPacket.begin, SrcPacket.BufferSize);

	readPos = begin + (SrcPacket.readPos - SrcPacket.begin);
	writePos = begin + (SrcPacket.writePos - SrcPacket.begin);

	return *this;
}

//넣기

CPacket& CPacket::operator << (unsigned char byValue)
{
	if (end - writePos >= sizeof(unsigned char))
	{
		*(unsigned char*)writePos = byValue;
		writePos += sizeof(unsigned char);
		DataSize += sizeof(unsigned char);
	}
	return *this;
}

CPacket& CPacket::operator << (char chValue)
{
	if (end - writePos >= sizeof(char))
	{
		*writePos = chValue;
		writePos += sizeof(char);
		DataSize += sizeof(char);
	}
	return *this;
}

CPacket& CPacket::operator << (short shValue)
{
	if (end - writePos >= sizeof(short))
	{
		*(short*)writePos = shValue;
		writePos += sizeof(short);
		DataSize += sizeof(short);
	}
	return *this;
}
CPacket& CPacket::operator << (unsigned short wValue)
{
	if (end - writePos >= sizeof(unsigned short))
	{
		*(unsigned short*)writePos = wValue;
		writePos += sizeof(unsigned short);
		DataSize += sizeof(unsigned short);
	}
	return *this;
}

CPacket& CPacket::operator << (int iValue)
{
	if (end - writePos >= sizeof(int))
	{
		*(int*)writePos = iValue;
		writePos += sizeof(int);
		DataSize += sizeof(int);
	}
	return *this;

}
CPacket& CPacket::operator << (unsigned int iValue)
{
	if (end - writePos >= sizeof(int))
	{
		*(int*)writePos = iValue;
		writePos += sizeof(int);
		DataSize += sizeof(int);
	}
	return *this;
}

CPacket& CPacket::operator << (long lValue)
{
	if (end - writePos >= sizeof(long))
	{
		*(long*)writePos = lValue;
		writePos += sizeof(long);
		DataSize += sizeof(long);
	}
	return *this;
}

CPacket& CPacket::operator << (unsigned long lValue)
{
	if (end - writePos >= sizeof(unsigned long))
	{
		*(unsigned long*)writePos = lValue;
		writePos += sizeof(unsigned long);
		DataSize += sizeof(unsigned long);
	}
	return *this;

}
CPacket& CPacket::operator << (float fValue)
{
	if (end - writePos >= sizeof(float))
	{
		*(float*)writePos = fValue;
		writePos += sizeof(float);
		DataSize += sizeof(float);
	}
	return *this;
}

CPacket& CPacket::operator << (__int64 iValue)
{
	if (end - writePos >= sizeof(__int64))
	{
		*(__int64*)writePos = iValue;
		writePos += sizeof(__int64);
		DataSize += sizeof(__int64);
	}
	return *this;
}

CPacket& CPacket::operator << (double dValue)
{
	if (end - writePos >= sizeof(double))
	{
		*(double*)writePos = dValue;
		writePos += sizeof(double);
		DataSize += sizeof(double);
	}
	return *this;

}

// 빼기


CPacket& CPacket::operator >> (unsigned char& byValue)
{
	if (writePos - readPos >= sizeof(unsigned char))
	{
		byValue = *(unsigned char*)readPos;
		readPos += sizeof(unsigned char);
		DataSize -= sizeof(unsigned char);
	}
	return *this;
}

CPacket& CPacket::operator >> (char& chValue)
{
	if (writePos - readPos >= sizeof(char))
	{
		chValue = *(char*)readPos;
		readPos += sizeof(char);
		DataSize -= sizeof(char);
	}
	return *this;
}

CPacket& CPacket::operator >> (short& shValue)
{
	if (writePos - readPos >= sizeof(short))
	{
		shValue = *(short*)readPos;
		readPos += sizeof(short);
		DataSize -= sizeof(short);
	}
	return *this;

}
CPacket& CPacket::operator >> (unsigned short& wValue)
{
	if (writePos - readPos >= sizeof(unsigned short))
	{
		wValue = *(unsigned short*)readPos;
		readPos += sizeof(unsigned short);
		DataSize -= sizeof(unsigned short);
	}
	return *this;
}

CPacket& CPacket::operator >> (int& iValue)
{
	if (writePos - readPos >= sizeof(int))
	{
		iValue = *(int*)readPos;
		readPos += sizeof(int);
		DataSize -= sizeof(int);
	}
	return *this;
}

CPacket& CPacket::operator >> (unsigned int& iValue)
{
	if (writePos - readPos >= sizeof(unsigned int))
	{
		iValue = *(unsigned int*)readPos;
		readPos += sizeof(unsigned int);
		DataSize -= sizeof(unsigned int);
	}
	return *this;
}

CPacket& CPacket::operator >> (long& dwValue)
{
	if (writePos - readPos >= sizeof(long))
	{
		dwValue = *(long*)readPos;
		readPos += sizeof(long);
		DataSize -= sizeof(long);
	}
	return *this;

}

CPacket& CPacket::operator >> (unsigned long& dwValue)
{
	if (writePos - readPos >= sizeof(unsigned long))
	{
		dwValue = *(unsigned long*)readPos;
		readPos += sizeof(unsigned long);
		DataSize -= sizeof(unsigned long);
	}
	return *this;

}
CPacket& CPacket::operator >> (float& fValue)
{
	if (writePos - readPos >= sizeof(float))
	{
		fValue = *(float*)readPos;
		readPos += sizeof(float);
		DataSize -= sizeof(float);
	}
	return *this;
}

CPacket& CPacket::operator >> (__int64& iValue)
{
	if (writePos - readPos >= sizeof(__int64))
	{
		iValue = *(__int64*)readPos;
		readPos += sizeof(__int64);
		DataSize -= sizeof(__int64);
	}
	return *this;
}

CPacket& CPacket::operator >> (double& dValue)
{
	if (writePos - readPos >= sizeof(double))
	{
		dValue = *(double*)readPos;
		readPos += sizeof(double);
		DataSize -= sizeof(double);
	}
	return *this;
}

int CPacket::GetData(char* chpDest, int GetSize)
{
	if (GetSize <= 0)
	{
		return 0;
	}

	if (writePos - readPos < GetSize)
	{
		return -1;
	}

	memcpy(chpDest, readPos, GetSize);
	readPos += GetSize;
	DataSize -= GetSize;

	return GetSize;
}

int CPacket::PutData(char* chpSrc, int PutSize)
{
	if (PutSize <= 0)
	{
		return 0;
	}

	if (end - writePos < PutSize)
	{
		return -1;
	}

	memcpy(writePos, chpSrc, PutSize);
	writePos += PutSize;
	DataSize += PutSize;

	return PutSize;
}