// All necessary declarations for the Tux Controller driver must be in this file

#ifndef TUXCTL_H
#define TUXCTL_H

#define TUX_SET_LED _IOR('E', 0x10, unsigned long)
#define TUX_READ_LED _IOW('E', 0x11, unsigned long*)
#define TUX_BUTTONS _IOW('E', 0x12, unsigned long*)
#define TUX_INIT _IO('E', 0x13)
#define TUX_LED_REQUEST _IO('E', 0x14)
#define TUX_LED_ACK _IO('E', 0x15)
#define LED_MASK 0xFFFF
#define Fourbit_mask 0xF
#define Onebit_mask 0x1
#define LEDoffset 16
#define Decimaloffset 24
#define Left_mask 0x2
#define Down_mask 0x4
#define Leftoffset 1
#define Downoffset 2
#define Right_Up_mask 0x9
#define Coffset 4
#define Six 6
#define Five 5

#endif

