#include "vxWorks.h"
#include "stdio.h"
#include "stdlib.h"
#include "semLib.h"
#include "taskLib.h"
#include "sysLib.h"
#include "tickLib.h"
#include "wdLib.h"

#define SOH 0x01
#define EOT 0x04
#define ACK 0x06
#define NAK 0x15
#define CAN 0x18
#define MAX_SIZE 1500  /*传输数据大小(大于1K)*/
#define DATA_SIZE 128  /*数据块大小(每个包含数据的大小)*/
#define MAX_ERRORS 3   /*丢包重传次数*/
#define STACK_SIZE  20000 /* 分配给每个任务的堆栈大小 */

typedef struct data {  /*存储待发送数据*/
    int len;  /*数据长度*/
    int cur;  /*当前数组指针*/
    char str[MAX_SIZE];  /*剩余数据*/
} DataNode;

char buffer[133];  /*数据缓冲区*/
char response;     /*存放应答帧*/
SEM_ID bufferSemId;    /* 同步信号量，发送完成时释放 */
SEM_ID responseSemId;  /* 同步信号量，应答帧到来时释放 */
WDOG_ID  timerDog;   /*看门狗定时器*/
int send_taskId;     /*发送数据TaskId*/
int receive_taskId;  /*接受数据TaskId*/
static const unsigned short crc16tab[256]= {   /*crc16tab  查表法计算crc校验码*/
        0x0000,0x1021,0x2042,0x3063,0x4084,0x50a5,0x60c6,0x70e7,
        0x8108,0x9129,0xa14a,0xb16b,0xc18c,0xd1ad,0xe1ce,0xf1ef,
        0x1231,0x0210,0x3273,0x2252,0x52b5,0x4294,0x72f7,0x62d6,
        0x9339,0x8318,0xb37b,0xa35a,0xd3bd,0xc39c,0xf3ff,0xe3de,
        0x2462,0x3443,0x0420,0x1401,0x64e6,0x74c7,0x44a4,0x5485,
        0xa56a,0xb54b,0x8528,0x9509,0xe5ee,0xf5cf,0xc5ac,0xd58d,
        0x3653,0x2672,0x1611,0x0630,0x76d7,0x66f6,0x5695,0x46b4,
        0xb75b,0xa77a,0x9719,0x8738,0xf7df,0xe7fe,0xd79d,0xc7bc,
        0x48c4,0x58e5,0x6886,0x78a7,0x0840,0x1861,0x2802,0x3823,
        0xc9cc,0xd9ed,0xe98e,0xf9af,0x8948,0x9969,0xa90a,0xb92b,
        0x5af5,0x4ad4,0x7ab7,0x6a96,0x1a71,0x0a50,0x3a33,0x2a12,
        0xdbfd,0xcbdc,0xfbbf,0xeb9e,0x9b79,0x8b58,0xbb3b,0xab1a,
        0x6ca6,0x7c87,0x4ce4,0x5cc5,0x2c22,0x3c03,0x0c60,0x1c41,
        0xedae,0xfd8f,0xcdec,0xddcd,0xad2a,0xbd0b,0x8d68,0x9d49,
        0x7e97,0x6eb6,0x5ed5,0x4ef4,0x3e13,0x2e32,0x1e51,0x0e70,
        0xff9f,0xefbe,0xdfdd,0xcffc,0xbf1b,0xaf3a,0x9f59,0x8f78,
        0x9188,0x81a9,0xb1ca,0xa1eb,0xd10c,0xc12d,0xf14e,0xe16f,
        0x1080,0x00a1,0x30c2,0x20e3,0x5004,0x4025,0x7046,0x6067,
        0x83b9,0x9398,0xa3fb,0xb3da,0xc33d,0xd31c,0xe37f,0xf35e,
        0x02b1,0x1290,0x22f3,0x32d2,0x4235,0x5214,0x6277,0x7256,
        0xb5ea,0xa5cb,0x95a8,0x8589,0xf56e,0xe54f,0xd52c,0xc50d,
        0x34e2,0x24c3,0x14a0,0x0481,0x7466,0x6447,0x5424,0x4405,
        0xa7db,0xb7fa,0x8799,0x97b8,0xe75f,0xf77e,0xc71d,0xd73c,
        0x26d3,0x36f2,0x0691,0x16b0,0x6657,0x7676,0x4615,0x5634,
        0xd94c,0xc96d,0xf90e,0xe92f,0x99c8,0x89e9,0xb98a,0xa9ab,
        0x5844,0x4865,0x7806,0x6827,0x18c0,0x08e1,0x3882,0x28a3,
        0xcb7d,0xdb5c,0xeb3f,0xfb1e,0x8bf9,0x9bd8,0xabbb,0xbb9a,
        0x4a75,0x5a54,0x6a37,0x7a16,0x0af1,0x1ad0,0x2ab3,0x3a92,
        0xfd2e,0xed0f,0xdd6c,0xcd4d,0xbdaa,0xad8b,0x9de8,0x8dc9,
        0x7c26,0x6c07,0x5c64,0x4c45,0x3ca2,0x2c83,0x1ce0,0x0cc1,
        0xef1f,0xff3e,0xcf5d,0xdf7c,0xaf9b,0xbfba,0x8fd9,0x9ff8,
        0x6e17,0x7e36,0x4e55,0x5e74,0x2e93,0x3eb2,0x0ed1,0x1ef0
};


/*函数声明*/
void start();  /*系统开始入口函数*/
void stop();   /*系统退出入口函数*/
void timeout_handler();  /*定时器超时处理函数*/
void send();   /*send task 逻辑函数*/
void receive();  /*receive task 逻辑函数*/
unsigned short cal_crc(const char*, int);  /*计算crc校验和*/


void send (){
    DataNode* pData = (DataNode*)malloc(sizeof(DataNode));    /*定义测试数据*/
    int i;
    int errorCount;  /*丢包次数*/
    unsigned char packageNumber = 0x01;  /*包序号*/
    unsigned int checkSum;  /*校验和*/
    unsigned char high;  /*校验和高位*/
    unsigned char low;  /*校验和低位*/
    char data[DATA_SIZE];   /*一次待发送的数据*/
    unsigned char len;  /*当前包中数据长度*/

    for (i = 0; i < MAX_SIZE -1; ++i) {
        pData->str[i] = 'a' + rand() % 26;  /*随机写入英文小写字母*/
    }
    pData->len = MAX_SIZE - 1;
    pData->cur = 0;

    printf("\nsend task: data size is %d", pData->len);

    while (pData->cur < pData->len) {  /*还有数据待发送*/
        len = 0x00;
        for (i = 0; i < 128; ++i) {  /*读取128字节数据*/
            if (pData->cur < pData->len){
                data[i] = pData->str[pData->cur];
                pData->cur ++;
                len ++;
            } else{   /*数据不够，记录长度*/
                break;
            }
        }

        errorCount = 0;
        while (errorCount < MAX_ERRORS) {  /*当前包发送次数要不超过最大允许丢包次数*/
            /*组包  控制字符 + 包序号 + 包序号的反码 + 数据 + 校验和*/
            buffer[0] = SOH;  /*数据写入buffer*/
            buffer[1] = packageNumber;
            buffer[2] = len;
            for (i = 0; i < len; ++i) {
                buffer[i+3] = data[i];
            }
            checkSum = cal_crc(data, len) & 0xffff;  /*校验和*/
            high = (checkSum >> 8) & 0xff;
            low = checkSum & 0xff;
            buffer[len +3] = high;  /*校验和 前一个字节*/
            buffer[len +4] = low;  /*校验和 后一个字节*/
            printf("\nsend task: a package send");
            response = 0x00;  /*置空*/
            /*开始定时器*/
            wdStart(timerDog, 1000,
                    (FUNCPTR)timeout_handler, 0);
            semGive(bufferSemId);

            /*等待应答数据*/
            semTake(responseSemId, WAIT_FOREVER);
            wdCancel(timerDog);  /*关闭定时器*/
            /*如果收到应答数据则跳出循环，发送下一包数据*/
            /*未收到应答，错误包数+1，继续重发*/
            if (response == ACK) {
                printf("\nsend task: receive a ack");
                break;  /*发送完成*/
            } else {
                ++errorCount;
            }
        }
        if (errorCount >= MAX_ERRORS){  /*连续MAX_ERRORS次没收到ACK应答，进入结束阶段*/
            printf("\nsend task: no ack receive in max count");
            buffer[0] = EOT;
            semGive(bufferSemId);

            while(1) {
                /*等待应答数据*/
                wdStart(timerDog, 1000,
                        (FUNCPTR)timeout_handler, 0);
                semTake(responseSemId, WAIT_FOREVER);
                wdCancel(timerDog);  /*关闭定时器*/
                break;
            }
        }
        /*包序号自增*/
        packageNumber ++;
    }

    if (pData->cur >= pData->len){
        /*数据传输完成，进入结束阶段*/
        buffer[0] = EOT;
        semGive(bufferSemId);
        printf("\nsend task: all data is successfully send");

        while(1) {
            wdStart(timerDog, 1000,
                    (FUNCPTR)timeout_handler, 0);
            semTake(responseSemId, WAIT_FOREVER);
            wdCancel(timerDog);  /*关闭定时器*/
            break;
        }
    }
}

void receive(){
    int errorCount = 0;   /*错误包数*/
    char curNumber = 0x01;   /*当前包序号*/
    unsigned int checkSum;     /*校验和*/
    char data[MAX_SIZE];  /*存放所有接受到的数据*/
    int index = 0; /*data[]下标索引*/
    char dataBuffer[DATA_SIZE];  /*存放每个包中的数据*/
    int i;
    char packageNumber;
    int crc;
    unsigned char len;  /*包中含数据长度*/

    while(1) {
        /*如果出错发送重传标识*/
        if (errorCount != 0) {
            response = NAK;
            semGive(responseSemId);
        }
        /*等待数据*/
        semTake(bufferSemId, WAIT_FOREVER);
        if (buffer[0] != EOT) {
            /*判断接收到的是否是开始标识*/
            if (buffer[0] != SOH) {
                printf("\nnot soh");
                errorCount++;
                continue;
            }

            /*验证包序号*/
            packageNumber = buffer[1];
            /*判断包序号是否正确*/
            if (curNumber != packageNumber) {
                printf("\nreceive task: packageNumber is wrong");
                errorCount++;
                continue;
            }

            /*读取数据长度*/
            len = buffer[2];
            /*printf("\nreceive task: len is %d", len);*/

            /*读取数据*/
            for (i = 0; i < len; i++) {
                dataBuffer[i] = buffer[i+3];
            }

            /*验证校验和*/
            checkSum = ((unsigned char)buffer[len +3] << 8) + (unsigned char)buffer[len +4];
            crc = cal_crc(dataBuffer, len);
            if (crc != checkSum) {
                printf("\nreceive task: checkSum is wrong");
                errorCount++;
                continue;
            }

            /*写入数据*/
            for (i = 0; i < len; ++i) {
                data[index ++] = dataBuffer[i];
            }
            /*printf("\nreceive: data written");*/

            /*发送应答*/
            response = ACK;
            semGive(responseSemId);
            /*包序号自增*/
            curNumber++;
            /*错误包数归零*/
            errorCount = 0;
        } else {
            data[index] = '\0';
            printf("\nreceive task: size of data received is %d", index);
            printf("\nreceive task: data received is %s", data);
            break;
        }
    }

}

void timeout_handler(){
    semGive(responseSemId);  /*唤醒发送task,task将进一步判断是发送超时还是收到应答*/
}

void start(){

    /*初始化卡门狗定时器*/
    timerDog = wdCreate();

    /*初始化信号量*/
    bufferSemId = semBCreate(SEM_Q_FIFO, SEM_EMPTY);
    responseSemId = semBCreate(SEM_Q_FIFO, SEM_EMPTY);

    /* 创建任务task */
    send_taskId = taskSpawn("send", 220, 0, STACK_SIZE,
                              (FUNCPTR) send, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    receive_taskId = taskSpawn("receive", 220, 0, STACK_SIZE,
                            (FUNCPTR) receive, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

}

void stop(){
    /*删除Task*/
    taskDelete(send_taskId);
    taskDelete(receive_taskId);

    /*删除定时器*/
    wdDelete(timerDog);

    /*删除信号量*/
    semDelete(bufferSemId);
    semDelete(responseSemId);
}

/*计算crc校验和*/
unsigned short cal_crc(const char *buf, int len)
{
    register int counter;
    register unsigned short crc = 0;
    for( counter = 0; counter < len; counter++)
        crc = (crc<<8) ^ crc16tab[((crc>>8) ^ *(char *)buf++)&0x00FF];
    return crc;
}
