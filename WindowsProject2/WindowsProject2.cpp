// WindowsProject2.cpp: 응용 프로그램의 진입점을 정의합니다.
//

#pragma comment(lib,"Comctl32.lib")


#include "stdafx.h"
#include "HIDComm.h"
#include <stdio.h>
#include <cstdio>
#include <Commdlg.h>
#include "WindowsProject2.h"
#include "Debug.h"
#include <iostream>
#include <windows.h>
#include <commctrl.h>
#include <process.h>
#include <wchar.h>
#include <string.h>
#include <stdlib.h>
#include <dbt.h>
#include <ctime>
#include <io.h>
#include "hidapi.h"
#include "cbor.h"
#include "cn-cbor.h"
#include <sys/stat.h>
#include "fcntl.h"
#include "cbor_cpp.h"
#include "rapidjson/document.h"

#define NEXT(index,QSIZE)   ((index+1)%QSIZE)  //원형 큐에서 인덱스를 변경하는 매크로 함수
#define BUFFERSIZE (8192)

#ifdef USE_CBOR_CONTEXT
#define CBOR_CONTEXT_PARAM , NULL
#else
#define CBOR_CONTEXT_PARAM
#endif

#define ERROR(msg, p) fprintf(stderr, "ERROR: " msg " %s\n", (p));

using namespace ASMType;
using namespace rapidjson;
using rapidjson::Value;

Document document;
HINSTANCE hInst;
HWND  gHwnd = NULL;             
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
OPENFILENAME ofn;
char filePathBuffer[MAX_PATH] = { 0, };
WindowsProject2* windowsProject2 = NULL;
struct hid_device_info *devs, *cur_dev;

static unsigned char* load_file(const char* filepath, unsigned char **end) {
	struct stat st;
	if (stat(filepath, &st) == -1) {
		ERROR("can't find file", filepath);
		return 0;
	}
	int fd = _open(filepath, O_RDONLY);
	if (fd == -1) {
		ERROR("can't open file", filepath);
		return 0;
	}
	unsigned char* text = new unsigned char[st.st_size + 1]; // this is not going to be freed
	if (st.st_size != _read(fd, text, st.st_size)) {
		ERROR("can't read file", filepath);
		_close(fd);
		return 0;
	}
	_close(fd);
	text[st.st_size] = '\0';
	*end = text + st.st_size;
	return text;
}

static void dump(const cn_cbor* cb, char* out, char** end, int indent) {
	if (!cb)
		goto done;
	int i;
	cn_cbor* cp;
	char finchar = ')';           /* most likely */

#define CPY(s, l) memcpy(out, s, l); out += l;
#define OUT(s) CPY(s, sizeof(s)-1)
#define PRF(f, a) out += sprintf(out, f, a)

	for (i = 0; i < indent; i++) *out++ = ' ';
	switch (cb->type) {
	case CN_CBOR_TEXT_CHUNKED:   OUT("(_\n");                  goto sequence;
	case CN_CBOR_BYTES_CHUNKED:  OUT("(_\n\n");                goto sequence;
	case CN_CBOR_TAG:            PRF("%ld(\n", cb->v.sint);    goto sequence;
	case CN_CBOR_ARRAY:  finchar = ']'; OUT("[\n");            goto sequence;
	case CN_CBOR_MAP:    finchar = '}'; OUT("{\n");            goto sequence;
	sequence:
		for (cp = cb->first_child; cp; cp = cp->next) {
			dump(cp, out, &out, indent + 2);
		}
		for (i = 0; i<indent; i++) *out++ = ' ';
		*out++ = finchar;
		break;
	case CN_CBOR_BYTES:   OUT("h'");
		for (i = 0; i<cb->length; i++)
			PRF("%02x", cb->v.str[i] & 0xff);
		*out++ = '\'';
		break;
	case CN_CBOR_TEXT:    *out++ = '"';
		CPY(cb->v.str, cb->length); /* should escape stuff */
		*out++ = '"';
		break;
	case CN_CBOR_NULL:   OUT("null");                      break;
	case CN_CBOR_TRUE:   OUT("true");                      break;
	case CN_CBOR_FALSE:  OUT("false");                     break;
	case CN_CBOR_UNDEF:  OUT("simple(23)");                break;
	case CN_CBOR_INT:    PRF("%ld", cb->v.sint);           break;
	case CN_CBOR_UINT:   PRF("%lu", cb->v.uint);           break;
	case CN_CBOR_DOUBLE: PRF("%e", cb->v.dbl);             break;
	case CN_CBOR_SIMPLE: PRF("simple(%ld)", cb->v.sint);   break;
	default:             PRF("???%d???", cb->type);        break;
	}
	*out++ = '\n';
done:
	*end = out;
}

/* init */
int WindowsProject2::Init(void)
{
	devs = hid_enumerate(0xE383, 0x0007);
	if (devs == NULL)
	{
		MessageBox(gHwnd, "Device 연결을 확인해주세요.", "", MB_OK);
		return 0;
	}
	cur_dev = devs;
	DBG_Log("init start");

	if (windowsProject2 != NULL)
	{
		return 1;
	}
	windowsProject2 = new WindowsProject2();
	if (windowsProject2 == NULL)
	{
		return 1;
	}

	windowsProject2 = new WindowsProject2(cur_dev->path);
	DBG_Log("devicepath: %s", cur_dev->path);
	hid_free_enumeration(devs);
	return 0;
}

/* Uninit */
int WindowsProject2::Uninit(void)
{
	DBG_Log("uinit start");

	if (windowsProject2 == NULL)
	{
		return 1;
	}

	delete windowsProject2;
	windowsProject2 = NULL;

	
	return 0;
}

WindowsProject2::WindowsProject2(const char* deviceName,unsigned long cid)
	: deviceName(NULL),cid(cid)
{
	int deviceNameLen = (int)strlen(deviceName);
	this->deviceName = new char[deviceNameLen + 1]{ 0, };
	if (this->deviceName != NULL)
		memcpy(this->deviceName, deviceName, deviceNameLen);

	hidComm = new HIDComm(deviceName);
}

WindowsProject2::WindowsProject2(WindowsProject2& other)
{
	delete hidComm;
	hidComm = NULL;

	int deviceNameLen = (int)strlen(other.deviceName);
	this->deviceName = new char[deviceNameLen + 1]{ 0, };
	if (this->deviceName != NULL)
		memcpy(this->deviceName, other.deviceName, deviceNameLen);
}

WindowsProject2::~WindowsProject2()
{
	if (this->deviceName != NULL)
	{
		delete[] this->deviceName;
		this->deviceName = NULL;
	}
	hid_exit();
}

int WindowsProject2::HIDRead(unsigned char* readBuffer, const unsigned long readBufferLength, int cmdType)
{
	int ret = 0;
	unsigned char readPacketBuffer[HID_PACKET_SIZE] = { 0, };
	unsigned int payload = 0;

	unsigned int timeOut = 0;
	switch (cmdType)
	{
	case HID_CMD_UAF:
	{
		timeOut = 5000;
	}
	break;

	case HID_CMD_FINGERPRINT:
	{
		timeOut = 100000;
	}
	break;

	case HID_CMD_UTIL:
	{
		timeOut = 20000;
	}
	break;
	}


	/* Check Parameter */
	if (readBuffer == NULL)
	{
		DBG_Log("error");
		return -1;
	}

	/* check Device */
	if (hidComm == NULL)
	{
		DBG_Log("error");
		return -1;
	}

	ret = hidComm->Read(readPacketBuffer, HID_PACKET_SIZE, timeOut);
	if (ret != 0)
	{
		DBG_Log("Read Fail");
		return -1;
	}

	/* read 64 Bytes(initial packet) */
	InitPacket_t* initPacket = (InitPacket_t*)readPacketBuffer;

	if (initPacket->CID != cid)
	{
		//dump(readPacketBuffer, HID_PACKET_SIZE, "receive Packet");
		DBG_Log("error");
		return -1;
	}

	if ((initPacket->CMD != HID_CMD_UAF) && (initPacket->CMD != HID_CMD_FINGERPRINT) && (initPacket->CMD != HID_CMD_UTIL))
	{
		//dump(readPacketBuffer, HID_PACKET_SIZE, "receive Packet");
		DBG_Log("error");
		return -1;
	}

	/* Check the Packet Header and Payload */
	payload = (((initPacket->BCNTH & 0xFF) << 8) | (initPacket->BCNTL & 0xFF));
	if (payload > readBufferLength)
	{
		//dump(readPacketBuffer, HID_PACKET_SIZE, "receive Packet");
		DBG_Log("error, payload is too big %d", payload);
		return -1;
	}

	/* copy var.. */
	unsigned int   copySize = 0;
	unsigned int   copyTotal = 0;
	unsigned char* copyDst = readBuffer;

	/* Copy to buffer */
	copySize = payload;
	if (copySize > sizeof(initPacket->DATA))
	{
		copySize = sizeof(initPacket->DATA);
	}
	copyTotal = 0;
	copyDst = readBuffer;
	if (memcpy(copyDst, initPacket->DATA, copySize) != copyDst)
	{
		DBG_Log("error");
		return -1;
	}
	copyTotal += copySize;
	copyDst += copySize;

	/* Reads continue while seq is lower than the totalSeuquence */
	const int totalSeuquence = ((copyTotal < payload) ? ((payload - (HID_PACKET_SIZE - 7)) / (HID_PACKET_SIZE - 5)) : 0);
	ContPacket_t* contPacket = (ContPacket_t*)readPacketBuffer;
	while (copyTotal < payload)
	{
		/* read 64 Bytes(continue packet) */
		memset(readPacketBuffer, 0, sizeof(readPacketBuffer));
		ret = hidComm->Read(readPacketBuffer, HID_PACKET_SIZE, timeOut);
		if (ret != 0)
		{
			DBG_Log("Read Fail");
			return -1;
		}

		if (initPacket->CID != cid)
		{
			DBG_Log("error");
			return -1;
		}

		if (contPacket->SEQ > totalSeuquence)
		{
			DBG_Log("error, invalid sequence, seq: %d, total: %d", contPacket->SEQ, totalSeuquence);
			return -1;
		}

		/* Copy to buffer */
		copySize = payload - copyTotal;
		if (copySize > sizeof(contPacket->DATA))
		{
			copySize = sizeof(contPacket->DATA);
		}

		if (memcpy(copyDst, contPacket->DATA, copySize) != copyDst)
		{
			DBG_Log("error");
			return -1;
		}
		copyTotal += copySize;
		copyDst += copySize;
	}

	ret = copyTotal;
	return ret;
}

int WindowsProject2::HIDRead(unsigned char** readBuffer, int cmdType)
{
	int ret = 0;
	unsigned char readPacketBuffer[HID_PACKET_SIZE] = { 0, };
	unsigned int payload = 0;

	unsigned int timeOut = 0;
	switch (cmdType)
	{
	case HID_CMD_UAF:
	{
		timeOut = 20000;
	}
	break;

	case HID_CMD_FINGERPRINT:
	{
		timeOut = 100000;
	}
	break;

	case HID_CMD_UTIL:
	{
		timeOut = 20000;
	}
	break;
	}


	/* Check Parameter */
	if (*readBuffer != NULL)
	{
		DBG_Log("error");
		return -1;
	}

	/* check Device */
	if (hidComm == NULL)
	{
		DBG_Log("error");
		return -1;
	}

	ret = hidComm->Read(readPacketBuffer, HID_PACKET_SIZE, timeOut);
	if (ret != 0)
	{
		DBG_Log("Read Fail");
		return -1;
	}

	/* read 64 Bytes(initial packet) */
	InitPacket_t* initPacket = (InitPacket_t*)readPacketBuffer;

	if (initPacket->CID != cid)
	{
		//dump(readPacketBuffer, HID_PACKET_SIZE, "receive Packet");
		DBG_Log("error");
		return -1;
	}

	if ((initPacket->CMD != HID_CMD_UAF) && (initPacket->CMD != HID_CMD_FINGERPRINT) && (initPacket->CMD != HID_CMD_UTIL))
	{
		//dump(readPacketBuffer, HID_PACKET_SIZE, "receive Packet");
		DBG_Log("error");
		return -1;
	}

	/* Check the Packet Header and Payload */
	payload = (((initPacket->BCNTH & 0xFF) << 8) | (initPacket->BCNTL & 0xFF));
	*readBuffer = new unsigned char[payload] { 0, };
	if (*readBuffer == NULL)
	{
		DBG_Log("new error");
		return -1;
	}

	/* copy var.. */
	unsigned int   copySize = 0;
	unsigned int   copyTotal = 0;
	unsigned char* copyDst = *readBuffer;

	/* Copy to buffer */
	copySize = payload;
	if (copySize > sizeof(initPacket->DATA))
	{
		copySize = sizeof(initPacket->DATA);
	}
	copyTotal = 0;
	copyDst = *readBuffer;
	if (memcpy(copyDst, initPacket->DATA, copySize) != copyDst)
	{
		DBG_Log("error");
		return -1;
	}
	copyTotal += copySize;
	copyDst += copySize;

	/* Reads continue while seq is lower than the totalSeuquence */
	const int totalSeuquence = ((copyTotal < payload) ? ((payload - (HID_PACKET_SIZE - 7)) / (HID_PACKET_SIZE - 5)) : 0);
	ContPacket_t* contPacket = (ContPacket_t*)readPacketBuffer;
	while (copyTotal < payload)
	{
		/* read 64 Bytes(continue packet) */
		memset(readPacketBuffer, 0, sizeof(readPacketBuffer));
		ret = hidComm->Read(readPacketBuffer, HID_PACKET_SIZE, timeOut);
		if (ret != 0)
		{
			DBG_Log("Read Fail");
			return -1;
		}

		if (initPacket->CID != cid)
		{
			DBG_Log("error");
			return -1;
		}

		if (contPacket->SEQ > totalSeuquence)
		{
			DBG_Log("error, invalid sequence, seq: %d, total: %d", contPacket->SEQ, totalSeuquence);
			return -1;
		}

		/* Copy to buffer */
		copySize = payload - copyTotal;
		if (copySize > sizeof(contPacket->DATA))
		{
			copySize = sizeof(contPacket->DATA);
		}

		if (memcpy(copyDst, contPacket->DATA, copySize) != copyDst)
		{
			DBG_Log("error");
			return -1;
		}
		copyTotal += copySize;
		copyDst += copySize;
	}

	ret = copyTotal;
	return ret;
}

int WindowsProject2::HIDWrite(unsigned char* writeBuffer, const unsigned long payload, int cmdType)
{
	int ret = 0;
	unsigned char writePacketBuffer[HID_PACKET_SIZE] = { 0, };

	/* Check Parameter */
	if (writeBuffer == NULL || !(cmdType == HID_CMD_UAF || cmdType == HID_CMD_FINGERPRINT || cmdType == HID_CMD_UTIL))
	{
		DBG_Log("error");
		return -1;
	}

	unsigned int timeOut = 0;
	switch (cmdType)
	{
	case HID_CMD_UAF:
	{
		timeOut = 5000;
	}
	break;

	case HID_CMD_FINGERPRINT:
	{
		timeOut = 5000;
	}
	break;

	case HID_CMD_UTIL:
	{
		timeOut = 5000;
	}
	break;
	}

	/* check Device */
	if (hidComm == NULL)
	{
		DBG_Log("error");
		return -1;
	}

	/* Make the initilal packet */
	InitPacket_t* initPacket = (InitPacket_t*)writePacketBuffer;
	memset(writePacketBuffer, 0, sizeof(writePacketBuffer));
	initPacket->CID = cid;
	initPacket->CMD = cmdType;
	initPacket->BCNTH = (payload >> 8) & 0xFF;
	initPacket->BCNTL = payload & 0xFF;

	/* copy var.. */
	unsigned int   copySize = 0;
	unsigned int   copyTotal = 0;
	unsigned char* copySrc = writeBuffer;

	copySize = payload;
	if (copySize > sizeof(initPacket->DATA))
	{
		copySize = sizeof(initPacket->DATA);
	}
	memset(initPacket->DATA, 0xcc, sizeof(initPacket->DATA));
	if (memcpy(initPacket->DATA, copySrc, copySize) != initPacket->DATA)
	{
		DBG_Log("error");
		return -1;
	}

	copyTotal += copySize;
	copySrc += copySize;

	/* send 64 Bytes(initial packet) */
	ret = hidComm->Write(writePacketBuffer, HID_PACKET_SIZE, timeOut);
	if (ret != 0)
	{
		DBG_Log("Write Fail");
		return -1;
	}

	/* write continue until seq is higher than the totalSeuquence */
	const int totalSeuquence = (payload - (HID_PACKET_SIZE - 7)) / (HID_PACKET_SIZE - 5);
	ContPacket_t* contPacket = (ContPacket_t*)writePacketBuffer;
	memset(writePacketBuffer, 0, sizeof(writePacketBuffer));
	contPacket->CID = cid;
	contPacket->SEQ = 0;
	while (copyTotal < payload)
	{
		if (contPacket->SEQ > totalSeuquence || contPacket->SEQ > 127)
		{
			DBG_Log("error");
			return -1;
		}

		copySize = (payload - copyTotal);
		if (copySize > sizeof(contPacket->DATA))
		{
			copySize = sizeof(contPacket->DATA);
		}
		memset(contPacket->DATA, 0xcc, sizeof(contPacket->DATA));
		if (memcpy(contPacket->DATA, copySrc, copySize) != contPacket->DATA)
		{
			DBG_Log("error");
			return -1;
		}

		copyTotal += copySize;
		copySrc += copySize;

		/* send 64 Bytes(initial packet) */
		ret = hidComm->Write(writePacketBuffer, HID_PACKET_SIZE, timeOut);
		if (ret != 0)
		{
			DBG_Log("Write Fail");
			return -1;
		}

		contPacket->SEQ++;
	}

	ret = copyTotal;
	return ret;
}

/* Make binFile */
int WindowsProject2::FileRead(const char* fileName, unsigned char** updateBinary, unsigned int* updateBinarySize)
{
	int ret = -1;

	if (updateBinarySize == NULL)
	{
		return -1;
	}

	FILE* imageFile = fopen(fileName, "rb");
	if (imageFile == NULL)
	{
		return -1;
	}
	unsigned int imageFileSize = 0;
	ret = fseek(imageFile, 0, SEEK_END);
	if (ret != 0)
	{
		fclose(imageFile);
		return -1;
	}
	imageFileSize = ftell(imageFile);
	if (imageFileSize < 0)
	{
		fclose(imageFile);
		return -1;
	}
	ret = fseek(imageFile, 0, SEEK_SET);
	if (ret != 0)
	{
		fclose(imageFile);
		return -1;
	}

	unsigned char* imageFileBuffer = NULL;

	imageFileBuffer = new unsigned char[imageFileSize] {0, };
	if (imageFileBuffer == NULL)
	{
		fclose(imageFile);
		DBG_Log("memory error");
		return -1;
	}

	*updateBinarySize = imageFileSize;
	*updateBinary = new unsigned char[*updateBinarySize]{ 0, };
	imageFileBuffer = *updateBinary;

	fread(imageFileBuffer, 1, imageFileSize, imageFile);

	ret = fclose(imageFile);

	if (ret != 0)
	{
		DBG_Log("fclose error");
		return -1;
	}
	imageFile = NULL;


	return 0;
}

/* CBOR Decoding */
int WindowsProject2::CborDecoding(const char* filepath)
{
	cn_cbor *cb;
	signed int enc_sz;
	unsigned char encoded[BUFFERSIZE];
	int i = 0;
	char buf[8192];
	unsigned char *end;
	char *bufend;
		
	unsigned char* s = load_file(filepath, &end);

	cb = cn_cbor_decode(s, end-s CBOR_CONTEXT_PARAM, 0);
	if (cb) {
		dump(cb, buf, &bufend, 0);
		*bufend = 0;
		DBG_Log("%s\n", buf);
		cn_cbor_free(cb CBOR_CONTEXT_PARAM);
		cb = 0;                     /* for leaks testing */
	}

	FILE* bin = fopen("./cbor_decode.txt", "wb");

	fwrite(buf, strlen(buf), 1, bin);
	fclose(bin);

	return 0;
}

/* CBOR Encoding */
int WindowsProject2::CborEncoding(unsigned char*  readBuffer, const unsigned long readBufferLength, int mapNum, int arrayNum, char* value)
{

	unsigned char encoded[BUFFERSIZE];
	cn_cbor *map;
	cn_cbor *cdata = NULL;
	cn_cbor *cb_int;
	signed int enc_sz;
	int datasize = 0;
	
	cdata = cn_cbor_string_create((const char*)readBuffer, CBOR_CONTEXT NULL);

	enc_sz = cn_cbor_encoder_write(encoded, 0, sizeof(encoded), cdata);


	if (enc_sz < 0)
	{
		DBG_Log("Encoding Fail. enc_sz : %d", enc_sz);
		return -1;
	}

	DBG_Log("cdata: %s", cdata->v.str);
	DBG_Log("datasize: %d", enc_sz);
	FILE* bin = fopen("./cbor_encode.bin", "wb");

	fwrite(encoded, enc_sz, 1, bin);

	fclose(bin);
	
	return 0;
}

int WindowsProject2::FileDecoding(char* PathBuffer)
{
	int ret = 0;

	/* CBOR Decoding */
	
	ret = CborDecoding(PathBuffer);
	if (ret != 0)
	{
		DBG_Log("CBOR Decoding Fail");
		return -1;
	}
	else
	{
		DBG_Log("CBOR Decoding Success");
	}

	return 0;
}

/*	normal: Fileint = NULL
	inmap:  searchChar = NULL
	map:    searchChar = NULL*/
int WindowsProject2::EncoderCount(int tag, char* originalFile, int Fileint, char searchChar)
{
	int i, count, incount;
	count = 0;
	
	switch (tag)
	{
		case normal:
			for (i = 0; originalFile[i] != NULL; i++)
			{
				if (originalFile[i] == searchChar)
					count++;
				else
					continue;
			}
			return count;

		case inmap:											// Update Plz
			Fileint++;
			for (i = Fileint; originalFile[i] != NULL; i++)
			{
				if (originalFile[i] == '}')
				{
					break;
				}

				if (originalFile[i] == '{')
				{
					do
					{
						i++;
					} while (originalFile[i] != '}');
				}

				if (originalFile[i] == '[')
				{
					do
					{
						i++;
					} while (originalFile[i] != ']');
				}

				if (originalFile[i] == ',')
					count++;
				else
					continue;
			}
			return count+1;

		case map:
			for (i = Fileint; originalFile[i] != NULL; i++)
			{
				if (originalFile[i] == ']')
				{
					break;
				}

				if (originalFile[i] == '{')
					count++;
				else
					continue;
			}
			return count;
	}
	
}

//int WindowsProject2::EncoderString(char* originalFile)
//{
//	char EncodingFile[BUFFERSIZE]{ 0, };
//	result = strtok(originalFile, "()|{}, h’\t.;&-[]\"\':`");
//
//	if (result == NULL)
//	{
//		return 0;
//	}
//
//	cborstring = result;
//
//	result = strtok(NULL, "");
//	
//	
//	sprintf(EncodingFile, result, strlen(result));
//	for (int a = 0; EncodingFile[a] != NULL; a++)
//	{
//		if (EncodingFile[a] == ':')
//		{
//			result = strtok(EncodingFile, "()|{}, h’\t.;&-[]\"\':`");
//
//			cborvalue = result;
//			result = strtok(NULL, "");
//		}
//		else if (EncodingFile[a] == '\"' || EncodingFile[a] == ',')
//			continue;
//	}
//	DBG_Log("\nstring: %s\nvalue: %s", cborstring, cborvalue);
//
//	while (result != NULL)
//	{
//		EncoderString(result);
//	}
//
//	return 0;
//}

int WindowsProject2::EncoderinStruct(char *originalFile)
{
	int ret = -1;
	int a = 0;
	char cborchar = NULL;
	int stringdist = -1;
	cbor::output_dynamic output;
	cbor::encoder encoder(output);
	
	char fpName[64] = { 0, };
	unsigned char* fwBinaryBuffer = NULL;
	unsigned int   fwBinaryBufferSize = 0;
	unsigned char  buffer[BUFFERSIZE] = { 0, };

	TLV_t* response = (TLV_t*)buffer;

	for (int i = 0; originalFile[i] != NULL; i++)
	{
		cborchar = originalFile[i];
		char Encoding[BUFFERSIZE]{ 0, };

		switch (cborchar)
		{
			case '[':
				arrayNum = EncoderCount(map, originalFile, i, NULL);
				encoder.write_array(arrayNum);
				printf("-array start-\n");
				break;
			case '{':
				inmapNum = 0;
				inmapNum = EncoderCount(inmap, originalFile, i, NULL);
				encoder.write_map(inmapNum);
				mapArray[mapArrayNum] = inmapNum;
				mapArrayNum++;

				printf("(inmapNum : %d)\n", inmapNum);
				printf("-map start-\n");
				break;
			case '}':
				printf("-map end-\n");
				break;
			case ']':
				printf("-array end-\n");
				break;
			case '\"':
				stringdist = sstart;
				printf("-string start-\n");
				break;
			case '\'':
				stringdist = bstart;
				printf("-bytes start-\n");
				break;
			case ':':
				stringdist = etc;
				printf("-value start-\n");
				break;
			case ',':
				printf("-and-\n");
				break;
			case ' ':
				break;
			default:
				if (originalFile[i] == 'h' && originalFile[i + 1] == '\'')	//bytes
				{
					break;
				}
				if (originalFile[i + 1] == ':')
				{	
					do
					{
						Encoding[a] = originalFile[i];
						a++;
						i++;
					} while (originalFile[i] != '\"' && originalFile[i] != ',' && originalFile[i] != '}' && originalFile[i] != ']' && originalFile[i] != ':' && originalFile[i] != ' ' && originalFile[i] != '\'');
					
					encoder.write_int(atoi(Encoding));
					memset(Encoding, 0x00, sizeof(Encoding));
					a = 0;
					break;
				}
				if (originalFile[i] != ' ')
				{
					do
					{
						Encoding[a] = originalFile[i];
						a++;
						i++;
					} while (originalFile[i] != '\"' && originalFile[i] != ',' && originalFile[i] != '}' && originalFile[i] != ']' && originalFile[i] != '\'');

					

					if (originalFile[i] == '\"' && stringdist == sstart)	//string
					{
						stringdist = send;
						printf("-string end-\n");
					}
					else if (originalFile[i] == '\'' && stringdist == bstart)	//bytes
					{
						stringdist = bend;
						printf("-bytes end-\n");
					}
					
					if (stringdist == send)			//string
					{
						encoder.write_string(Encoding);
						printf("string: [%s]\n", Encoding);
						memset(Encoding, 0x00, sizeof(Encoding));
						a = 0;
					}
					else if(stringdist == bend)		//bytes
					{
						encoder.write_bytes((const unsigned char*)Encoding, strlen(Encoding));
						printf("bytes: [%s]\n", Encoding);
						memset(Encoding, 0x00, sizeof(Encoding));
						a = 0;
					}
					else if (stringdist == etc)
					{
						if (strcmp(Encoding, "TRUE") == 0 || strcmp(Encoding, "true") == 0)	//bool true
						{
							encoder.write_bool(true);
							printf("bool: [TRUE]\n");
							memset(Encoding, 0x00, sizeof(Encoding));
							a = 0;
						}
						else if (strcmp(Encoding, "FALSE") == 0 || strcmp(Encoding, "false") == 0)	//bool false
						{
							encoder.write_bool(false);
							printf("bool: [FALSE]\n");
							memset(Encoding, 0x00, sizeof(Encoding));
							a = 0;
						}
						else                                 //int
						{
							encoder.write_int(atoi(Encoding));
							printf("int: [%s]\n", Encoding);
							memset(Encoding, 0x00, sizeof(Encoding));
							a = 0;
						}
					}
				}
				break;
		}
	}

	FILE* test = fopen("encoding_test.bin", "wb");
	fwrite(output.data(), output.size(), 1, test);
	fclose(test);

	fwBinaryBuffer = output.data();
	fwBinaryBufferSize = output.size();

	/* HIDWrite Start */
	ret = HIDWrite(fwBinaryBuffer, fwBinaryBufferSize, HID_CMD_UAF);
	if (ret < 0)
	{
		printf("HIDWrite Fail\n");
		printf("fwBinaryBufferSize: %d", fwBinaryBufferSize);
		return -1;
	}
	else
	{
		printf("HIDWrite Success\n");
	}

	//memset(buffer, 0x00, sizeof(buffer));
	//
	///* HIDRead Start */
	//ret = HIDRead(buffer, sizeof(buffer), HID_CMD_UAF);

	//time_t timer;
	//struct tm *t;
	//timer = time(NULL);
	//t = localtime(&timer);


	//sprintf(fpName, "./FIDO2.0_Test/%04d_%02d_%02d_%02d_%02d_%02d.bin", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
	//if (_access("./FIDO2.0_Test", 0) == -1)
	//{
	//	CreateDirectory("./FIDO2.0_Test", NULL);
	//	FILE* bin = fopen(fpName, "wb");
	//	if (fwBinaryBuffer != NULL)
	//	{
	//		delete[] fwBinaryBuffer;
	//		fwBinaryBuffer = NULL;
	//	}

	//	fclose(bin);
	//}
	//FILE* bin = fopen(fpName, "wb");

	//fwrite(buffer, sizeof(buffer), 1, bin);

	//if (fwBinaryBuffer != NULL)
	//{
	//	delete[] fwBinaryBuffer;
	//	fwBinaryBuffer = NULL;
	//}
	//
	//fclose(bin);

	//if (ret < 0)
	//{
	//	printf("HIDRead Fail\n");
	//	return -1;
	//}

	//ret = CborDecoding(fpName);
	//if (ret != 0)
	//{
	//	printf("CborDecoding Fail");
	//}

	return 0;
}

void WindowsProject2::MiddleEncoder(char* PathBuffer)
{
	int ret = 0;

	unsigned char* fwBinaryBuffer = NULL;
	char binaryFile[BUFFERSIZE]{ 0, };
	unsigned int   fwBinaryBufferSize = 0;

	ret = FileRead(PathBuffer, &fwBinaryBuffer, &fwBinaryBufferSize);
	if (ret < 0)
	{
		printf("FileRead error");
	}
	if (fwBinaryBufferSize <= 0)
	{
		printf("invalid fwBinaryBufferSize");
	}
	if (fwBinaryBuffer == NULL)
	{
		printf("fwBinaryBuffer is NULL");
	}

	memcpy(binaryFile, fwBinaryBuffer, fwBinaryBufferSize);

	mapNum = EncoderCount(normal, binaryFile, NULL, '{');
	ret = EncoderinStruct(binaryFile);
}

/* binFile request&response */
int WindowsProject2::FileSend(char* PathBuffer)
{
	HWND ghwnd;
	int ret = 0;
	char fpName[64] = { 0, };

	unsigned char* fwBinaryBuffer = NULL;
	char binaryFile[BUFFERSIZE]{ 0, };
	unsigned int   fwBinaryBufferSize = 0;
	unsigned char  buffer[BUFFERSIZE] = { 0, };

	TLV_t* response = (TLV_t*)buffer;
	

	ret = FileRead(PathBuffer, &fwBinaryBuffer, &fwBinaryBufferSize);
	if (ret < 0)
	{
		printf("FileRead error");
		return -1;
	}
	if (fwBinaryBufferSize <= 0)
	{
		printf("invalid fwBinaryBufferSize");
		return -1;
	}
	if (fwBinaryBuffer == NULL)
	{
		printf("fwBinaryBuffer is NULL");
		return -1;
	}
	
	memcpy(binaryFile, fwBinaryBuffer, fwBinaryBufferSize);

	mapNum = EncoderCount(normal, binaryFile, NULL, '{');
	ret = EncoderinStruct(binaryFile);

	//MiddleEncoder(PathBuffer);
	DBG_Log("\narrayNum: %d\nmapNum: %d", arrayNum, mapNum);

	/* CBOR Encoding */
	/*ret = CborEncoding(fwBinaryBuffer, fwBinaryBufferSize);
	if (ret != 0)
	{
		DBG_Log("CBOR Encoding Fail");
		return -1;
	}
	else
	{
		DBG_Log("CBOR Encoding Success");
	}*/

	///* HIDWrite Start */
	//ret = HIDWrite(fwBinaryBuffer, fwBinaryBufferSize, HID_CMD_UAF);
	//if (ret < 0)
	//{
	//	printf("HIDWrite Fail\n");
	//	printf("fwBinaryBufferSize: %d", fwBinaryBufferSize);
	//	return -1;
	//}

	//memset(buffer, 0x00, sizeof(buffer));
	//
	///* HIDRead Start */
	//ret = HIDRead(buffer, sizeof(buffer), HID_CMD_UAF);

	//time_t timer;
	//struct tm *t;
	//timer = time(NULL);
	//t = localtime(&timer);


	//sprintf(fpName, "./FIDO2.0_Test/%04d_%02d_%02d_%02d_%02d_%02d.bin", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
	//if (_access("./FIDO2.0_Test", 0) == -1)
	//{
	//	CreateDirectory("./FIDO2.0_Test", NULL);
	//	FILE* bin = fopen(fpName, "wb");
	//	if (fwBinaryBuffer != NULL)
	//	{
	//		delete[] fwBinaryBuffer;
	//		fwBinaryBuffer = NULL;
	//	}

	//	fclose(bin);
	//}
	//FILE* bin = fopen(fpName, "wb");

	//fwrite(buffer, sizeof(buffer), 1, bin);

	//if (fwBinaryBuffer != NULL)
	//{
	//	delete[] fwBinaryBuffer;
	//	fwBinaryBuffer = NULL;
	//}
	//
	//fclose(bin);

	//if (ret < 0)
	//{
	//	printf("HIDRead Fail\n");
	//	return -1;
	//}
		
	return 0;
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
	hInst = hInstance;
	InitCommonControls();
	return DialogBox(hInst, MAKEINTRESOURCE(ID_DIALOG_MAIN), NULL, (DLGPROC)WndProc);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	/*AllocConsole();
	freopen("CONOUT$", "wt", stdout);*/
	int ret = FALSE;
    switch (message)
    {
	case WM_DEVICECHANGE:
	{
		switch (wParam)
		{
		case DBT_DEVICEARRIVAL:
		{
			ret = windowsProject2->Init();
			DBG_Log("FIDO Plug");
			MessageBox(gHwnd, "연결되었습니다.", "", MB_OK);
			DBG_Log("devicepath: %s", cur_dev->path);
		}
		break;

		case DBT_DEVICEREMOVECOMPLETE:
		{
			DBG_Log("FIDO UnPlug");
			MessageBox(gHwnd, "연결이 해제되었습니다.", "", MB_OK);
		}
		break;

		default:
		{
		}
		break;
		}
	}
	break;

	case WM_INITDIALOG:
		{
			DBG_Log("UI init");

			ret = windowsProject2->Init();

			if (ret != 0)
			{
				DBG_Log("Init error");
				exit(1);
			}
			/* init icon*/
			HICON icon;
			icon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_ICON));
			if (icon != NULL)
			{
				SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)icon);
				SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);
			}
		}
		return TRUE;

	case WM_CLOSE:
		{
			windowsProject2->Uninit();
			PostQuitMessage(0);
		}
		return TRUE;

    case WM_COMMAND:
        {
            // 메뉴 선택을 구문 분석합니다.
            switch (LOWORD(wParam))
            {
			case IDC_BUTTON_SEARCH:
			{
				
			}
			break;

			case IDC_BUTTON_DECODE:
			{
				gHwnd = hWnd;
				//ret = asmInit();

				ZeroMemory(&ofn, sizeof(OPENFILENAME));

				ofn.lStructSize = sizeof(OPENFILENAME);
				ofn.hwndOwner = NULL;
				ofn.lpstrFile = filePathBuffer;
				ofn.nMaxFile = MAX_PATH;
				ofn.lpstrFilter = "Binary Files(.bin)\0*.BIN\0All Files(*.*)\0*.*\0";
				ofn.nFilterIndex = 1;
				ofn.lpstrTitle = "파일을 선택하세요.";
				ofn.Flags = OFN_HIDEREADONLY | OFN_EXPLORER | OFN_OVERWRITEPROMPT;

				char currentPath[MAX_PATH] = { 0, };
				GetCurrentDirectory(MAX_PATH, currentPath);

				ret = GetOpenFileName(&ofn);
				if (ret == (int)true)
				{
					SetWindowText(GetDlgItem(hWnd, IDC_EDIT_FILEPATH), filePathBuffer);
				}

				SetWindowText(GetDlgItem(hWnd, IDC_EDIT_FILEPATH), filePathBuffer);
				ret = windowsProject2->FileDecoding(filePathBuffer);
				if (ret == 0)
				{
					MessageBox(hWnd, "파일이 생성되었습니다.", "", MB_OK);
				}
				else
				{
					MessageBox(hWnd, "파일이 생성에 실패하였습니다.", "", MB_OK);
				}
			}
			break;

			case IDC_BUTTON_START:
			{
				gHwnd = hWnd;
				ZeroMemory(&ofn, sizeof(OPENFILENAME));

				ofn.lStructSize = sizeof(OPENFILENAME);
				ofn.hwndOwner = NULL;
				ofn.lpstrFile = filePathBuffer;
				ofn.nMaxFile = MAX_PATH;
				ofn.lpstrFilter = "All Files(*.*)\0*.*\0";
				ofn.nFilterIndex = 1;
				ofn.lpstrTitle = "파일을 선택하세요.";
				ofn.Flags = OFN_HIDEREADONLY | OFN_EXPLORER | OFN_OVERWRITEPROMPT;

				char currentPath[MAX_PATH] = { 0, };
				GetCurrentDirectory(MAX_PATH, currentPath);

				ret = GetOpenFileName(&ofn);
				if (ret == (int)true)
				{
					SetWindowText(GetDlgItem(hWnd, IDC_EDIT_FILEPATH), filePathBuffer);
				}

				SetWindowText(GetDlgItem(hWnd, IDC_EDIT_FILEPATH), filePathBuffer);
				ret = windowsProject2->FileSend(filePathBuffer);
				if (ret == 0)
				{
					MessageBox(hWnd, "파일이 생성되었습니다.", "", MB_OK);
				}
				else
				{
					MessageBox(hWnd, "파일이 생성에 실패하였습니다.", "", MB_OK);
				}
			}
			break;
            }
        }
        break;
	}

	return 0;
}
