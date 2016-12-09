#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb/input.h>
#include <asm/unaligned.h>

#define DRIVER_VERSION "0.6"
#define DRIVER_AUTHOR "Ondra Havel <ondra.havel@gmail.com>, Slawomir Szczyrba <sszczyrba@gmail.com>"
#define DRIVER_DESC "USB xp-pen tablet driver"
#define DRIVER_LICENSE "GPL"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE(DRIVER_LICENSE);

#define USB_VENDOR_ID_XPPEN	0x28bd
#define USB_PRODUCT_ID_G540	0x0075

#define USB_AM_PACKET_LEN   8

static int buttons[]={BTN_0,BTN_1};

#define AM_MAX_ABS_X	0x7fff
#define AM_MAX_ABS_Y	0x7fff
#define AM_MAX_PRESSURE	0x07ff

struct xppen {
	unsigned char *data;
	dma_addr_t data_dma;
	struct input_dev *dev;
	struct usb_device *usbdev;
	struct urb *irq;
	char phys[32];
};

static inline void handle_default(struct xppen *xppen)
{
	unsigned char *data = xppen->data;
	struct input_dev *dev = xppen->dev;

	switch(data[0]) {
		case 0x09:   /* default hid report */
		    input_report_abs(dev, ABS_X, le16_to_cpup((__le16 *)&data[2]));
		    input_report_abs(dev, ABS_Y, le16_to_cpup((__le16 *)&data[4]));
		    input_report_abs(dev, ABS_PRESSURE, le16_to_cpup((__le16 *)&data[6]));
		    input_report_key(dev, BTN_LEFT, data[1] & 0x1);
		    input_report_key(dev, buttons[0], data[1] & 0x2);
		    input_report_key(dev, buttons[1], data[1] & 0x4);
		break;
		default :
		    printk(KERN_WARNING "xp-pen irq : received unknown HID message (0x%02x)",data[0]);
	        break;
	}
}


static void xppen_irq(struct urb *urb)
{
	struct xppen *xppen = urb->context;
	int retval;

	switch (urb->status) {
		case 0:
			/* success */
			switch( xppen->usbdev->descriptor.idProduct ) {
				case USB_PRODUCT_ID_G540:
				    handle_default(xppen);
				    break;
				default:
				    handle_default(xppen);
				    break;
			}
			break;
		case -ECONNRESET:
		case -ENOENT:
		case -ESHUTDOWN:
			/* this urb is terminated, clean up */
			printk("%s - urb shutting down with status: %d", __func__, urb->status);
			return;
		default:
			printk("%s - nonzero urb status received: %d", __func__, urb->status);
			break;
	}

	input_sync(xppen->dev);

	retval = usb_submit_urb (urb, GFP_ATOMIC);
	if (retval)
		printk("%s - usb_submit_urb failed with result %d", __func__, retval);
}

static struct usb_device_id xppen_ids[] = {
	{ USB_DEVICE(USB_VENDOR_ID_XPPEN, USB_PRODUCT_ID_G540) },
	{}
};

MODULE_DEVICE_TABLE(usb, xppen_ids);

static int xppen_open(struct input_dev *dev)
{
	struct xppen *xppen = input_get_drvdata(dev);

	xppen->irq->dev = xppen->usbdev;
	if (usb_submit_urb(xppen->irq, GFP_KERNEL))
		return -EIO;

	return 0;
}

static void xppen_close(struct input_dev *dev)
{
	struct xppen *xppen = input_get_drvdata(dev);

	usb_kill_urb(xppen->irq);
}

static int xppen_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usb_endpoint_descriptor *endpoint;
	struct xppen *xppen;
	struct input_dev *input_dev;
	int error = -ENOMEM, i;

	xppen = kzalloc(sizeof(struct xppen), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!xppen || !input_dev)
		goto fail1;

	xppen->data = (unsigned char *)usb_alloc_coherent(dev, USB_AM_PACKET_LEN, GFP_KERNEL, &xppen->data_dma);
	if (!xppen->data)
		goto fail1;

	xppen->irq = usb_alloc_urb(0, GFP_KERNEL);
	if (!xppen->irq)
		goto fail2;

	xppen->usbdev = dev;
	xppen->dev = input_dev;

	usb_make_path(dev, xppen->phys, sizeof(xppen->phys));
	strlcat(xppen->phys, "/input0", sizeof(xppen->phys));

	input_dev->name = "xp-pen osu! tablet";
	input_dev->phys = xppen->phys;
	usb_to_input_id(dev, &input_dev->id);
	input_dev->dev.parent = &intf->dev;

	input_set_drvdata(input_dev, xppen);

	input_dev->open = xppen_open;
	input_dev->close = xppen_close;

	input_dev->evbit[0] |= BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input_dev->keybit[BIT_WORD(BTN_DIGI)] |= BIT_MASK(BTN_TOOL_PEN);
	input_dev->keybit[BIT_WORD(BTN_LEFT)] |= BIT_MASK(BTN_LEFT);
	for(i=0;i<sizeof(buttons)/sizeof(buttons[0]);i++)  __set_bit(buttons[i], input_dev->keybit);

	input_set_abs_params(input_dev, ABS_X, 0, AM_MAX_ABS_X, 4, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, AM_MAX_ABS_Y, 4, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0, AM_MAX_PRESSURE, 0, 0);

	endpoint = &intf->cur_altsetting->endpoint[0].desc;

	usb_fill_int_urb(xppen->irq, dev,
			usb_rcvintpipe(dev, endpoint->bEndpointAddress),
			xppen->data, USB_AM_PACKET_LEN,
			xppen_irq, xppen, endpoint->bInterval);
	xppen->irq->transfer_dma = xppen->data_dma;
	xppen->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	error = input_register_device(xppen->dev);
	if (error)
		goto fail3;

	usb_set_intfdata(intf, xppen);
	return 0;

fail3:   usb_free_urb(xppen->irq);
fail2:   usb_free_coherent(dev, USB_AM_PACKET_LEN, xppen->data, xppen->data_dma);
fail1:   input_free_device(input_dev);
	kfree(xppen);
	return error;
}

static void xppen_disconnect(struct usb_interface *intf)
{
	struct xppen *xppen = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);
	if (xppen) {
		usb_kill_urb(xppen->irq);
		input_unregister_device(xppen->dev);
		usb_free_urb(xppen->irq);
		usb_free_coherent(interface_to_usbdev(intf), USB_AM_PACKET_LEN, xppen->data, xppen->data_dma);
		kfree(xppen);
	}
}

static struct usb_driver xppen_driver = {
	.name = "xppen",
	.probe = xppen_probe,
	.disconnect = xppen_disconnect,
	.id_table =   xppen_ids,
};

static int __init xppen_init(void)
{
	int rv;

	if((rv = usb_register(&xppen_driver)) != 0)
		return rv;

	printk(DRIVER_DESC " " DRIVER_VERSION "\n");

	return 0;
}

static void __exit xppen_exit(void)
{
	usb_deregister(&xppen_driver);
}

module_init(xppen_init);
module_exit(xppen_exit);
