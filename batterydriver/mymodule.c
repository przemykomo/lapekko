#include "linux/types.h"
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/mutex.h>
#include <linux/ioctl.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/notifier.h>
#include <linux/keyboard.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/export.h>
#include <linux/kmod.h>

// Original module that this one is based on: https://github.com/suhitsinha/Kernel-Device-Driver-for-Arduino
MODULE_LICENSE("GPL");

/* Function headers */
static ssize_t arduino_write(struct file *f, const char __user *buf, size_t count, loff_t *off);
static int arduino_open(struct inode * inode, struct file * file);
static int arduino_release(struct inode * inode, struct file * file);
static ssize_t arduino_read(struct file * file, char __user * buf, size_t count, loff_t * off);

/* Global Variable Declarations */

void * buff = NULL;
void * safe_dev = NULL;
int count_actual_read_len = 0;

/* Global Structure Definition to store the information about a USB device */
struct usb_arduino {
	struct usb_device *udev;
	struct usb_interface *interface;
	unsigned char minor;
	char serial_number[8];
	char *bulk_in_buffer, *bulk_out_buffer, *ctrl_buffer;
	struct usb_endpoint_descriptor *bulk_in_endpoint, *bulk_out_endpoint;
	struct urb *bulk_in_urb, *bulk_out_urb;
};

static int arduino_open(struct inode * inode, struct file * file)
{
	printk("Arduino Message: Inside Open Function.\n");
	return 0;
}

static int arduino_release(struct inode * inode, struct file * file)
{
	printk("Arduino Message: Inside Release Function.\n");
	return 0;
}

static void arduino_delete(void)
{
	struct usb_arduino * dev = (struct usb_arduino *)safe_dev;

	/* Release all the resources */
	usb_free_urb(dev->bulk_out_urb);
	kfree(dev->bulk_in_buffer);
	kfree(dev->bulk_out_buffer);
	kfree(dev->ctrl_buffer);

	return;
}

static struct file_operations arduino_fops = {
	.owner = THIS_MODULE,
	.write = arduino_write,
	.read = arduino_read,
	.open =	arduino_open,
	.release = arduino_release,
};

static struct usb_class_driver arduino_class = {
	.name = "ard%d",
	.fops = &arduino_fops,
	.minor_base = 0,
};

static struct usb_device_id arduino_table [ ] = {
	//{ USB_DEVICE(0x2341, 0x0043) },
	{ USB_DEVICE(0x2341, 0x8036) },
	{ }                 /* Terminating entry */
};
MODULE_DEVICE_TABLE (usb, arduino_table);

static void arduino_write_callback(struct urb * submit_urb, struct pt_regs * reg)
{
	printk ("Arduino Message: This is the write callback for Arduino.\n");
	return;
}

static ssize_t arduino_read(struct file * f, char __user *buf, size_t len, loff_t *off)
{
	int retval;
	
	struct usb_arduino * mydev = safe_dev;
	struct usb_device * dev = mydev->udev;

	printk("Arduino Message: Inside Read Function.\n");

	retval = usb_bulk_msg(dev, usb_rcvbulkpipe(dev, (unsigned int)mydev->bulk_in_endpoint->bEndpointAddress), 
				mydev->bulk_in_buffer, len, &count_actual_read_len, 4000);
	printk("Count: %lu\n", len);
	printk("Actual length: %d\n", count_actual_read_len);
	if (retval)
	{
		printk("Error: Could not submit Read URB. RetVal: %d\n", retval);
		return -1;
	}
	if (copy_to_user(buf, mydev->bulk_in_buffer, (unsigned long)count_actual_read_len))
	{
		printk("Error: Copy to user failed.\n");
		return -1;
	}

	return len;
}

static ssize_t arduino_write(struct file *f, const char __user *buf, size_t count, loff_t *off)
{
	int retval;
	struct usb_arduino * mydev = safe_dev;
	struct usb_device * dev = mydev->udev;
	
	printk("Arduino Message: Inside write function.\n");

	buff = kmalloc(128, GFP_KERNEL);
	if (copy_from_user(buff, buf, count))
	{
		printk("Error: Could not read user data!\n");
		return -1;
	}
	
	usb_fill_bulk_urb(mydev->bulk_out_urb, dev, usb_sndbulkpipe(dev, (unsigned int)mydev->bulk_out_endpoint->bEndpointAddress), 
			buff, count, (usb_complete_t)arduino_write_callback, dev);
	
	printk("Message from user: %s\n",(char *)buff);
	retval = usb_submit_urb(mydev->bulk_out_urb, GFP_KERNEL);
	if(retval)
	{
		printk("Error: Could not submit!\n");
		printk("Error Code: %d\n", retval);
		return -1;
	}

	kfree(buff);

	return 0;

}

static int arduino_probe(struct usb_interface * interface, const struct usb_device_id * id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct usb_arduino * dev = NULL;

	struct usb_host_interface * arduino_currsetting;
	struct usb_endpoint_descriptor * endpoint;
	
	int retval, i;
	int bulk_end_size_in, bulk_end_size_out;
	
	msleep(4000);
	printk("USB Device Inserted. Probing Arduino.\n");

	if (!udev) {
		printk("Error: udev is NULL.\n");
		return -1;
	}

	dev = kmalloc(sizeof(struct usb_arduino), GFP_KERNEL);

	dev->udev = udev;
	dev->interface = interface;

	arduino_currsetting = interface->cur_altsetting;

	// printk("Number of end points %d\n", arduino_currsetting->desc.bNumEndpoints);
	printk("Line %d\n", __LINE__);
	for (i = 0; i < arduino_currsetting->desc.bNumEndpoints; ++i) {
                printk("Line %d\n", __LINE__);
		endpoint = &arduino_currsetting->endpoint[i].desc;

                printk("Endpoint address: %u", (unsigned int)endpoint->bEndpointAddress);
		if (((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN)
		    && ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK)) {
			dev->bulk_in_endpoint = endpoint;
			printk("Found Bulk Endpoint IN\n");
		}
		
		if (((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT)
		    && ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK)) {
			dev->bulk_out_endpoint = endpoint;
			printk("Found Bulk Endpoint OUT\n");
		}

	}

	if (!dev->bulk_in_endpoint) {
		printk("Error: Could not find bulk IN endpoint.\n");
		return -1;
	}
	
	if (!dev->bulk_out_endpoint) {
		printk("Error: Could not find bulk OUT endpoint.\n");
		return -1;
	}

	//To convert data in little indian format to cpu specific format
	bulk_end_size_in = le16_to_cpu(dev->bulk_in_endpoint->wMaxPacketSize);
	bulk_end_size_out = le16_to_cpu(dev->bulk_out_endpoint->wMaxPacketSize);
	
	//Allocate a buffer of max packet size of the interrupt
	dev->bulk_in_buffer = kmalloc(bulk_end_size_in, GFP_KERNEL);
	dev->bulk_out_buffer = kmalloc(bulk_end_size_out, GFP_KERNEL);

	//Creates an urb for the USB driver to use, increments the usage counter, and returns a pointer to it. 
	// ISO_PACKET parameter 0 for interrupt bulk and control urbs

	dev->bulk_out_urb = usb_alloc_urb(0, GFP_KERNEL);
	if(!dev->bulk_out_urb)
	{
		printk("Error: Out URB not allocated space.\n");
		return -1;
	}
	
	dev->bulk_in_urb = usb_alloc_urb(0, GFP_KERNEL);
	if(!dev->bulk_in_urb)
	{
		printk("Error: In URB not allocated space.\n");
		return -1;
	}

	/* setup control urb packet */
	dev->ctrl_buffer = kzalloc(8, GFP_KERNEL);
	if(!dev->ctrl_buffer)
	{
		printk("Error: Ctrl Buffer could not be allocated memory.\n");
		return -1;
	}
	

        // Changed value from 0x00 to 0x03 because packet from cdc_acm had it set to 3. IDK what's that, but it this module started getting messages from arduino after I changed it to 3.
	retval = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, 0), 0x22, 0x21, cpu_to_le16(0x03), cpu_to_le16(0x00), dev->ctrl_buffer,
		cpu_to_le16(0x00), 0);
	if(retval < 0)
	{
		printk("Error: Control commands(1) could not be sent.\n");
		return -1;
	}
	printk("Data Bytes 1: %d\n", retval);

	// set control commands 
	dev->ctrl_buffer[0] = 0x80;
	dev->ctrl_buffer[1] = 0x25;
	dev->ctrl_buffer[6] = 0x08;

	retval = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, 0), 0x20, 0x21, cpu_to_le16(0x00), cpu_to_le16(0x00), dev->ctrl_buffer,
		cpu_to_le16(0x08), 0);
	if(retval < 0)
	{
		printk("Error: Control commands(2) could not be sent.\n");
		return -1;
	}
	printk("Data Bytes 2: %d\n", retval);	
        printk("Line %d\n", __LINE__);
        printk("Serial number: %u\n", (unsigned int)udev->descriptor.iSerialNumber);
	/* Retrieve a serial. */
        /*
        int test_ret = usb_string(udev, udev->descriptor.iSerialNumber,
				dev->serial_number, sizeof(dev->serial_number));
        printk("test_ret: %d\n", test_ret);
	if (!test_ret) {
		printk("Error: could not retrieve serial number\n");
		return -1;
	} */

	usb_set_intfdata(interface, dev);
	
        printk("Line %d\n", __LINE__);
	/* We can register the device now, as it is ready. */
	retval = usb_register_dev(interface, &arduino_class);
	if (retval) {
		printk("Error: Not able to get a minor for this device.\n");
		usb_set_intfdata(interface, NULL);
		return -1;
	}

        printk("Line %d\n", __LINE__);
	dev->minor = interface->minor;

	printk("USB Arduino device now attached to /dev/ard%d\n", interface->minor - 0);
	
	safe_dev = dev;
	
        printk("Line %d\n", __LINE__);
	return 0;
}

static void arduino_disconnect(struct usb_interface *interface)
{
	struct usb_arduino * dev;
	int minor = interface->minor;

	dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	arduino_delete();
	/* give back minor */
	usb_deregister_dev(interface, &arduino_class);

	printk("Disconnecting Arduino\n. Minor: %d", minor);
}

static struct usb_driver arduino_driver = {
	.name = "arduino",
	.id_table = arduino_table,
	.probe = arduino_probe,
	.disconnect = arduino_disconnect,
};

int init_module()
{
	int regResult;
	regResult = usb_register(&arduino_driver);

	if(regResult)
	{
		printk("Failed to register the Arduino device with error code %d\n", regResult);
		return 0;
	}

	return 0;
}


void cleanup_module()
{
        usb_deregister(&arduino_driver);
	printk("Message: Inside cleanup module.\n");
}
