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
	
static struct button button;

/************************ Protocol Implementation *************************/

/* tuxctl_handle_packet()
 * IMPORTANT : Read the header for tuxctl_ldisc_data_callback() in 
 * tuxctl-ld.c. It calls this function, so all warnings there apply 
 * here as well.
 */
void tuxctl_handle_packet (struct tty_struct* tty, unsigned char* packet)
{
    unsigned a, b, c;
    a = packet[0]; /* Avoid printk() sign extending the 8-bit */
    b = packet[1]; /* values when printing them. */
    c = packet[2];

		switch(a){
		case MTCP_ACK:
		// lower ACK to 0
		MTCP_ACK = 0;
			return 0;

		case MTCP_RESET:
			return 0;

		case MTCP_BIOC_EVENT:
			unsigned long flag;
			unsigned int LEFT, DOWN;
			spin_lock_irqsave(&(button.lock), flag);
			button.value = (b & 0xF) | ((c & 0xF) << 4);
			spin_unlock_irqrestore(&(button.lock), flag);
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
		return tux_init(tty);
	case TUX_BUTTONS:
		return tux_button(tty,arg);
	case TUX_SET_LED:
		return tux_LED(tty,arg);
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

// initialization
int tux_initial (struct tty_struct* tty){
	button.value = 0xFF; 
	button.lock= SPIN_LOCK_UNLOCKED;
	return 0;
}	

int tux_LED(struct tty_struct* tty,unsigned long arg){
// mask to display each number from 1 to 16
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
// array to store which sefment is on
unsigned char seven_segment[4];
int j =2;

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
//loop over each LED set. 
for(i=0; i<4; i++){
	// The low 16-bits specify a number whose
	// hexadecimal value is to be displayed on the 7-segment displays
	// 1st number: LED_value[0] = (arg & (0xF)); 2nd number: LED_value[1] = (arg & (0xF << 4));
	// 3rd number: LED_value[2] = (arg & (0xF << 8));4th number: LED_value[3] = (arg & (0xF << 12));
	if ((0x01 & (LED_ON_OFF >> i)) != 0){
	LED_value[i] = (arg & (0xF << (i*4)));
	// seven_segment[0] = ( sixten_number_mask[1st number]) | ((Decimal << 4))
	seven_segment[i] = ( sixten_number_mask[LED_value[i]]) | ((Decimal << 4))
	}
	else{
		seven_segment[i]=0x0;
	}
}

buf[0] = MTCP_LED_SET;
buf[1] = LED_ON_OFF;

	// check which led is on
	// Arguments: >= 1 bytes
	// byte 0 - Bitmask of which LED's to set:
	// __7___6___5___4____3______2______1______0___
	// | X | X | X | X | LED3 | LED2 | LED1 | LED0 | 
	// ----+---+---+---+------+------+------+------+
for (i=0; i<4; i++){
	// if current led is on
	if ((0x01 & (LED_ON_OFF >> i)) != 0){
		buf[j]= seven_segment[i];
		j++
	}
	else {
		buf[j] = 0x0;
	}
}
tuxctl_ldisc_put(tty, buf, j);
return 0;

}


int tux_button (struct tty_struct* tty,unsigned long arg){
	unsigned long flag;
	unsigned long * button;
	int user_copy;
	button = &(button.value);
	
	spin_lock_irqsave(&(button.lock), flag);
	copy_to_user((void *)arg, (void*)button, sizeof(long));			
	spin_unlock_irqrestore(&(button.lock), flag);
	
	if(copy_to_user((void *)arg, (void*)button, sizeof(long)) == 0) 
		return 0;
	else 
		return -EFAULT;

}