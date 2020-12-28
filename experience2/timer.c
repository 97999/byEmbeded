
#include "vxWorks.h"
#include "stdio.h"
#include "stdlib.h"
#include "semLib.h"
#include "taskLib.h"
#include "sysLib.h"
#include "tickLib.h"
#include "led.h"

/* 宏定义 */
#define NUM_SAMPLE  10    /*组成一个样本的数据的个数*/
#define SYSCLK_TICK 100   /* 系统时钟每秒的tick数 */
#define NO_ARG      0     /* 传递给函数的参数 */
#define STACK_SIZE  20000 /* 分配给每个任务的堆栈大小 */

/* 类型定义 */
typedef struct data
{
    int data;
    struct data *preNode;
}LIST_NODE;

/* 全局变量 */
int gather_taskId;                 /* task_ID */
int deal_taskId;
int monitor_taskId;
int randData  = 0;           /* 存储产生的随机数 */
int count = 0;       /*统计样本数量*/
int result      = 0;           /* 样本处理结果 */
int sysClkOriginalRate = 60;   /* 系统时钟的原始ISR */
LIST_NODE * pCurrNode  = NULL; /* 数据列表的头指针 */
SEM_ID dataSemId;              /* 同步信号量，数据到达时释放 */
SEM_ID sampleSemId;              /* 同步信号量，样本到达时释放 */
SEM_ID listSemId;     /* 互斥信号量，保护数据列表 */

/* 函数申明 */
void timer(void);  /*时钟中断服务程序*/
void gather(void);  /*采集数据*/
void deal(void);  /*数据处理函数，求样本平均值，输出并送led显示*/
void monitor(void);  /*监测程序运行情况*/
void nodeAdd(int data);
void nodeDel(void);
extern void usrClock ();




/*******************************************************************
* progStart - 启动实例程序
*
* 负责创建信号量与任务
*
* RETURNS: OK
*/
STATUS start(void){
    sampleSemId = semBCreate(SEM_Q_FIFO, SEM_EMPTY);
    dataSemId = semBCreate(SEM_Q_FIFO, SEM_EMPTY);
    listSemId = semMCreate(SEM_Q_PRIORITY
                           | SEM_INVERSION_SAFE
                           | SEM_DELETE_SAFE);
    /* 初始化变量 */
    pCurrNode           = NULL;

    /* 初始化系统时钟 */
    sysClkOriginalRate  = sysClkRateGet();  /*读取系统默认时钟频率*/
    printf("\n system clock original rate = %d", sysClkOriginalRate);

    if (sysClkRateSet(SYSCLK_TICK) == ERROR){  /*设置新的时钟频率*/
        printf("\n System clock rate setting failure!");
        return(ERROR);
    }
    if (sysClkConnect((FUNCPTR) timer, NO_ARG) == ERROR){  /*替换时钟中断服务程序*/
        printf("\n System clock ISR connecting failure!");
        return(ERROR);
    }
    sysClkEnable();  /*打开系统时钟中断*/

    /* 创建任务task */
    gather_taskId = taskSpawn("gather", 220, 0, STACK_SIZE,
                          (FUNCPTR) gather, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    deal_taskId = taskSpawn("deal", 240, 0, STACK_SIZE,
                          (FUNCPTR) deal, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    monitor_taskId = taskSpawn("monitor", 250, 0, STACK_SIZE,
                           (FUNCPTR) monitor,0,0,0,0,0,0,0,0,0,0);

    return (OK);
}

/******************************************************************
* 系统时钟服务程序，每次释放一次信号量代表数据到来一次
*/
void timer(void){
    tickAnnounce();  /*通知vxworks操作系统，将系统时钟的tick值++以及进行一些其他的操作*/
    randData = rand() % 16;  /*产生一个0-15的随机数*/
    semGive(dataSemId);  /*释放信号量，表示数据到来*/
}



/*******************************************************************
*
* 将采集到的数据，每NUM_SAMPLE个一组，组成一个样本，样本用全局的数据链表存储
* 每组成一个样本之后，释放信号量，唤醒样本处理任务deal
*/
void gather(void){
    int nodeIdx;
    FOREVER{
        for (nodeIdx = 0; nodeIdx < NUM_SAMPLE; nodeIdx++){
            /*等待数据到来 */
            semTake(dataSemId, WAIT_FOREVER);
            /* 保存数据到样本  */
            nodeAdd(randData);
        }
        /* 一个样本已收集完成 */
        semGive(sampleSemId);
    }
}



/******************************************************************
* 处理样本数据
*
* 对收到的样本数据进行处理。
* 计算样本平均值,供给led显示
*/
void deal(void){
    int sampleSum   = 0; /* 保存样本数据之和 */
    int index = 0;

    FOREVER{
        semTake(sampleSemId, WAIT_FOREVER);
        semTake(listSemId, WAIT_FOREVER);
        while (pCurrNode != NULL){
            sampleSum   +=  pCurrNode->data;
            index += 1;
            nodeDel();
        }
        semGive(listSemId);
        if(index != 0){
            result = sampleSum / index;
            /*led_display(result); *//*送led灯显示*/
            count ++;  /*样本数量加一*/
            printf("a group of data gathered,，and the avg is %d, led is on \n", result);
        } else{
            result = 0;
        }
        sampleSum = 0;
        index = 0;

    }
}

/*****************************************************************
* 监视程序运行情况
*
* 以统时钟频率的时间间隔监视程序运行情况
* 并输出结果
*/
void monitor(void){
    FOREVER{
        taskDelay(sysClkRateGet());
        printf("===========================================================\n");
        printf("monitor task: the current is %dth sample \n", count);
        printf("===========================================================\n");
    }
}

/********************************************************************
* 停止程序
*
* 删除创建的任务并释放信号量资源。
*/
void stop(void){
    /* 恢复对系统时钟的默认配置 */
    sysClkDisable();
    sysClkConnect ((FUNCPTR) usrClock, NO_ARG);
    sysClkRateSet (sysClkOriginalRate);
    sysClkEnable ();

    taskDelete(gather_taskId);
    taskDelete(deal_taskId);
    taskDelete(monitor_taskId);

    semDelete(dataSemId);
    semDelete(sampleSemId);
    semDelete(listSemId);

    /* 清空数据链表 */
    while (pCurrNode != NULL){
        nodeDel();
    }
    return;
}

/******************************************************************
*
* 将样本数据存储到数据链表
*
* 申请一个数据节点来保持样本数据，并将节点前插入数据链表中
*
*/
void nodeAdd(int data)/* 需要存入的数据 用于创建新的数据节点 */{
    LIST_NODE * node;
    if ( (node = (LIST_NODE *) malloc(sizeof (LIST_NODE))) != NULL){
        node->data      = data;
        /* 将节点前插入数据链表中 */
        node->preNode = pCurrNode;
        pCurrNode       = node;
    } else {
        printf ("cobble: Out of Memory.\n");
        taskSuspend(0);
    }
    return;
}

/******************************************************************
*
*  从数据链表中删除首个数据节点
*
*/
void nodeDel(void){
    LIST_NODE *pTmpNode; /* 指向首个数据节点 */
    if (pCurrNode != NULL){
        pTmpNode    = pCurrNode;
        pCurrNode   = pCurrNode->preNode;
        free(pTmpNode);
    }
    return;
}