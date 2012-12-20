/* This is the simple USB device driver for Pen drive,
 * this is the host side driver.. 
 * had it been the device side driver, then it would be called as 
 * USB gadget driver..
 */

/*
Steps to get this driver work...
1. compile the driver without any warning/error
2. load the driver using user space not super user/root
3. mount the usbfs using commad -> $ mount -t usbfs none /proc/bus/usb
4. insert the test device/ pen driver/ any usb device which you wrote driver for
  (This device must not be keyboard/mouse)...
5. check the dmesg... your probe function should get called giving you details about the 
	device plugged..
6. This device will not open and cannot shows data in it.. for that you need to 
	write an associate SCSI driver for mass storage.. 
Looking forward to it...........
 */

#include<linux/module.h>
#include<linux/kernel.h>
#include<linux/usb.h>
#include<linux/init.h>
#include<linux/slab.h>
#include<linux/types.h>
#include<asm/uaccess.h>
#include<linux/errno.h>

#define USB_STICK_MINOR_BASE	192

// these are the IDs for JetFlash Pendrive Transcend 4Gb. 
#define TRANSCENT_VENDOR_ID		0x058f
#define TRANSCENT_PRODUCT_ID		0x6387
#define SANDISK_VENDOR_ID		0X0781
#define SANDISK_PRODUCT_ID		0X5567
#define TRANSCENT_IPOD_VENDOR_ID	0X8564
#define TRANSCENT_IPOD_PRODUCT_ID	0X5000

static const struct usb_device_id usb_id_table[] = {
	{ USB_DEVICE(TRANSCENT_VENDOR_ID, TRANSCENT_PRODUCT_ID)},
	{ USB_DEVICE(SANDISK_VENDOR_ID, SANDISK_PRODUCT_ID)},
	{ USB_DEVICE(TRANSCENT_IPOD_VENDOR_ID, TRANSCENT_IPOD_PRODUCT_ID)},
	{}				/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, usb_id_table);

// structure from the usb_skeleton.c
typedef struct usb_stick
{
	struct usb_device	*udev;			/* the usb device for this device */
	struct usb_interface	*interface;		/* the interface for this device */
	struct semaphore	limit_sem;		/* limits the number of writes in the progress */
	struct usb_anchor	submitted;		/* in case we need to retract our submissions */
	struct urb		*bulk_in_urb;		/* the urb to read data with */
	unsigned char           *bulk_in_buffer;	/* the buffer to receive data */
	size_t			bulk_in_size;		/* the size of the receive buffer */
	size_t			bulk_in_filled;		/* number of bytes in the buffer */
	size_t			bulk_in_copied;		/* already copied to user space */
	__u8			bulk_in_endpointAddr;	/* the address of the bulk in endpoint */
	__u8			bulk_out_endpointAddr;	/* the address of the bulk out endpoint */
	int			errors;			/* the last request tanked */
	int			open_count;		/* count the number of openers */
	bool			ongoing_read;		/* a read is going on */
	bool			processed_urb;		/* indicates we haven't processed the urb */
	spinlock_t		err_lock;		/* lock for errors */
	struct kref		kref;
	struct completion	bulk_in_completion;	/* to wait for an ongoing read */
	struct mutex		io_mutex;		/* sync IO with disconnect */
}USB_S;


static struct usb_driver usb_stick_driver;

/* step-3 */ 	
/* Things to do here --			date: 19 Dec 12
	Simple functions which enable the user program to 
	open / close / read / write data to or from the device
	so the obvious choice would be a character driver.
	- declare the file operation structure and it members 
	  as our different device specific file operation functions.
	- add the entry of this structure to the struct usb_class_driver {
	- develop the functions according to the requirement besides standard 
	  development procedure.
*/
static int usb_stick_open(struct inode *inode, struct file *fp)
{
	USB_S *dev = NULL;
	struct usb_interface *interface;
	int subminor;
	int retval = 0;

	/* Things to do here:
		1. identify the interfaces using  usb_find_interface(&private_struct, subminori);
		2. subminor can be get using api: iminor(inode);
		2. try to connect then to the private device	usb_get_intfdata(interface);
	*/

	subminor = iminor(inode);
	
	interface = usb_find_interface(&usb_stick_driver, subminor);
	if(!interface)	{
		printk(KERN_INFO"can't find device for minor %d", subminor);
		retval = -ENODEV;
		goto exit;
	}

	dev = usb_get_intfdata(interface);
	if(!dev)	{
		retval = -ENODEV;
		goto exit;
	}
		
	fp->private_data = dev;
exit:
	return retval;
}


static int usb_stick_release(struct inode *inode, struct file *fp)
{
	/* Things to do here:
		-- nothing actually, in this driver's case
	*/
	USB_S *dev;
	int retval;

	dev = fp->private_data;
	if(!dev){
		retval = -ENODEV;
		goto exit;
	}

exit:
	return retval;
		
}


static ssize_t usb_stick_read(struct file *fp, char __user *u_buffer, size_t count, loff_t *offset)
{
	return 0;	

}


static void usb_stick_write_completion_handler(struct urb *urb)
{
	USB_S *dev;
	/* Undersstand the completion handler first:
		This is the function acting as an ISR, this always get called in INTERRUPT CONTEXT, in only 3 situaltions 
		1. After successful transmission of data [means if(urb->status == 0) is true]
		2. Error(s) encounterd during transmission.
		3. The URB was unlinked by the USB core, hense to free the URB allocated this handler is called.
	      The most important thing to mention here is that, its an isr so its execution should be ATOMIC 
	      [mense without sleep]. So in order to make it that fast, only status checking and free-ing the URB 
	      or may be checking the command and retransmission kind of bussiness shuold be done here. Please try 
	      to avoid memory allocation, loops, more interrupt situations which are prone to make this handler 
	      sleeps.

	Things to do here:
		1. check the status of the URB, check for the Errors too
		2. free the URB if its work done.
	*/
	
	dev = urb->context;

	if(urb->status)
	{
		if (urb->status == -ENODEV ||		// the USB device doesn't exits 		
			urb->status == -ENOENT ||	// specified interface or endpoint does not exit or is not enabled
			urb->status == -ESHUTDOWN)	// a device or host controller has been disabled or can not work out
		{
			printk(KERN_INFO"URB termination with status %d",urb->status);
		}
		else
		{
			printk(KERN_INFO"non-zero status of the URB %d",urb->status);
		}
	}
	
	// here means our transmission is successful 
	printk(KERN_INFO"transmission successful----------------------------------");
	printk(KERN_INFO"actual data trasmitted %d",urb->actual_length);

	// time to free the allocated URB
	usb_free_coherent(urb->dev,			// our device 
			  urb->transfer_buffer_length,	// buffer length
			  urb->transfer_buffer,		// transmission buffer
			  urb->transfer_dma);		// transfer type

}

static ssize_t usb_stick_write(struct file *fp, const char __user *u_buffer, size_t count, loff_t *offset)
{
	USB_S *dev;
	struct urb *urb = NULL;
	int retval = 0;
	char *buff;

	/* Things to do here:
		1. retrive the pointer from file->private_data;
		2. check the device is still plugged
		3. verify that we have data to write
		4. allocate URB
		5. allocate DMA buffer to URB,- to save data comes from user to transfer it to device in efficient manner.
		6. copy the data from user and save it to the URB's buffer.
		7. initialize URB
		8. submitt the URB - this will actually tranfer the data from USBcore to the Device.
	*/
	
	// getting pointer to the private data structure
	dev = fp->private_data;

	//check the device is still plugged?
	if(!dev->udev)	{
		retval = -ENODEV;
		printk(KERN_INFO"No device or device is unplugged");
		goto exit;
	}
	
	// verufy that we have data to write
	if(count == 0){
		printk(KERN_INFO"no data to write");
	 	return -1;
	}
	
	// allocate the URB
	urb = usb_alloc_urb(0, GFP_KERNEL);
	if(!urb){
		printk(KERN_INFO"URB could not be allocate");
		retval = -ENOMEM;
		goto exit;
	}
	
	// allocate DMA buffer for URB
	buff = usb_alloc_coherent(dev->udev, 		// device
				 count,			// URB's buffer size
				 GFP_KERNEL,		// familier kernel flag
				 &urb->transfer_dma);	// (output) DMA address to the buffer
	if(!buff){
		retval = -ENOMEM;
		printk(KERN_INFO"no memory allocated to URB");
		goto exit;
	}
	
	// copy the data from user and save it to the URB's buffer [make sure the pointers...]
	// make sure this is running properly, most of the problems may come through this api.
	// TODO: add debug here so that can be verified.
	if (copy_from_user(buff, u_buffer, count))
	{
		retval = -EFAULT;
		goto exit;
	}

	// Properly initialize the URB,
	usb_fill_bulk_urb(urb,					// pointer to the URB to be initialize		
			  dev->udev,				// USB device to which this URB is to be sent to
			  usb_sndbulkpipe(dev->udev, dev->bulk_out_endpointAddr),	//the specific endpoint to which this URB is sent to
			  buff,					// pointer to the buffer which holds the data to be transfered.
			  count,				// lenght of the buffer pointed by 'buff'
			  usb_stick_write_completion_handler,	// pointer to the completion handler that is called when urb is completed
			  dev);					// context, means pointer to the blob which is added to urb structure for later
								// retreival by completion handler function.
	
	// this indicates that the "urb->transfer_dma" buffer is valid on submit.
	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	// submit the URB, it's the time.
	// kernel flag can be GFP_ATOMIC, which does not allow to sleep, 
	// specially when this submit is done with spinlock held
	retval = usb_submit_urb(urb, GFP_KERNEL);
	if(retval){
		printk(KERN_INFO"failed to submit the URB, error %d", retval);
		goto exit;
	}
exit:	
	return retval;

}
   

static struct file_operations usb_fops ={
	.owner	= 	THIS_MODULE,
	.read	= 	usb_stick_read,
	.write	=	usb_stick_write,
	.open	=	usb_stick_open,
	.release=	usb_stick_release,
};


static struct usb_class_driver usb_stick_driver_class = {
	.name = "USB_STICK_%d",
	.fops = &usb_fops,
	.minor_base = USB_STICK_MINOR_BASE,
};


/* AlMighty probe and disconnect methods */
static int usb_stick_probe(struct usb_interface *intf, 
			const struct usb_device_id *id)
{
	USB_S *usb_stick;
	struct usb_host_interface *h_interface;
	struct usb_endpoint_descriptor *endpoint;
	int i, ret;
	size_t	buffer_size;

	/* step 1 */
	/* Things to do here: 
	 References: usb-skeleton.c , usb.c
	 - allocate the device private structure a memory
	 - assign cur_altsetting to the host interface descriptor
	 - set the current interface as the driver interface using usb_set_interface();
	 - register the USB device using usb_register_dev();
	 */

	 /* step 2 	date: 17 Dec 12*/ 
	 /*	Thigs to do in this step:
	 	Refering to usb_skeleton.c and ml_driver.c 
		- identify the endpoints Attribute and direction
		- allocate memory to input endpoint buffer as its IN so from device to Host, so we require buffer at host side.
	  */


	 // allocate the device private structure a memory
	usb_stick = kzalloc(sizeof(USB_S),GFP_KERNEL);
	if(!usb_stick)
	{	
		printk(KERN_INFO"out of memory");
		return -ENOMEM;
	}

	usb_stick->udev = interface_to_usbdev(intf);
	usb_stick->interface = intf;
	
	// assign cur_altsetting to the host interface descriptor
	h_interface = intf->cur_altsetting;

	// find the number of endpoints associated with the interface
	// bNumEndpoints denotes the number of endpoints of the interface
	printk(KERN_INFO"Number of endpoints: %d",h_interface->desc.bNumEndpoints);
	for (i=0;i<h_interface->desc.bNumEndpoints; i++)
	{
		// got the number of endpoints descriptor
		endpoint = &h_interface->endpoint[i].desc;
		
		// information of endpoint prior to checking...
		printk(KERN_INFO"----------------Interface information----");
		printk(KERN_INFO"h_interface->desc.bInterfaceNumber = %04X",h_interface->desc.bInterfaceNumber);
		printk(KERN_INFO"----------------endpoint information------");
		printk(KERN_INFO"[%d] endpoint->bEndpointAddress = %04X", i, endpoint->bEndpointAddress);
		printk(KERN_INFO"[%d] endpoint->bmAttributes = %04X",i, endpoint->bmAttributes);
		printk(KERN_INFO"[%d] endpoint->wMaxPacketSize = %04X",i, endpoint->wMaxPacketSize);
		
		
		/*
		// check the type and the direction of the endpoints
		if(!usb_stick->bulk_in_endpointAddr && 
			(endpoint->bEndpointAddress & USB_DIR_IN) &&
			((endpoint->bmAttribute & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK))

		{
			// if all above is true then this is the bilk IN endpoint
			usb_stick->bulk_in_endpointAddr = endpoint->bEndpointAddress;
			buffer_size = le16_to_cpu(endpoint->wMaxPacketSize);
			usb_stick->bulk_in_size = buffer_size;
			usb_stick->bulk_in_buffer = kmalloc(buffer_size, GFP_KERNEL);
			if(!usb_stick->bulk_in_buffer) {
				printk(KERN_INFO"could not allocate the memory");
				return -ENOMEM;
			}
		}				

		// similarly check for the bulk out endpoint 
		if (!usb_stick->bulk_out_endpointAddr &&
			(endpoint->bEndpointAddress & USB_DIR_OUT) && 
			((endpoint->bmAttribute & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK))
		{
			//  if all the above conditiones are true then we have fount the bulk out endpoint
			usb_stick->bulk_out_endpointAddr = endpoint->bEndpointAddress;
				
		}
		*/
		/* This check is wasy to understand but it has more simple way of doing mentioned below */

		if(usb_endpoint_xfer_bulk(endpoint)) 
		{
			if(usb_endpoint_dir_in(endpoint)) 
			{
				usb_stick->bulk_in_endpointAddr = endpoint->bEndpointAddress;
				buffer_size = le16_to_cpu(endpoint->wMaxPacketSize);
				usb_stick->bulk_in_size = buffer_size;
				usb_stick->bulk_in_buffer = kmalloc(buffer_size, GFP_KERNEL);
				if (!usb_stick->bulk_in_buffer)
				{
					printk(KERN_INFO"could not allocate memory");
					return -ENOMEM;
				}
			}
			else
			{
				usb_stick->bulk_out_endpointAddr = endpoint->bEndpointAddress;
			}
		}
	}
	
	// set the current interface as the usb_device interface...
	usb_set_intfdata(intf, usb_stick);

	// register the device with the USB subsystem
	ret = usb_register_dev(intf, &usb_stick_driver_class);
	if(!ret)
	{
		printk(KERN_INFO"minor number cant be allocated");
		return 0;
	}
	
	dev_info(&intf->dev,"USB stick driver plugged in to USB_STICK %d",intf->minor);
	printk(KERN_INFO"device with credentials (%04X:%04X)",id->idVendor, id->idProduct);

	return 0;
}

static void usb_stick_disconnect(struct usb_interface *intf)
{	
	USB_S *usb_stick;

	usb_stick = usb_get_intfdata(intf);
	usb_set_intfdata(intf, NULL);

	// unregister the device take back the minor number
	usb_deregister_dev(intf, &usb_stick_driver_class);

	dev_info(&intf->dev, "USB stick device disconnected #%d",intf->minor);
	printk(KERN_INFO"USB stick unplugged");
}

static struct usb_driver usb_stick_driver = {
	.name = 	"usb_stick_driver",
	.id_table = 	usb_id_table,
	.probe = 	usb_stick_probe,
	.disconnect = 	usb_stick_disconnect,
};

static int __init usb_dev_init(void)
{
	int ret;
	/* register this driver with usb subsystem */
	ret = usb_register(&usb_stick_driver);
	if(ret)
	{
		err("usb_register failed, Error num %d",ret);
		printk(KERN_NOTICE"usb registration failed");
	}
	return ret;
}

static void __exit usb_dev_cleanup(void)
{
	/* de-register this driver with the usb subsystem */
	usb_deregister(&usb_stick_driver);
}

module_init(usb_dev_init);
module_exit(usb_dev_cleanup);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Bhushan R Balapure");
MODULE_DESCRIPTION("USB DEVICE DRIVER");

