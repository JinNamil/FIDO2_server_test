#pragma once

#include "resource.h"
#include <cstdio>
#include "ASMTypes.h"
#include "HIDComm.h"
#include "ASM.h"
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include "hidapi.h"
#include "cn-cbor.h"
#include "cbor.h"

#define USB_VID						 (0xE383)
#define USB_PID					     (0x0007)
#define UPDATE_PACKET_BLOCK_SIZE     (1536)
#define HID_PACKET_SIZE				 (64)         /* EP2, EP3 wMaxPacketSize */
#define HID_CMD_PACKET				 (0x80)
#define HID_CMD_NONE				 (0x00)
#define HID_CMD_UAF					 (HID_CMD_PACKET | 0x41)
#define HID_CMD_FINGERPRINT			 (HID_CMD_PACKET | 0x51)
#define HID_CMD_UTIL				 (HID_CMD_PACKET | 0x52)
#define HID_CID						 (0x01020304)



typedef enum cborcount
{
	normal,
	inmap,
	map
};

typedef enum stringdist
{
	sstart,
	send,
	bstart,
	bend,
	etc
};

#pragma pack(push, 1)
typedef struct
{
	unsigned int  CID;                       // Channel identifier
	unsigned char CMD;                       // Command identifier(bit 7 always set)
	unsigned char BCNTH;                     // High part of payload length
	unsigned char BCNTL;                     // Low part of payload length
	unsigned char DATA[(HID_PACKET_SIZE - 7)]; // Payload data(s is equal to the fixed packet size(:64))
} InitPacket_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct
{
	unsigned int datasize;
	char data[1];
} FilePacket_t;
#pragma pack(pop)

/* FirmwareUpdateOut Dictionary */
#pragma pack(push, 1)
typedef struct {
	unsigned short      statusCode;
	unsigned short      responseType;
	unsigned int        datasize;
	unsigned char		data[1];
} FileDataOut_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct
{
	unsigned int  CID;                       // Channel identifier
	unsigned char SEQ;                       // Packet sequence 0x00..0x7f (bit 7 always cleared)
	unsigned char DATA[(HID_PACKET_SIZE - 5)]; // Payload data (s is equal to the fixed packet size(:64))
} ContPacket_t;
#pragma pack(pop)

typedef struct _buffer {
	size_t sz;
	unsigned char *ptr;
} buffer;

typedef void * Element; //void * 형식을 Element 형식 명으로 정의
typedef struct Queue //Queue 구조체 정의
{
	Element *buf;//저장소
	int qsize;
	int front; //꺼낼 인덱스(가장 오래전에 보관한 데이터가 있는 인덱스)
	int rear;//보관할 인덱스
	int count;//보관 개수

}Queue;

Queue *NewQueue();//생성자
void DeleteQueue(Queue *queue);//소멸자
int IsEmpty(Queue *queue); //큐가 비었는지 확인
void Enqueue(Queue *queue, Element data); //큐에 보관
Element Dequeue(Queue *queue); //큐에서 꺼냄


typedef struct Data
{
	char *name;
}Data;

Data *NewData(const char *name)
{
	Data *data = (Data *)malloc(sizeof(Data));
	data->name = (char *)malloc(strlen(name) + 1);
	strcpy_s(data->name, strlen(name) + 1, name);
	return data;
}

class WindowsProject2
{
public:

	HIDComm * hidComm = NULL;
	WindowsProject2(const char* deviceName, unsigned long cid = HID_CID);
	WindowsProject2(WindowsProject2& Other);
	WindowsProject2() {};
	virtual ~WindowsProject2();

	int HIDRead(unsigned char*  readBuffer, const unsigned long readBufferLength, int cmdType);
	int HIDRead(unsigned char** readBuffer, int cmdType);
	int HIDWrite(unsigned char* writeBuffer, const unsigned long payload, int cmdType);

	int CborEncoding(unsigned char*  readBuffer, const unsigned long readBufferLength, int mapNum, int arrayNum, char* value);
	int CborDecoding(const char* PathBuffer);
	int FileSend(char* PathBuffer);
	int FileDecoding(char* PathBuffer);
	int Init(void);
	int Uninit(void);
	int EncoderCount(int tag, char* originalFile, int Fileint, char searchChar);
	int EncoderinStruct(char *originalFile);
	void MiddleEncoder(char* PathBuffer);
private:
	char* result = NULL;
	char* cborstring = NULL;
	char* cborvalue = NULL;
	int mapNum = 0;
	int arrayNum = 0;
	int inmapNum = 1;
	int mapArray[100];
	int mapArrayNum = 0;
	char* deviceName = NULL;
	unsigned long cid;
	int FileRead(const char* fileName, unsigned char** updateBinary, unsigned int* updateBinarySize);
	int WindowsProject2::FileRead_t(FILE* file, unsigned char** updateBinary, unsigned int* updateBinarySize);
};

