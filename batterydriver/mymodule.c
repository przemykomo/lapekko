#include "linux/err.h"
#include "linux/gfp.h"
#include "linux/kern_levels.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/stddef.h"
#include "linux/types.h"
#include <linux/interrupt.h>
#include <linux/ioctl.h>
#include <linux/keyboard.h>
#include <linux/kmod.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/usb.h>

#include <linux/power_supply.h>

#include "../common.h"

// Original module that this one is based on:
// https://github.com/suhitsinha/Kernel-Device-Driver-for-Arduino
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Lapekko battery driver");

static ssize_t dev_file_write(struct file *f, const char __user *buf,
                              size_t count, loff_t *off);
static int dev_file_open(struct inode *inode, struct file *file);
static int dev_file_release(struct inode *inode, struct file *file);
static ssize_t dev_file_read(struct file *file, char __user *buf, size_t count,
                             loff_t *off);

/* Global Variable Declarations */
// TODO: maybe remove them?
void *buff = NULL;
void *safe_dev = NULL;
int count_actual_read_len = 0;
static bool arduino_connected = false;

/* Global Structure Definition to store the information about a USB device */
struct usb_arduino {
    struct usb_device *udev;
    struct usb_interface *interface;
    unsigned char minor;
    char *bulk_in_buffer, *bulk_out_buffer, *ctrl_buffer;
    struct usb_endpoint_descriptor *bulk_in_endpoint, *bulk_out_endpoint;
    struct urb *bulk_in_urb, *bulk_out_urb;
};

static int dev_file_open(struct inode *inode, struct file *file) {
    printk(KERN_DEBUG "Arduino Message: Inside Open Function.\n");
    return 0;
}

static int dev_file_release(struct inode *inode, struct file *file) {
    printk(KERN_DEBUG "Arduino Message: Inside Release Function.\n");
    return 0;
}

static void arduino_delete(void) {
    struct usb_arduino *dev = (struct usb_arduino *)safe_dev;

    /* Release all the resources */
    usb_free_urb(dev->bulk_out_urb);
    kfree(dev->bulk_in_buffer);
    kfree(dev->bulk_out_buffer);
    kfree(dev->ctrl_buffer);

    return;
}

static struct file_operations dev_fops = {
    .owner = THIS_MODULE,
    .write = dev_file_write,
    .read = dev_file_read,
    .open = dev_file_open,
    .release = dev_file_release,
};

static struct usb_class_driver arduino_class = {
    .name = "ard%d",
    .fops = &dev_fops,
    .minor_base = 0,
};

static struct usb_device_id arduino_table[] = {
    {USB_DEVICE(0x2341, 0x8036)}, {} /* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, arduino_table);

static void usb_write_callback(struct urb *submit_urb) {
    printk(KERN_DEBUG
           "Arduino Message: This is the write callback for Arduino.\n");
}

static ssize_t dev_file_read(struct file *f, char __user *buf, size_t len,
                             loff_t *off) {
    int retval;

    struct usb_arduino *mydev = safe_dev;
    struct usb_device *dev = mydev->udev;

    printk(KERN_DEBUG "Arduino Message: Inside Read Function.\n");

    retval = usb_bulk_msg(
        dev,
        usb_rcvbulkpipe(
            dev, (unsigned int)mydev->bulk_in_endpoint->bEndpointAddress),
        mydev->bulk_in_buffer, len, &count_actual_read_len, 4000);
    printk(KERN_DEBUG "Count: %lu\n", len);
    printk(KERN_DEBUG "Actual length: %d\n", count_actual_read_len);
    if (retval) {
        printk(KERN_ERR "Error: Could not submit Read URB. RetVal: %d\n",
               retval);
        return -1;
    }
    if (copy_to_user(buf, mydev->bulk_in_buffer,
                     (unsigned long)count_actual_read_len)) {
        printk(KERN_ERR "Error: Copy to user failed.\n");
        return -1;
    }

    return len;
}

static ssize_t dev_file_write(struct file *f, const char __user *buf,
                              size_t count, loff_t *off) {
    int retval;
    struct usb_arduino *mydev = safe_dev;
    struct usb_device *dev = mydev->udev;

    printk(KERN_DEBUG "Arduino Message: Inside write function.\n");

    buff = kmalloc(128, GFP_KERNEL); // TODO: free it in case of an error
    if (copy_from_user(buff, buf, count)) {
        printk(KERN_ERR "Error: Could not read user data!\n");
        return -1;
    }

    usb_fill_bulk_urb(
        mydev->bulk_out_urb, dev,
        usb_sndbulkpipe(
            dev, (unsigned int)mydev->bulk_out_endpoint->bEndpointAddress),
        buff, count, usb_write_callback, dev);

    printk(KERN_DEBUG "Message from user: %s\n", (char *)buff);
    retval = usb_submit_urb(mydev->bulk_out_urb, GFP_KERNEL);
    if (retval) {
        printk(KERN_ERR "Error: Could not submit!\n");
        printk(KERN_ERR "Error Code: %d\n", retval);
        return -1;
    }

    kfree(buff);

    return 0;
}

static int arduino_probe(struct usb_interface *interface,
                         const struct usb_device_id *id) {
    struct usb_device *udev = interface_to_usbdev(interface);
    struct usb_arduino *dev = NULL;

    struct usb_host_interface *arduino_currsetting;
    struct usb_endpoint_descriptor *endpoint;

    int retval, i;
    int bulk_end_size_in, bulk_end_size_out;

    msleep(4000);
    printk(KERN_INFO "USB Device Inserted. Probing Arduino.\n");

    if (!udev) {
        printk(KERN_ERR "Error: udev is NULL.\n");
        return -1;
    }

    dev = kmalloc(sizeof(struct usb_arduino), GFP_KERNEL);

    dev->udev = udev;
    dev->interface = interface;

    arduino_currsetting = interface->cur_altsetting;

    for (i = 0; i < arduino_currsetting->desc.bNumEndpoints; ++i) {
        endpoint = &arduino_currsetting->endpoint[i].desc;

        printk(KERN_DEBUG "Endpoint address: %u",
               (unsigned int)endpoint->bEndpointAddress);
        if (((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) ==
             USB_DIR_IN) &&
            ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
             USB_ENDPOINT_XFER_BULK)) {
            dev->bulk_in_endpoint = endpoint;
            printk(KERN_INFO "Found Bulk Endpoint IN\n");
        }

        if (((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) ==
             USB_DIR_OUT) &&
            ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
             USB_ENDPOINT_XFER_BULK)) {
            dev->bulk_out_endpoint = endpoint;
            printk(KERN_INFO "Found Bulk Endpoint OUT\n");
        }
    }

    if (!dev->bulk_in_endpoint) {
        printk(KERN_ERR "Error: Could not find bulk IN endpoint.\n");
        return -1;
    }

    if (!dev->bulk_out_endpoint) {
        printk(KERN_ERR "Error: Could not find bulk OUT endpoint.\n");
        return -1;
    }

    // To convert data in little indian format to cpu specific format
    bulk_end_size_in = le16_to_cpu(dev->bulk_in_endpoint->wMaxPacketSize);
    bulk_end_size_out = le16_to_cpu(dev->bulk_out_endpoint->wMaxPacketSize);

    // Allocate a buffer of max packet size of the interrupt
    dev->bulk_in_buffer = kmalloc(bulk_end_size_in, GFP_KERNEL);
    dev->bulk_out_buffer = kmalloc(bulk_end_size_out, GFP_KERNEL);

    // Creates an urb for the USB driver to use, increments the usage counter,
    // and returns a pointer to it.
    //  ISO_PACKET parameter 0 for interrupt bulk and control urbs

    dev->bulk_out_urb = usb_alloc_urb(0, GFP_KERNEL);
    if (!dev->bulk_out_urb) {
        printk(KERN_ERR "Error: Out URB not allocated space.\n");
        return -1;
    }

    dev->bulk_in_urb = usb_alloc_urb(0, GFP_KERNEL);
    if (!dev->bulk_in_urb) {
        printk(KERN_ERR "Error: In URB not allocated space.\n");
        return -1;
    }

    /* setup control urb packet */
    dev->ctrl_buffer = kzalloc(8, GFP_KERNEL);
    if (!dev->ctrl_buffer) {
        printk(KERN_ERR "Error: Ctrl Buffer could not be allocated memory.\n");
        return -1;
    }

    // Changed value from 0x00 to 0x03 because packet from cdc_acm had it set
    // to 3. IDK what's that, but this module started getting messages from
    // arduino after I changed it to 3.
    retval = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, 0), 0x22,
                             0x21, cpu_to_le16(0x03), cpu_to_le16(0x00),
                             dev->ctrl_buffer, cpu_to_le16(0x00), 0);
    if (retval < 0) {
        printk(KERN_ERR "Error: Control commands(1) could not be sent.\n");
        return -1;
    }
    printk(KERN_DEBUG "Data Bytes 1: %d\n", retval);

    // set control commands
    dev->ctrl_buffer[0] = 0x80;
    dev->ctrl_buffer[1] = 0x25;
    dev->ctrl_buffer[6] = 0x08;

    retval = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, 0), 0x20,
                             0x21, cpu_to_le16(0x00), cpu_to_le16(0x00),
                             dev->ctrl_buffer, cpu_to_le16(0x08), 0);
    if (retval < 0) {
        printk(KERN_ERR "Error: Control commands(2) could not be sent.\n");
        return -1;
    }
    printk(KERN_DEBUG "Data Bytes 2: %d\n", retval);

    usb_set_intfdata(interface, dev);

    /* We can register the device now, as it is ready. */
    retval = usb_register_dev(interface, &arduino_class);
    if (retval) {
        printk(KERN_ERR "Error: Not able to get a minor for this device.\n");
        usb_set_intfdata(interface, NULL);
        return -1;
    }

    dev->minor = interface->minor;

    arduino_connected = true;
    printk(KERN_INFO "USB Arduino device now attached to /dev/ard%d\n",
           interface->minor - 0);

    safe_dev = dev;

    return 0;
}

static void arduino_disconnect(struct usb_interface *interface) {
    struct usb_arduino *dev;
    int minor = interface->minor;

    dev = usb_get_intfdata(interface);
    usb_set_intfdata(interface, NULL);

    arduino_delete();
    usb_deregister_dev(interface, &arduino_class);

    arduino_connected = false;
    printk(KERN_INFO "Disconnecting Arduino. Minor: %d", minor);
}

static struct usb_driver arduino_driver = {
    .name = "arduino",
    .id_table = arduino_table,
    .probe = arduino_probe,
    .disconnect = arduino_disconnect,
};

static struct power_supply *lapekko_power_supply;

static enum power_supply_property lapekko_power_battery_props[] = {
    POWER_SUPPLY_PROP_STATUS,
    POWER_SUPPLY_PROP_HEALTH,
    POWER_SUPPLY_PROP_PRESENT,
    POWER_SUPPLY_PROP_TECHNOLOGY,
    POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
    POWER_SUPPLY_PROP_CHARGE_FULL,
    POWER_SUPPLY_PROP_CHARGE_NOW,
    POWER_SUPPLY_PROP_CHARGE_COUNTER,
    POWER_SUPPLY_PROP_CAPACITY,
    POWER_SUPPLY_PROP_CAPACITY_LEVEL,
    POWER_SUPPLY_PROP_MODEL_NAME,
    POWER_SUPPLY_PROP_MANUFACTURER,
    POWER_SUPPLY_PROP_VOLTAGE_NOW,
    POWER_SUPPLY_PROP_CURRENT_NOW,
};

static BATTERY_DATA_T get_value_from_arduino(char id) {
    if (!arduino_connected) {
        return -1;
    }
    printk(KERN_DEBUG "Trying to read value %c\n", id);

    int retval;
    struct usb_arduino *mydev = safe_dev;
    struct usb_device *dev = mydev->udev;

    usb_fill_bulk_urb(
        mydev->bulk_out_urb, dev,
        usb_sndbulkpipe(
            dev, (unsigned int)mydev->bulk_out_endpoint->bEndpointAddress),
        &id, 1, usb_write_callback, dev);
    retval = usb_submit_urb(mydev->bulk_out_urb, GFP_KERNEL);
    if (retval) {
        return -2;
    }

    int try_len = sizeof(BATTERY_DATA_T);
    retval = usb_bulk_msg(
        dev,
        usb_rcvbulkpipe(
            dev, (unsigned int)mydev->bulk_in_endpoint->bEndpointAddress),
        mydev->bulk_in_buffer, try_len, &count_actual_read_len,
        4000);
    printk(KERN_DEBUG "Tried length: %d, Actual length: %d\n", try_len, count_actual_read_len);
    if (retval) {
        printk(KERN_ERR "Error: Could not submit Read URB. RetVal: %d\n",
               retval);
        return -3;
    }

    printk(KERN_DEBUG "Buffer char %c\n", mydev->bulk_in_buffer[0]);
    printk(KERN_DEBUG "Buffer int32 %d\n", ((BATTERY_DATA_T *)mydev->bulk_in_buffer)[0]);

    return ((BATTERY_DATA_T *)mydev->bulk_in_buffer)[0];
}

static int lapekko_power_get_battery_property(struct power_supply *psy,
                                              enum power_supply_property psp,
                                              union power_supply_propval *val) {
    switch (psp) {
    case POWER_SUPPLY_PROP_MODEL_NAME:
        val->strval = "Lapekko battery";
        break;
    case POWER_SUPPLY_PROP_MANUFACTURER:
        val->strval = "Pekkorp";
        break;
    case POWER_SUPPLY_PROP_STATUS:
        val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
        break;
    case POWER_SUPPLY_PROP_HEALTH:
        val->intval = POWER_SUPPLY_HEALTH_UNKNOWN;
        break;
    case POWER_SUPPLY_PROP_PRESENT:
        val->intval = arduino_connected;
        break;
    case POWER_SUPPLY_PROP_TECHNOLOGY:
        val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
        break;
    case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
        val->intval = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
        break;
    case POWER_SUPPLY_PROP_CAPACITY:
    case POWER_SUPPLY_PROP_CHARGE_NOW:
        val->intval = get_value_from_arduino(BATTERY_CMD_CHARGE_NOW);
        break;
    case POWER_SUPPLY_PROP_CHARGE_COUNTER:
        val->intval = get_value_from_arduino(BATTERY_CMD_CHARGE_COUNTER);
        break;
    case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
    case POWER_SUPPLY_PROP_CHARGE_FULL:
        val->intval = 100;
        break;
    case POWER_SUPPLY_PROP_VOLTAGE_NOW:
        val->intval = get_value_from_arduino(BATTERY_CMD_VOLTAGE_NOW);
        break;
    case POWER_SUPPLY_PROP_CURRENT_NOW:
        val->intval = get_value_from_arduino(BATTERY_CMD_CURRENT_NOW);
        break;
    default:
        pr_info("%s: some properties deliberately report errors.\n", __func__);
        return -EINVAL;
    }
    return 0;
}

static const struct power_supply_desc lapekko_power_desc = {
    .name = "lapekko_battery",
    .type = POWER_SUPPLY_TYPE_BATTERY,
    .properties = lapekko_power_battery_props,
    .num_properties = ARRAY_SIZE(lapekko_power_battery_props),
    .get_property = lapekko_power_get_battery_property};

static const struct power_supply_config lapekko_power_config = {

};

static int __init myModuleInit(void) {
    int ret;
    ret = usb_register(&arduino_driver);

    if (ret) {
        printk(KERN_ERR
               "Failed to register the Arduino device with error code %d\n",
               ret);
        return 0;
    }

    lapekko_power_supply =
        power_supply_register(NULL, &lapekko_power_desc, &lapekko_power_config);
    if (IS_ERR(lapekko_power_supply)) {
        pr_err("%s: failed to register: %s\n", __func__,
               lapekko_power_desc.name);
        power_supply_unregister(lapekko_power_supply);
        return PTR_ERR(lapekko_power_supply);
    }

    return 0;
}

static void __exit myModuleExit(void) {
    usb_deregister(&arduino_driver);
    power_supply_unregister(lapekko_power_supply);
}

module_init(myModuleInit);
module_exit(myModuleExit);
