
#ifndef TIMER_LED_H
#define TIMER_LED_H


/* GPIO初始化 */
#define rGPBCON    (*(volatile unsigned *)\
          0x56000010) /* Port B control */
#define rGPBDAT    (*(volatile unsigned *)\
          0x56000014) /* Port B data */
#define rGPBUP     (*(volatile unsigned *)\
          0x56000018) /* Pull-up control B */

/* 函数声明*/
void led_init(void);
void led_display(int);

#endif //TIMER_LED_H
