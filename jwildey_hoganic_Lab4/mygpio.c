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
#include <asm/arch/irqs.h>  /* interrupts */
#include <linux/interrupt.h>

#define GPIO_BTN0 17
#define GPIO_BTN1 101
#define GPIO_LED0 28
#define GPIO_LED1 29
#define GPIO_LED2 30
#define GPIO_LED3 31

#define GPIO_BTN0_NAME "BTN0"
#define GPIO_BTN1_NAME "BTN1"
#define GPIO_LED0_NAME "LED0"
#define GPIO_LED1_NAME "LED1"
#define GPIO_LED2_NAME "LED2"
#define GPIO_LED3_NAME "LED3"

#define GPIO_HIGH 1
#define GPIO_LOW  0

#define MAX_WRT_LEN 5

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

/************************************
 * Declaration of the init and exit 
 * functions
 ************************************/
// Set intialization and exit functions
module_init(mygpio_init);
module_exit(mygpio_exit);

/**
 * TODO
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

    // Test if running or not
    if (btn0 > 0)
    {
        ctrState = RUNNING;
        if (btn1 > 0)
        {
            ctrDir = UP;
            ctrVal++;
            if (ctrVal > ctrInitVal)
            {
                ctrVal = 1;
            }
        }
        else
        {
            ctrDir = DOWN;
            ctrVal--;
            if (ctrVal < 1)
            {
                ctrVal = ctrInitVal;
            }
        }
        setLEDs();
    }
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

    // Free up GPIOs
    gpio_free(GPIO_BTN0);
    gpio_free(GPIO_BTN1);
    gpio_free(GPIO_LED0);
    gpio_free(GPIO_LED1);
    gpio_free(GPIO_LED2);
    gpio_free(GPIO_LED3);

    del_timer(&gTimer);

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
    //TODO
    return 0;
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
