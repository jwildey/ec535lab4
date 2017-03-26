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

#define GPIO_HIGH 1
#define GPIO_LOW  0

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
static irqreturn_t btn0Handler(unsigned int irq, void *dev_id, struct pt_regs *regs);
static irqreturn_t btn1Handler(unsigned int irq, void *dev_id, struct pt_regs *regs);

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
static irqreturn_t btn0Handler(unsigned int irq, void *dev_id, struct pt_regs *regs)
{
    printk(KERN_INFO "BTN0 Handler!!!!\n");
    printk(KERN_INFO "GPIO_LED0: %d\n", pxa_gpio_get_value(GPIO_LED0));
    /*if(gpio_get_value(GPIO_LED0) > 0)
    {
        printk(KERN_INFO "GPIO_LED0 is high, setting to low\n");
        //gpio_set_value(GPIO_LED0, GPIO_LOW);
    }
    else
    {
        printk(KERN_INFO "GPIO_LED0 is low, setting to high\n");
        //gpio_set_value(GPIO_LED0, GPIO_HIGH);
    }*/
    return IRQ_HANDLED;
}

/**
 * TODO
 */
static irqreturn_t btn1Handler(unsigned int irq, void *dev_id, struct pt_regs *regs)
{
    printk(KERN_INFO "BTN1 Handler!!!!\n");
    printk(KERN_INFO "GPIO_LED0: %d\n", pxa_gpio_get_value(GPIO_LED1));
    /*if(gpio_get_value(GPIO_LED1) > 0)
    {
        printk(KERN_INFO "GPIO_LED1 is high, setting to low\n");
        //gpio_set_value(GPIO_LED1, GPIO_LOW);
    }
    else
    {
        printk(KERN_INFO "GPIO_LED1 is low, setting to high\n");
        //gpio_set_value(GPIO_LED1, GPIO_HIGH);
    }*/
    return IRQ_HANDLED;
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
    gpio_request(GPIO_BTN0, "BTN0");
    gpio_request(GPIO_BTN1, "BTN1");
    gpio_request(GPIO_LED0, "LED0");
    gpio_request(GPIO_LED1, "LED1");
    gpio_request(GPIO_LED2, "LED2");
    gpio_request(GPIO_LED3, "LED3");

    // Set GPIO pin direction
    gpio_direction_input(GPIO_BTN0);
    gpio_direction_input(GPIO_BTN1);
    gpio_direction_output(GPIO_LED0, GPIO_DFLT_LOW);
    gpio_direction_output(GPIO_LED1, 0);
    gpio_direction_output(GPIO_LED2, 0);
    gpio_direction_output(GPIO_LED3, 0);

    // Setup IRQs
    result = request_irq(gpio_to_irq(GPIO_BTN0),                     //requested interrupt
                         (irq_handler_t) btn0Handler,                // pointer to handler function
                         IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, // interrupt mode flag
                         "btn0Handler",                              // used in /proc/interrupts
                         NULL);                 // the *dev_id shared intterupt lines, NULL is okay

    result = request_irq(gpio_to_irq(GPIO_BTN1),                     //requested interrupt
                         (irq_handler_t) btn1Handler,                // pointer to handler function
                         IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, // interrupt mode flag
                         "btn1Handler",                              // used in /proc/interrupts
                         NULL);                 // the *dev_id shared intterupt lines, NULL is okay

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

    // Free up IRQs
    free_irq(gpio_to_irq(GPIO_BTN0), NULL);
    free_irq(gpio_to_irq(GPIO_BTN1), NULL);

    // Free up GPIOs
    gpio_free(GPIO_BTN0);
    gpio_free(GPIO_BTN1);
    gpio_free(GPIO_LED0);
    gpio_free(GPIO_LED1);
    gpio_free(GPIO_LED2);
    gpio_free(GPIO_LED3);

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
    //TODO
	return 0;
}
