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


//static struct button button;
static unsigned long LED_save;
unsigned char button_value;
spinlock_t lock;
/************************ Protocol Implementation *************************/

/* tuxctl_handle_packet()
 * IMPORTANT : Read the header for tuxctl_ldisc_data_callback() in 
 * tuxctl-ld.c. It calls this function, so all warnings there apply 
 * here as well.
 */
int ACK =1; 
unsigned int left, down;
void tuxctl_handle_packet (struct tty_struct* tty, unsigned char* packet)
{
    unsigned a, b, c;
    a = packet[0]; /* Avoid printk() sign extending the 8-bit */
    b = packet[1]; /* values when printing them. */
    c = packet[2];

		switch(a){
		case MTCP_ACK:
		// set ACK flag to 1
			ACK =1;
			break;

		case MTCP_RESET:
			// run initialization function
			tux_initial (tty);
			if (ACK = 1){
			// restore LED value when reset
				tux_LED (tty,LED_save);
				ACK = 0;
			}
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
			left = (c & 0x02) >>1;
			down = (c & 0x04) >>2; 
			button_value = (b & 0xF) | ((c & 0xF) << 4) | (left << 6) | (down <<5);
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
int 
tuxctl_ioctl (struct tty_struct* tty, struct file* file, 
	      unsigned cmd, unsigned long arg)
{
    switch (cmd) {
	case TUX_INIT:
		tux_initial(tty);
		return 0;
	case TUX_BUTTONS:
		tux_button(tty,arg);
		return 0;
	case TUX_SET_LED:	
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

//------------------------- initialization---------------------------//
int tux_initial (struct tty_struct* tty){
	// buffer used for initialization
	unsigned char buf [2];
	// lock initialization
	spin_lock_init (&lock);
	lock = SPIN_LOCK_UNLOCKED;
	// initialize buffer
	buf[0] = MTCP_LED_USR;
	buf[1] = MTCP_BIOC_ON;
	tuxctl_ldisc_put(tty, buf, 2);
	// store LED state for reset
	LED_save = 0;
	button_value = 0;
	// success, return 0
	return 0;
}	

//----------------------------- LED---------------------------//
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
unsigned char LED_value [4];
// LED buffer
unsigned char buf[6];
// The low 4 bits of the third byte specifies which LED’s should be turned on.
unsigned char LED_ON_OFF = (arg &(0x0F<<16)) >> 16; 
// The low 4 bits of the highest byte (bits 27:24) specify whether the corresponding decimal points should be turned on. 
unsigned char Decimal = (arg &(0x0F<<24)) >> 24;
int i;
// led buffer initialization
buf[0] = MTCP_LED_SET;
buf[1] = 0xF;
buf[2] = 0;
buf[3] = 0;
buf[4] = 0;
buf[5] = 0;

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
//Loop to find correct LED musk, store in array form

for(i=0; i<4; i++){
	LED_value[i] = sixten_number_mask [(arg&(0xF << (i*4))>> (i*4))];
}

// loop to load LED value if led is on
for(i=0; i<4; i++){
	// The low 16-bits specify a number whose
	// hexadecimal value is to be displayed on the 7-segment displays
	if ((0x01 & (LED_ON_OFF >> i)) != 0){
		buf[i+2] = LED_value[i];
	}
}

// loop to find decimal value
for(i=0; i<4; i++){
	if ( Decimal & (0x1 << i)!= 0){
		buf[i+2] = LED_value[i] | 0x10;
	}
}

tuxctl_ldisc_put(tty, buf, 6);
	return 0;

}

//----------------------------- BUTTON---------------------------//
int tux_button (struct tty_struct* tty,unsigned long arg){
	// spin lock flag
	unsigned long flag;
	//check return value
	int user_copy;
	
	//copy button value to user space 
	spin_lock_irqsave(&(lock), flag);
	user_copy = copy_to_user((void *)arg, (void*)button, sizeof(long));			
	spin_unlock_irqrestore(&(lock), flag);
	// check return value
	if(user_copy == 0 ) 
		return 0;
	else 
		return -EFAULT;

}

