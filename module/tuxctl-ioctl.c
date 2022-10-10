/* tuxctl-ioctl.c
 *
 * Driver (skeleton) for the mp2 tuxcontrollers for ECE391 at UIUC.
 *
 * Mark Murphy 2006
 * Andrew Ofisher 2007
 * Steve Lumetta 12-13 Sep 2009
 * Puskar Naha 2013
 */

#include <asm/current.h>
#include <asm/uaccess.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/miscdevice.h>
#include <linux/kdev_t.h>
#include <linux/tty.h>
#include <linux/spinlock.h>

#include "tuxctl-ld.h"
#include "tuxctl-ioctl.h"
#include "mtcp.h"

#define debug(str, ...) \
	printk(KERN_DEBUG "%s: " str, __FUNCTION__, ## __VA_ARGS__)

// explicit declaration of function used in this file
int tuxctl_ioctl (struct tty_struct* tty, struct file* file, unsigned cmd, unsigned long arg);
int tux_initial (struct tty_struct* tty);
int tux_LED(struct tty_struct* tty,unsigned long arg);
int tux_button (struct tty_struct* tty,unsigned long arg);


//long is 32bit
static unsigned long LED_save;
unsigned long button_value;
/************************ Protocol Implementation *************************/

/* tuxctl_handle_packet()
 * IMPORTANT : Read the header for tuxctl_ldisc_data_callback() in 
 * tuxctl-ld.c. It calls this function, so all warnings there apply 
 * here as well.
 */
unsigned int LEFT, DOWN;
// buffer used for initialization
int ACK; 
void tuxctl_handle_packet (struct tty_struct* tty, unsigned char* packet)
{
	unsigned char buf[2];
    unsigned a, b, c;
    a = packet[0]; /* Avoid printk() sign extending the 8-bit */
    b = packet[1]; /* values when printing them. */
    c = packet[2];


		switch(a){
		case MTCP_ACK:
		// set ACK flag to 0
		ACK = 0;
			break;

		case MTCP_RESET:
		// initialize buffer
		buf[0] = MTCP_BIOC_ON;
		buf[1] = MTCP_LED_USR;
		tuxctl_ldisc_put(tty, buf, 2);
		//set LED
		tux_LED (tty,LED_save);
		ACK = 0;
			break ;

		case MTCP_BIOC_EVENT:
		/* b ____7____4_3___2___1____0___
			| 1 X X X | C | B | A | START |
			-------------------------------
		   c ____7____4___3______2______1_____0___
			| 1 X X X | right | down | left | up |     */
			// Here we have to flip "left" and "down"
			// initial order: |Right|Down|Left|Up|C|B|A|Start
			// correct order: |Right|Left|Down|Up|C|B|A|Start
			LEFT = (c & Left_mask) >> Leftoffset;
			DOWN = (c & Down_mask) >> Downoffset;
			// six is the place for left
			// five is the place for down 
			button_value =((b & Fourbit_mask) | ((c & Right_Up_mask) << Coffset) | (LEFT << Six) | (DOWN << Five));
			break;
		default:
			return;
	}
	return;
    /*printk("packet : %x %x %x\n", a, b, c); */
}

/******** IMPORTANT NOTE: READ THIS BEFORE IMPLEMENTING THE IOCTLS ************
 *                                                                            *
 * The ioctls should not spend any time waiting for responses to the commands *
 * they send to the controller. The data is sent over the serial line at      *
 * 9600 BAUD. At this rate, a byte takes approximately 1 millisecond to       *
 * transmit; this means that there will be about 9 milliseconds between       *
 * the time you request that the low-level serial driver send the             *
 * 6-byte SET_LEDS packet and the time the 3-byte ACK packet finishes         *
 * arriving. This is far too long a time for a system call to take. The       *
 * ioctls should return immediately with success if their parameters are      *
 * valid.                                                                     *
 *                                                                            *
 ******************************************************************************/

/* 
 * tuxctl_ioctl
 *   DESCRIPTION: tux io control function. switch to cases according to cmd
 *   INPUTS: struct tty_struct* tty -- tux port
 * 			 struct file* file 
	         unsigned cmd -- command issued by the tux
		     unsigned long arg -- package from tux
 *   OUTPUTS: none
 *   RETURN VALUE: 0 or -EINVAL
 *   SIDE EFFECTS: none
 */
int 
tuxctl_ioctl (struct tty_struct* tty, struct file* file, 
	      unsigned cmd, unsigned long arg)
{
    switch (cmd) {
	case TUX_INIT:
		// call tux initialization
		tux_initial(tty);
		return 0;
	case TUX_BUTTONS:
		// call tux button handler
		tux_button(tty,arg);
		return 0;
	case TUX_SET_LED:
		// if ACK = 1, leave	
		if(ACK ==1){
			return -1;
		}
		// set led
		tux_LED(tty,arg);
		return 0;
	// tux tell kernal when finish processing opcode
	case TUX_LED_ACK:
		return 0;
	case TUX_LED_REQUEST:
		return 0;
	case TUX_READ_LED:
		return 0;
	default:
	    return -EINVAL;
    }
}

//------------------------------------- initialization----------------------------//
/* 
 * tux_initial
 *   DESCRIPTION: initialize tux settings, clear button value and led save
 *   INPUTS: struct tty_struct* tty -- tux port
 *   OUTPUTS: none
 *   RETURN VALUE: 0
 *   SIDE EFFECTS: none
 */
int tux_initial (struct tty_struct* tty){
	// buffer used for initialization
	unsigned char buf[2];
	// initialize buffer
	buf[0] = MTCP_BIOC_ON;
	buf[1] = MTCP_LED_USR;
	tuxctl_ldisc_put(tty, buf, 2);
	// initialize LED saved value 
	LED_save = 0;
	// intialize button value
	button_value = 0;
	//set ack to 0
	ACK = 0;
	// success, return 0
	return 0;
}	

//------------------------------------- LED -----------------------------------------//
/* 
 * tux_LED
 *   DESCRIPTION: retrieve led information from arg, find correspoding
 * 				  bit, set tux led value according to mask
 *   INPUTS: struct tty_struct* tty -- tux port
 * 					unsigned long arg -- argument sent to tux
 *   OUTPUTS: none
 *   RETURN VALUE: 0
 *   SIDE EFFECTS: none
 */
int tux_LED(struct tty_struct* tty,unsigned long arg){
/* mask to display each number from 1 to 16 { 0,1,2,3
										      4,5,6,7
										      8,9,A,b,
										      c,d,E,F }*/					
unsigned char sixten_number_mask[16] = {0xE7, 0x06, 0xCB, 0x8F, 
										0x2E, 0xAD, 0xED, 0x86, 
										0xEF, 0xAF, 0xEE, 0x6D, 
										0xE1, 0x4F, 0xE9, 0xE8};

// LED ON or OFF
unsigned int LED_value[4];
// LED buffer
unsigned char buf[6];
// The low 4 bits of the third byte specifies which LED’s should be turned on.
unsigned char LED_ON_OFF;
// The low 4 bits of the highest byte (bits 27:24) specify whether the corresponding decimal points should be turned on. 
unsigned char Decimal;
int i;
// led buffer initialization
buf[0] = MTCP_LED_SET;
buf[1] = 0xF;
buf[2] = 0;
buf[3] = 0;
buf[4] = 0;
buf[5] = 0;
// byte 0, lower 4 bit represent which led is on
LED_ON_OFF = ((arg &(Fourbit_mask<<LEDoffset)) >> LEDoffset);

Decimal = ((arg &(Fourbit_mask<<Decimaloffset)) >> Decimaloffset);
// If ack =1 return

// 	Mapping from 7-segment to bits
//  The 7-segment display is:
// ;		  _A
// ;		F| |B
// ;		  -G
// ;		E| |C
// ;		  -D .dp
// ;
// ; 	The map from bits to segments is:
// ; 
// ; 	__7___6___5___4____3___2___1___0__
// ; 	| A | E | F | dp | G | C | B | D | 
// ; 	+---+---+---+----+---+---+---+---+

// check which led is on
// Arguments: >= 1 bytes
// byte 0 - Bitmask of which LED's to set:
// __7___6___5___4____3______2______1______0___
// | X | X | X | X | LED3 | LED2 | LED1 | LED0 | 
// ----+---+---+---+------+------+------+------+
// Loop to find correct LED musk, store in array form
// The low 16-bits specify a number whose
// hexadecimal value is to be displayed on the 7-segment displays
for(i=0; i<4; i++){
	LED_value[i] = sixten_number_mask[(( (arg & LED_MASK) >> (4*i) )& Fourbit_mask)];
}

// loop to load LED value if led is on
for(i=0; i<4; i++){
	if ((Onebit_mask & (LED_ON_OFF >> i)) != 0){
		buf[i+2] = LED_value[i];
	}
}

// loop to find decimal value
for(i=0; i<4; i++){
		buf[i+2] |= (((Decimal >> i)& Onebit_mask)<<4);
}
//save led value
LED_save = arg;
tuxctl_ldisc_put(tty, buf, 6);
	return 0;

}

//--------------------------------------- BUTTON--------------------------------------//
/* 
 * tux_button
 *   DESCRIPTION: retrieve led information from arg, find correspoding
 * 				  bit, copy tux button to user space
 *   INPUTS: struct tty_struct* tty -- tux port
 * 					unsigned long arg -- argument sent to tux
 *   OUTPUTS: none
 *   RETURN VALUE: 0
 *   SIDE EFFECTS: none
 */
int tux_button (struct tty_struct* tty,unsigned long arg){
	//check return value
	unsigned int user_copy;
	unsigned long* button_pointer;
	// change button value data type
	button_pointer= &(button_value);
	//copy button value to user space 
	user_copy = copy_to_user((void *)arg, (void*)button_pointer, sizeof(long));			
	// check return value
	if(user_copy == 0 ) 
		return 0;
	else 
		return -EFAULT;

}
