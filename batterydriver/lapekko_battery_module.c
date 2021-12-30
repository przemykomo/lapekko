#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

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

static void usb_write_callback(struct urb *submit_urb);
static int arduino_probe(struct usb_interface *interface,
                         const struct usb_device_id *id);
static void arduino_disconnect(struct usb_interface *interface);
static BATTERY_DATA_T get_value_from_arduino(char id);
static int lapekko_power_get_battery_property(struct power_supply *psy,
                                              enum power_supply_property psp,
                                              union power_supply_propval *val);

/* Global Structure Definition to store the information about a USB device */
struct usb_arduino {
    struct usb_device *udev;
    char *bulk_in_buffer, *ctrl_buffer;
    struct usb_endpoint_descriptor *bulk_in_endpoint, *bulk_out_endpoint;
    struct urb *bulk_in_urb, *bulk_out_urb;
};

/* Global Variable Declarations */
static struct usb_arduino *main_usb_arduino = NULL;
static bool arduino_connected = false;
DEFINE_MUTEX(arduino_mutex);

static struct usb_device_id arduino_id_table[] = {
    {USB_DEVICE(0x2341, 0x8036)}, {} /* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, arduino_id_table);

static struct usb_driver arduino_usb_driver = {
    .name = "lapekko_arduino",
    .id_table = arduino_id_table,
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

static const struct power_supply_desc lapekko_power_desc = {
    .name = "lapekko_battery",
    .type = POWER_SUPPLY_TYPE_BATTERY,
    .properties = lapekko_power_battery_props,
    .num_properties = ARRAY_SIZE(lapekko_power_battery_props),
    .get_property = lapekko_power_get_battery_property};

static const struct power_supply_config lapekko_power_config = {};

static void usb_write_callback(struct urb *submit_urb) {}

static int arduino_probe(struct usb_interface *interface,
                         const struct usb_device_id *id) {
    struct usb_device *udev = interface_to_usbdev(interface);
    struct usb_arduino *dev = NULL;

    struct usb_host_interface *arduino_currsetting;
    struct usb_endpoint_descriptor *endpoint;

    int retval, i;
    int bulk_end_size_in, bulk_end_size_out;

    msleep(4000);
    pr_info("USB Device Inserted. Probing Arduino.\n");

    if (!udev) {
        pr_err("Error: udev is NULL.\n");
        return -1;
    }

    dev = kmalloc(sizeof(struct usb_arduino), GFP_KERNEL);

    dev->udev = udev;

    arduino_currsetting = interface->cur_altsetting;

    for (i = 0; i < arduino_currsetting->desc.bNumEndpoints; ++i) {
        endpoint = &arduino_currsetting->endpoint[i].desc;

        if (((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) ==
             USB_DIR_IN) &&
            ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
             USB_ENDPOINT_XFER_BULK)) {
            dev->bulk_in_endpoint = endpoint;
        }

        if (((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) ==
             USB_DIR_OUT) &&
            ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
             USB_ENDPOINT_XFER_BULK)) {
            dev->bulk_out_endpoint = endpoint;
        }
    }

    if (!dev->bulk_in_endpoint) {
        pr_err("Error: Could not find bulk IN endpoint.\n");
        return -1;
    }

    if (!dev->bulk_out_endpoint) {
        pr_err("Error: Could not find bulk OUT endpoint.\n");
        return -1;
    }

    // To convert data in little indian format to cpu specific format
    bulk_end_size_in = le16_to_cpu(dev->bulk_in_endpoint->wMaxPacketSize);
    bulk_end_size_out = le16_to_cpu(dev->bulk_out_endpoint->wMaxPacketSize);

    // Allocate a buffer of max packet size of the interrupt
    dev->bulk_in_buffer = kmalloc(bulk_end_size_in, GFP_KERNEL);

    // Creates an urb for the USB driver to use, increments the usage counter,
    // and returns a pointer to it.
    //  ISO_PACKET parameter 0 for interrupt bulk and control urbs

    dev->bulk_out_urb = usb_alloc_urb(0, GFP_KERNEL);
    if (!dev->bulk_out_urb) {
        pr_err("Error: Out URB not allocated space.\n");
        return -1;
    }

    dev->bulk_in_urb = usb_alloc_urb(0, GFP_KERNEL);
    if (!dev->bulk_in_urb) {
        pr_err("Error: In URB not allocated space.\n");
        return -1;
    }

    /* setup control urb packet */
    dev->ctrl_buffer = kzalloc(8, GFP_KERNEL);
    if (!dev->ctrl_buffer) {
        pr_err("Error: Ctrl Buffer could not be allocated memory.\n");
        return -1;
    }

    // Changed value from 0x00 to 0x03 because packet from cdc_acm had it set
    // to 3. IDK what's that, but this module started getting messages from
    // arduino after I changed it to 3.
    retval = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, 0), 0x22,
                             0x21, cpu_to_le16(0x03), cpu_to_le16(0x00),
                             dev->ctrl_buffer, cpu_to_le16(0x00), 0);
    if (retval < 0) {
        pr_err("Error: Control commands(1) could not be sent.\n");
        return -1;
    }

    // set control commands
    dev->ctrl_buffer[0] = 0x80;
    dev->ctrl_buffer[1] = 0x25;
    dev->ctrl_buffer[6] = 0x08;

    retval = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, 0), 0x20,
                             0x21, cpu_to_le16(0x00), cpu_to_le16(0x00),
                             dev->ctrl_buffer, cpu_to_le16(0x08), 0);
    if (retval < 0) {
        pr_err("Error: Control commands(2) could not be sent.\n");
        return -1;
    }

    usb_set_intfdata(interface, dev);

    arduino_connected = true;
    pr_info("USB Arduino device now attached.");

    mutex_lock(&arduino_mutex);

    main_usb_arduino = dev;

    mutex_unlock(&arduino_mutex);
    return 0;
}

static void arduino_disconnect(struct usb_interface *interface) {
    struct usb_arduino *dev;

    dev = usb_get_intfdata(interface);
    usb_set_intfdata(interface, NULL);

    usb_free_urb(dev->bulk_out_urb);
    kfree(dev->bulk_in_buffer);
    kfree(dev->ctrl_buffer);

    mutex_lock(&arduino_mutex);

    if (dev == main_usb_arduino) {
        main_usb_arduino = NULL;
        arduino_connected = false;
    }

    mutex_unlock(&arduino_mutex);

    pr_info("Disconnecting Arduino.");
}

// mutex_lock and mutex_unlock should be called outside of this function!
static BATTERY_DATA_T get_value_from_arduino(char id) {
    if (!arduino_connected) {
        return -1;
    }

    pr_debug("Trying to read value %c\n", id);
    int retval;

    usb_fill_bulk_urb(
        main_usb_arduino->bulk_out_urb, main_usb_arduino->udev,
        usb_sndbulkpipe(main_usb_arduino->udev,
                        (unsigned int)main_usb_arduino->bulk_out_endpoint
                            ->bEndpointAddress),
        &id, 1, usb_write_callback, main_usb_arduino->udev);

    retval = usb_submit_urb(main_usb_arduino->bulk_out_urb, GFP_KERNEL);

    if (retval) {
        return -2;
    }

    int actual_length = 0;
    retval = usb_bulk_msg(
        main_usb_arduino->udev,
        usb_rcvbulkpipe(
            main_usb_arduino->udev,
            (unsigned int)main_usb_arduino->bulk_in_endpoint->bEndpointAddress),
        main_usb_arduino->bulk_in_buffer, sizeof(BATTERY_DATA_T),
        &actual_length, 4000);

    if (retval) {
        pr_err("Error: Could not submit Read URB. RetVal: %d\n", retval);
        return -3;
    }

    return ((BATTERY_DATA_T *)main_usb_arduino->bulk_in_buffer)[0];
}

static int lapekko_power_get_battery_property(struct power_supply *psy,
                                              enum power_supply_property psp,
                                              union power_supply_propval *val) {
    mutex_lock(&arduino_mutex);

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
        mutex_unlock(&arduino_mutex);
        return -EINVAL;
    }

    mutex_unlock(&arduino_mutex);
    return 0;
}

static int __init lapekko_module_init(void) {
    int ret;
    ret = usb_register(&arduino_usb_driver);

    if (ret) {
        pr_err("Failed to register the Arduino device with error code %d\n",
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

static void __exit lapekko_module_exit(void) {
    usb_deregister(&arduino_usb_driver);
    power_supply_unregister(lapekko_power_supply);
}

module_init(lapekko_module_init);
module_exit(lapekko_module_exit);
