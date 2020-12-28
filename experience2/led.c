
#include "vxWorks.h"
#include "stdio.h"
#include "stdlib.h"
#include "semLib.h"
#include "taskLib.h"
#include "sysLib.h"
#include "tickLib.h"
#include "led.h"

/* ----------------------------------------- */
void led_init(void)
{
    rGPBCON	= (rGPBCON | (0xf<<5));
    rGPBUP 	= (rGPBUP & ~(0xf<<5));
    rGPBDAT 	= (rGPBDAT | (0xf<<5));
}

/* ----------------------------------------- */
void led_display(int data)
{
    /*taskDelay(sysClkRateGet() / 2);
    printf("As if the light flashed\n");*/
printf("%d led is on \n", data);
    rGPBDAT = (rGPBDAT & ~(0xf<<5)) | ((~data & 0xf)<<5);
}
