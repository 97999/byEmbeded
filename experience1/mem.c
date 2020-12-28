//
// Created by Hu Zhensha on 2020/11/21.
// Email: 1292776129@qq.com .
//

#include "vxWorks.h"
#include "stdio.h"
#include "taskLib.h"
#include "string.h"
#include "semLib.h"
#include "sysLib.h"
#include "stdlib.h"
#include "memPartLib.h"
#define STACK_SIZE 1024

/*定义结构体  pool block*/

/*对应每一个内存块*/
typedef struct _block
{
    int poolId;  /*标识每个内存块. 1代表容量16B, 2代表容量256B*/
    struct _block* next;
}block;

/*对应每一个内存池*/
typedef struct _pool
{
    int blockSize;  /*内存池内含有的内存块的数量*/
    struct _pool* next;  /*指向下一个内存池，如16B内存池指向256内存池*/
    block* first;  /*第一个内存块*/
}pool;

/*用链表存储由系统分配的块组成的 sysMemHead为头指针*/
block sysMalloc;  /*用于初始化链表头指针*/
block* sysMemHead = &sysMalloc;  


/*全局变量*/
SEM_ID semMalloc;  /*内存申请信号量*/
SEM_ID semFree;  /*内存释放信号量*/
pool* initPtr;

int taskId1;
int taskId2;

/*函数声明*/
pool* initPool(void);
void* memMalloc(int size);
void memFree(void* dataPtr);
void memDel(void);
void task1(void);
void task2(void);
void start(void);
void stop(void);


/*initPool() 初始化内存池*/
pool* initPool(void)
{
    int i;
    pool* mem;
    pool* poolPtr;
    block* blockPtr;
    mem=(pool*)malloc(4000);  /*分配 4000 内存作为内存池*/
    /*初始化 pool*/
    poolPtr = (pool*)mem;
    /*初始化 pool 1 该内存池分配大小为 16B 的内存*/
    poolPtr->blockSize = 20;  /*初始化可用块数 20*/
    /*块大小 16B*/
    blockPtr = (block*)((char*)poolPtr+sizeof(pool)); /*除去poolHead开销,blockHeadPtr指向块的首地址*/
    poolPtr->first = blockPtr;  /*第一个可用块的地址*/
    poolPtr->next= (pool*)((char*)poolPtr + sizeof(poolPtr) + 20*(sizeof(block)+16)); /*next指向下一个内存池*/
    blockPtr->poolId =1;
    blockPtr->next = 0;
    for(i=1;i<20;i++) { /*初始化 10 个容量 16B 的内存块. 使用头插法建立链表.*/
        /*先初始化的块排在后面，最后初始化的块为第一个可用块*/
        blockPtr=(block*)((char*)blockPtr + (sizeof(block)+16)); /*块的首址移动 16 加结构体的开销长度,移到下一个块*/
        blockPtr->poolId = 1; /*pool号1，表示 16B 容量*/
        blockPtr->next = poolPtr->first; /*当前首个可用块地址赋给 next*/
        poolPtr->first = blockPtr;  /*下一个可用块地址*/
    }
    /*初始化 pool 2 该内存池分配大小为 256B 的内存*/
    poolPtr = poolPtr->next;
    poolPtr->blockSize = 10;  /*初始化可用块数 10*/
    /*块大小 256*/
    for(i=1;i<10;i++)   /*初始化 10 个容量 256B 的内存块*/
    {
        blockPtr=(block*)((char*)blockPtr + (sizeof(block)+256));
        blockPtr->poolId = 2;  /*pool号2，表示 256B 容量*/
        blockPtr->next = poolPtr->first;
        poolPtr->first = blockPtr;
    }
    return (pool*)mem;
}

/*memMalloc() 分配内存*/
void* memMalloc(int size)
{
    void* mem;
    pool* poolPtr = initPtr;
    block* blockPtr;
    semTake(semMalloc, WAIT_FOREVER);
    if((size <= 16)&&(poolPtr->blockSize != 0)) {  /*长度小于 16 时，分配长度为 16的内存空间*/
        blockPtr = poolPtr->first; /*首个可用块地址赋给分配块的首地址*/
        poolPtr->first = blockPtr->next; /*改变下一第一可用块的地址*/
        poolPtr->blockSize --;  /*可用块数减一*/
    }
    else if((size <= 256)&&((poolPtr->next)->blockSize != 0)){ /*长度大于 16 小于 256 时，分配长度为 256 的内存空间*/
        blockPtr = (poolPtr->next)->first;
        (poolPtr->next)->first = blockPtr->next;
        (poolPtr->next)->blockSize --;
    } else { /*超过最大块大小由系统的内存分配函数 malloc 分配*/
        mem = malloc(size);
        blockPtr = (block*)mem;
        blockPtr->poolId = 3;  /*块标记为3*/
        blockPtr->next = sysMemHead->next;  /*使用头插法将该块插入到系统分配块头指针sysMemHead之后*/
        sysMemHead->next = blockPtr;
    }
    semGive(semMalloc);
    return (void*)((char*)blockPtr + sizeof(block));  /*除去块头开销*/
}

/*memFree() 释放内存空间*/
void memFree(void* dataPtr){
    char* mem= (char*) dataPtr;
    pool* poolPtr = initPtr;  /*取得内存池起始地址*/
    block* blockPtr;  /*暂存待释放的内存块*/
    semTake(semFree,WAIT_FOREVER);
    blockPtr = (block*)((char*)mem - sizeof(block)); /*取得该内存的控制块地址*/
    if(blockPtr->poolId == 1)
    {
        blockPtr->next = poolPtr->first; /*将该内存块插在第一个可用块之前*/
        poolPtr->first = blockPtr;  /*重新定义第一个可用块地址*/
        poolPtr->blockSize++;   /*可用块数数量加一*/
    } else if(blockPtr->poolId == 2)
    {
        blockPtr->next = (poolPtr->next)->first;
        (poolPtr->next)->first = blockPtr;
        (poolPtr->next)->blockSize ++;
    } else   /*释放由系统分配的内存块*/
    {
        /*在sysMemHead链表上找到该内存块结点,并删除该结点，最后释放*/
        block* temp = sysMemHead;
        while(temp->next != 0){
            if( temp->next == blockPtr){  /*若temp为待删除块的前置结点*/
                temp->next = blockPtr->next;  /*删除结点*/
                free((char*)mem - sizeof(block));
                break;
            }
            temp = temp->next;
        }
    }
    semGive(semFree);
}

/*memDel()删除两个内存池和由系统分配的块*/
void memDel(void)
{
    void* mem;
    mem = (void*)(initPtr);
    free(mem);
    while (sysMemHead->next !=0){  /*删除mallocPtr上的所有内存块*/
        mem = (void*)(sysMemHead);
        sysMemHead=sysMemHead->next ;
        free(mem);
    }
    semDelete(semMalloc);  /*删除信号量*/
    semDelete(semFree);
}

/*任务一，负责信号量的创建*/
void task1(void)
{
    semMalloc= semBCreate(SEM_Q_PRIORITY,SEM_FULL);
    semFree = semBCreate(SEM_Q_PRIORITY,SEM_FULL);
}

/*任务二，测试程序*/
void task2(void){
    int i;
    char* add[10];
    int size[10] = {33, 888, 54, 245 ,11, 5, 4, 589, 13, 254};
    pool* poolPtr;
    sysMemHead->next = 0;
    initPtr = initPool();   /*第一个内存池地址*/
    for(i=0; i<10; i++){
        add[i] = (char*)memMalloc(size[i]);  /*分配内存，检查内存块分配情况*/
        poolPtr = initPtr;
        /*输出测试结果*/
        printf("task malloc size:%d\n",size[i]);
        if (size[i]<=16)
            printf("The Address is:%08x (use size is:16+8)\n",add[i]);
        else if (size[i]<=256)
            printf("The Address is:%08x (use size is:256+8)\n",add[i]);
        else
            printf("System memory malloc \n");
        printf("blockSize in pool 1 is : %d\n", poolPtr->blockSize);
        printf("blockSize in pool 2 is : %d\n\n",poolPtr->next->blockSize);
    }
    for(i=0; i<10; i++){
        memFree((void*)add[i]); /*释放内存，检查内存块分配情况*/
        poolPtr = initPtr;
        /*输出测试结果*/
        printf(" free size:%d\n",size[i]);
        if (size[i]<=16)
            printf("The Address is:%08x (free size is:16+8)\n",add[i]);
        else if (size[i]<=256)
            printf("The Address is:%08x (free size is:256+8)\n",add[i]);
        else
            printf("system memory free \n\n");
        printf("blockSize in pool 1 is : %d\n", poolPtr->blockSize);
        printf("blockSize in pool 2 is : %d\n\n",poolPtr->next->blockSize);
    }
    memDel();
    return;
}


/*测试程序*/
void start(void)
{
    taskId1=taskSpawn("tTask1",200,0,STACK_SIZE,(FUNCPTR)task1,0,0,0,0,0,0,0,0,0,0);
    taskId2=taskSpawn("tTask2",220,0,STACK_SIZE,(FUNCPTR)task2,0,0,0,0,0,0,0,0,0,0);
    return;
}

void stop(void){
    taskDelete(taskId1);
    taskDelete(taskId2);
}