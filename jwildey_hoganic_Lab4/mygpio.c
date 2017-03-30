/**
 * \file    mygpio.c
 * \brief   This module contains an implementation for GPIO
 * \author  Josh Wildey
 * \author  Ian Hogan
 * 
 * Course   ENG EC535
 *          Lab 4
 * This file implements a kernel module driver that interfaces to
 * buttons and LEDs that connect to the soldered headers of the
 * Gumstix module.  This module will use GPIO of the XScale
 * architecture.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h> /* printk() */
#include <linux/slab.h> /* kmallac() */
#include <linux/fs.h> /* everything... */
#include <linux/errno.h> /* error codes */
#include <linux/types.h> /* size_t */
#include <linux/proc_fs.h>
#include <linux/fcntl.h> /* 0_ACCMODE */
#include <linux/jiffies.h> /* jiffies */
#include <asm/system.h> /* cli(), *_flags */
#include <asm/uaccess.h> /* copy_from/to_user */
#include <linux/timer.h> /* kernel timer */
#include <linux/list.h>
#include <asm/string.h>
#include <linux/signal.h>
#include <asm/arch/hardware.h> /* GPIO access */
#include <asm/arch/gpio.h>
#include <asm/arch/pxa-regs.h>

// GPIO Pin Definitions
#define GPIO_BTN0 17
#define GPIO_BTN1 101
#define GPIO_LED0 28
#define GPIO_LED1 29
#define GPIO_LED2 30
#define GPIO_LED3 31

// GPIO Name Definitions
#define GPIO_BTN0_NAME "BTN0"
#define GPIO_BTN1_NAME "BTN1"
#define GPIO_LED0_NAME "LED0"
#define GPIO_LED1_NAME "LED1"
#define GPIO_LED2_NAME "LED2"
#define GPIO_LED3_NAME "LED3"

// GPIO High/Low Definitions
#define GPIO_HIGH 1
#define GPIO_LOW  0

// Max read/write lengths
#define MAX_WRT_LEN 5
#define MAX_MSG_LEN 128

// Frequency Definitions
#define F1 500
#define F2 1000
#define F3 1500
#define F4 2000
#define F5 2500
#define F6 3000
#define F7 3500
#define F8 4000

/************************************
 * Set Module Info
 ************************************/
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("GPIO Buttons/LEDs");
MODULE_AUTHOR("Josh Wildey, Ian Hogan");

/************************************
 * Declaration of mygpio.c functions
 ************************************/
static int mygpio_open(struct inode *inode, struct file *filp);
static int mygpio_release(struct inode *inode, struct file *filp);
static ssize_t mygpio_read(struct file *filp,
                            char *buf,
                            size_t count,
                            loff_t *f_pos);
static ssize_t mygpio_write(struct file *filp,
		                     const char *buf,
                             size_t count,
                             loff_t *f_pos);
static void mygpio_exit(void);
static int mygpio_init(void);

/************************************
 * Structure Definitions
 ************************************/
// Structure that declares the usual file access functions
struct file_operations mygpio_fops = {
    read: mygpio_read,
	write: mygpio_write,
	open: mygpio_open,
	release: mygpio_release
};

/************************************
 * Global Variables
 ************************************/
// Major number
static int mygpio_major = 61;

// Counter Initial Value
static unsigned long ctrInitVal = 15;

// Current Counter Value
static unsigned long ctrVal;

// Counter Period (ms)
static int ctrPer = 1000;

// Counter Direction (0 Down, 1 Up)
enum ctrDir_t {DOWN, UP};
static int ctrDir = DOWN;

// Counter State
enum ctrState_t {STOPPED, RUNNING};
static int ctrState = STOPPED;

// Recurring Timer
static struct timer_list gTimer;

// Buffer and Length for read
static char *msgBuffer;
static int msgBufferLen;

/************************************
 * Declaration of the init and exit 
 * functions
 ************************************/
// Set intialization and exit functions
module_init(mygpio_init);
module_exit(mygpio_exit);

/**
 * \brief Set LEDs
 *
 * This function will set the correct GPIO to represent
 * the value of ctrVal to the LED GPIO outputs
 */
void setLEDs(void)
{
    // Variables
    int mask = 0x01;
    int temp = ctrVal;

    // Set LED Outpus
    pxa_gpio_set_value(GPIO_LED0, (temp & mask));
    pxa_gpio_set_value(GPIO_LED1, ((temp >> 1) & mask));
    pxa_gpio_set_value(GPIO_LED2, ((temp >> 2) & mask));
    pxa_gpio_set_value(GPIO_LED3, ((temp >> 3) & mask));
}

/**
 * \brief Get seconds string
 * 
 * This function will create the string for the period
 * of the timer.  This is necessary because I couldn't
 * find how to support floating point calculations.
 *
 * \param *str - pointer to string
 */
void getSecStr(char *str)
{
    switch (ctrPer)
    {
        // 1/2 second
        case F1:
        {
            strcpy(str, "0.5");
            break;
        }
        // 1 Second
        case F2:
        {
            strcpy(str, "1.0");
            break;
        }
        // 1.5 Seconds
        case F3:
        {
            strcpy(str, "1.5");
            break;
        }
        // 2.0 Seconds
        case F4:
        {
            strcpy(str, "2.0");
            break;
        }
        // 2.5 Seconds
        case F5:
        {
            strcpy(str, "2.5");
            break;
        }
        // 3.0 Seconds
        case F6:
        {
            strcpy(str, "3.0");
            break;
        }
        // 3.5 Seconds
        case F7:
        {
            strcpy(str, "3.5");
            break;
        }
        // 4.0 Seconds
        case F8:
        {
            strcpy(str, "4.0");
            break;
        }
        // Default, should never get here
        default:
        {
            strcpy(str, "1.0");
            break;
        }
    }
}

/** 
 * \brief Callback function when timer expires
 *
 * This is the function that is called by the timer
 * when it expires.  It will be responsible for 
 * incrementing/decrementing the current value
 * and for switching the GPIO.
 *
 * \param data
 */
static void timerCallbackFcn(unsigned long data)
{
    int btn0 = 0;
    int btn1 = 0;

    btn0 = pxa_gpio_get_value(GPIO_BTN0);
    btn1 = pxa_gpio_get_value(GPIO_BTN1);

    // State is RUNNING
    if (btn0 > 0)
    {
        ctrState = RUNNING;
        // Direction is UP
        if (btn1 > 0)
        {
            ctrDir = UP;
            ctrVal++;
            // Reset if reached max value
            if (ctrVal > ctrInitVal)
            {
                ctrVal = 1;
            }
        }
        // Direction is DOWN
        else
        {
            ctrDir = DOWN;
            ctrVal--;
            // Reset if reached min value
            if (ctrVal < 1)
            {
                ctrVal = ctrInitVal;
            }
        }
        // Set state of LEDs
        setLEDs();
    }
    // State is STOPPED
    else
    {
        ctrState = STOPPED;
    }

    // Reset Timer
    mod_timer(&gTimer, jiffies + msecs_to_jiffies(ctrPer));
}

/**
 *  \brief Initialize mygpio module
 *
 *  The kernel space function which corresponds to inserting
 *  the module to the kernel.
 *
 *  \return 0 for success, otherwise error
 */
static int mygpio_init(void)
{
    // Variables
	int result;

	// Registering device
	result = register_chrdev(mygpio_major, "mygpio", &mygpio_fops);
	if (result < 0)
	{
		printk(KERN_ALERT
			"mygpio: cannot obtain major number %d\n", mygpio_major);
		return result;
	}

    // Allocate space for message buffer
    msgBuffer = kmalloc(MAX_MSG_LEN, GFP_KERNEL);
    if (!msgBuffer)
    {
        printk("mygpio: cannot allocate space for message buffer\n");
        return -ENOMEM;
    }

    // Request GPIO pins
    gpio_request(GPIO_BTN0, GPIO_BTN0_NAME);
    gpio_request(GPIO_BTN1, GPIO_BTN1_NAME);
    gpio_request(GPIO_LED0, GPIO_LED0_NAME);
    gpio_request(GPIO_LED1, GPIO_LED1_NAME);
    gpio_request(GPIO_LED2, GPIO_LED2_NAME);
    gpio_request(GPIO_LED3, GPIO_LED3_NAME);

    // Set GPIO pin direction
    gpio_direction_input(GPIO_BTN0);
    gpio_direction_input(GPIO_BTN1);
    gpio_direction_output(GPIO_LED0, 0);
    gpio_direction_output(GPIO_LED1, 0);
    gpio_direction_output(GPIO_LED2, 0);
    gpio_direction_output(GPIO_LED3, 0);

    // Setup Timer
    setup_timer(&gTimer, timerCallbackFcn, 0);
    mod_timer(&gTimer, jiffies + msecs_to_jiffies(ctrPer));

    // Set Defaults
    ctrDir = DOWN;
    ctrState = STOPPED;
    ctrVal = ctrInitVal;

    // Set initial state of LEDs
    setLEDs();

    printk(KERN_INFO "mygpio: module loaded.\n");
	return 0;
}

/**
 *  \brief De-intializes mygpio module
 *
 *  The kernal space function which corresponds to removing
 *  the module from the kernel.
 *
 */
static void mygpio_exit(void)
{
	// Freeing the major number
	unregister_chrdev(mygpio_major, "mygpio");

    // Clear the LEDs
    pxa_gpio_set_value(GPIO_LED0, GPIO_LOW);
    pxa_gpio_set_value(GPIO_LED1, GPIO_LOW);
    pxa_gpio_set_value(GPIO_LED2, GPIO_LOW);
    pxa_gpio_set_value(GPIO_LED3, GPIO_LOW);

    // Free up GPIOs
    gpio_free(GPIO_BTN0);
    gpio_free(GPIO_BTN1);
    gpio_free(GPIO_LED0);
    gpio_free(GPIO_LED1);
    gpio_free(GPIO_LED2);
    gpio_free(GPIO_LED3);

    // Delete Timer
    del_timer(&gTimer);

    // Free up buffer memory
    if (msgBuffer)
    {
        kfree(msgBuffer);
    }

	printk(KERN_INFO "mygpio: module unloaded.\n");
}

/**
 *  \brief Open the mygpio device as a file
 *
 *  The Kernel Space function, which corresponds to opening the
 *  /dev/mygpio file in user space. This function is not
 *  required to do anything for this driver
 *
 *  \param *inode - info structure with major/minor numbers
 *  \param *filp  - info structure with available file operations
 *  \return 0 for success
 */
static int mygpio_open(struct inode *inode, struct file *filp)
{
	// Nothing to do here
	return 0;
}

/**
 *  \brief Release the mygpio device as a file
 *
 *  The Kernel Space function which corresponds to closing the
 *  /dev/mygpio file in user space. This function is not
 *  required to do anything for this driver
 *
 *  \param *inode - info structure with major/minor numbers
 *  \param *filp  - info structure with available file operations
 *  \return 0 for success
 */
static int mygpio_release(struct inode *inode, struct file *filp)
{
	// Nothing to do here
	return 0;
}

/**
 * \brief Read the mygpio device as a file
 *
 * The kernel space function which corresponds to reading the
 * /dev/mygpio file in user space
 *
 * \param *filp  - info structure with available file ops
 * \param *buf   - output string
 * \param count  - number of chars/bytes to read
 * \param *f_pos - position in file
 * \return number of bytes read
 */
static ssize_t mygpio_read(struct file *filp, char *buf,
                            size_t count, loff_t *f_pos)
{
    // Variables
    char tmpStr[MAX_MSG_LEN];
    char *secStr = "";

    // Clear the buffer
    memset(msgBuffer, 0, MAX_MSG_LEN);

    //==============
    // Create string
    //==============
    // Counter Value
    sprintf(msgBuffer, "Counter Value:     %lu\n", ctrVal);
    // Counter Period
    getSecStr(secStr);
    sprintf(tmpStr, "Counter Period:    %s sec\n", secStr);
    strcat(msgBuffer, tmpStr);
    // Counter Direction
    if (ctrDir == UP)
    {
        sprintf(tmpStr, "Counter Direction: %s\n", "Up");
    }
    else
    {
        sprintf(tmpStr, "Counter Direction: %s\n", "Down");
    }
    strcat(msgBuffer, tmpStr);
    // Counter State
    if (ctrState == RUNNING)
    {
        sprintf(tmpStr, "Counter State:     %s\n", "Running");
    }
    else
    {
        sprintf(tmpStr, "Counter State:     %s\n", "Stopped");
    }
    strcat(msgBuffer, tmpStr);
    // Message buffer length
    msgBufferLen = strlen(msgBuffer);
    
    // End of buffer reached
    if (*f_pos >= msgBufferLen)
    {
        return 0;
    }

    // Copy to user space
    if (copy_to_user(buf, msgBuffer, msgBufferLen))
    {
        printk(KERN_ALERT "Fault in copying to user space\n");
        return -EFAULT;
    }

    *f_pos += msgBufferLen;
    return msgBufferLen;
}


/**
 *  \brief Write to the mygpio device as a file
 *
 *  The kernel space function which corresponds to writing to the
 *  /dev/mygpio file in user space.
 *
 *  \param *filp  - info structure with available file operations
 *  \param *buf   - input string from user space
 *  \param count  - num of chars/bytes to write
 *  \param *f_pos - position of file
 *  \return number of bytes written
 */
static ssize_t mygpio_write(struct file *filp, const char *buf,
							size_t count, loff_t *f_pos)
{
    // Variables
    char tmp[MAX_WRT_LEN];
    char *pInput;
    unsigned long num;

    // If more than 3 bytes written, thats too many
    // We're only expecting 2 bytes plus EOL char
    // Valid input:
    // f1, f2, f3, f4, f5, f6, f7, f8
    // v1, v2, v3, v4, v5, v6, v7, v8, v9, va, vb, vc, vd, ve, vf
    if (count > 3)
    {
        return -EINVAL;
    }

    // Get string from user space
    if (copy_from_user(tmp, buf, count))
    {
        return -EFAULT;
    }
    
    // Parse string in base 16 (HEX) to parse a-f
    num = simple_strtoul(&tmp[1], &pInput, 16);
    // If 0 or less, there was an error parsing
    if (num <= 0)
    {
        return -EINVAL;
    }

    if (tmp[0] == 'f')
    {
        // Valid frequency range 1-8
        if (num > 0 && num < 9)
        {
            ctrPer = (num * 1000) / 2;
        }
        else
        {
            return -EINVAL;
        }
    }
    else if (tmp[0] == 'v')
    {
        ctrVal = num;
        setLEDs();
    }
    else
    {
        return -EINVAL;
    }

	return count;
}
