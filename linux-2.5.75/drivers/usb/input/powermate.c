/*
 * A driver for the Griffin Technology, Inc. "PowerMate" USB controller dial.
 *
 * v1.1, (c)2002 William R Sowerbutts <will@sowerbutts.com>
 *
 * This device is a anodised aluminium knob which connects over USB. It can measure
 * clockwise and anticlockwise rotation. The dial also acts as a pushbutton with
 * a spring for automatic release. The base contains a pair of LEDs which illuminate
 * the translucent base. It rotates without limit and reports its relative rotation
 * back to the host when polled by the USB controller.
 *
 * Testing with the knob I have has shown that it measures approximately 94 "clicks"
 * for one full rotation. Testing with my High Speed Rotation Actuator (ok, it was 
 * a variable speed cordless electric drill) has shown that the device can measure
 * speeds of up to 7 clicks either clockwise or anticlockwise between pollings from
 * the host. If it counts more than 7 clicks before it is polled, it will wrap back
 * to zero and start counting again. This was at quite high speed, however, almost
 * certainly faster than the human hand could turn it. Griffin say that it loses a
 * pulse or two on a direction change; the granularity is so fine that I never
 * noticed this in practice.
 *
 * The device's microcontroller can be programmed to set the LED to either a constant
 * intensity, or to a rhythmic pulsing. Several patterns and speeds are available.
 *
 * Griffin were very happy to provide documentation and free hardware for development.
 *
 * Some userspace tools are available on the web: http://sowerbutts.com/powermate/
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb.h>

#define POWERMATE_VENDOR	0x077d	/* Griffin Technology, Inc. */
#define POWERMATE_PRODUCT_NEW	0x0410	/* Griffin PowerMate */
#define POWERMATE_PRODUCT_OLD	0x04AA	/* Griffin soundKnob */

#define CONTOUR_VENDOR		0x05f3	/* Contour Design, Inc. */
#define CONTOUR_JOG		0x0240	/* Jog and Shuttle */

/* these are the command codes we send to the device */
#define SET_STATIC_BRIGHTNESS  0x01
#define SET_PULSE_ASLEEP       0x02
#define SET_PULSE_AWAKE        0x03
#define SET_PULSE_MODE         0x04

/* these refer to bits in the powermate_device's requires_update field. */
#define UPDATE_STATIC_BRIGHTNESS (1<<0)
#define UPDATE_PULSE_ASLEEP      (1<<1)
#define UPDATE_PULSE_AWAKE       (1<<2)
#define UPDATE_PULSE_MODE        (1<<3)

#define POWERMATE_PAYLOAD_SIZE 3
struct powermate_device {
	signed char *data;
	dma_addr_t data_dma;
	struct urb *irq, *config;
	struct usb_ctrlrequest *configcr;
	dma_addr_t configcr_dma;
	struct usb_device *udev;
	struct input_dev input;
	struct semaphore lock;
	int static_brightness;
	int pulse_speed;
	int pulse_table;
	int pulse_asleep;
	int pulse_awake;
	int requires_update; // physical settings which are out of sync
	char phys[64];
};

static char pm_name_powermate[] = "Griffin PowerMate";
static char pm_name_soundknob[] = "Griffin SoundKnob";

static void powermate_config_complete(struct urb *urb, struct pt_regs *regs);

/* Callback for data arriving from the PowerMate over the USB interrupt pipe */
static void powermate_irq(struct urb *urb, struct pt_regs *regs)
{
	struct powermate_device *pm = urb->context;
	int retval;

	switch (urb->status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dbg("%s - urb shutting down with status: %d", __FUNCTION__, urb->status);
		return;
	default:
		dbg("%s - nonzero urb status received: %d", __FUNCTION__, urb->status);
		goto exit;
	}

	/* handle updates to device state */
	input_regs(&pm->input, regs);
	input_report_key(&pm->input, BTN_0, pm->data[0] & 0x01);
	input_report_rel(&pm->input, REL_DIAL, pm->data[1]);
	input_sync(&pm->input);

exit:
	retval = usb_submit_urb (urb, GFP_ATOMIC);
	if (retval)
		err ("%s - usb_submit_urb failed with result %d",
		     __FUNCTION__, retval);
}

/* Decide if we need to issue a control message and do so. Must be called with pm->lock down */
static void powermate_sync_state(struct powermate_device *pm)
{
	if (pm->requires_update == 0) 
		return; /* no updates are required */
	if (pm->config->status == -EINPROGRESS) 
		return; /* an update is already in progress; it'll issue this update when it completes */

	if (pm->requires_update & UPDATE_PULSE_ASLEEP){
		pm->configcr->wValue = cpu_to_le16( SET_PULSE_ASLEEP );
		pm->configcr->wIndex = cpu_to_le16( pm->pulse_asleep ? 1 : 0 );
		pm->requires_update &= ~UPDATE_PULSE_ASLEEP;
	}else if (pm->requires_update & UPDATE_PULSE_AWAKE){
		pm->configcr->wValue = cpu_to_le16( SET_PULSE_AWAKE );
		pm->configcr->wIndex = cpu_to_le16( pm->pulse_awake ? 1 : 0 );
		pm->requires_update &= ~UPDATE_PULSE_AWAKE;
	}else if (pm->requires_update & UPDATE_PULSE_MODE){
		int op, arg;
		/* the powermate takes an operation and an argument for its pulse algorithm.
		   the operation can be:
		   0: divide the speed
		   1: pulse at normal speed
		   2: multiply the speed
		   the argument only has an effect for operations 0 and 2, and ranges between
		   1 (least effect) to 255 (maximum effect).
       
		   thus, several states are equivalent and are coalesced into one state.

		   we map this onto a range from 0 to 510, with:
		   0 -- 254    -- use divide (0 = slowest)
		   255         -- use normal speed
		   256 -- 510  -- use multiple (510 = fastest).

		   Only values of 'arg' quite close to 255 are particularly useful/spectacular.
		*/    
		if (pm->pulse_speed < 255){
			op = 0;                   // divide
			arg = 255 - pm->pulse_speed;
		} else if (pm->pulse_speed > 255){
			op = 2;                   // multiply
			arg = pm->pulse_speed - 255;
		} else {
			op = 1;                   // normal speed
			arg = 0;                  // can be any value
		}
		pm->configcr->wValue = cpu_to_le16( (pm->pulse_table << 8) | SET_PULSE_MODE );
		pm->configcr->wIndex = cpu_to_le16( (arg << 8) | op );
		pm->requires_update &= ~UPDATE_PULSE_MODE;
	}else if (pm->requires_update & UPDATE_STATIC_BRIGHTNESS){
		pm->configcr->wValue = cpu_to_le16( SET_STATIC_BRIGHTNESS );
		pm->configcr->wIndex = cpu_to_le16( pm->static_brightness );
		pm->requires_update &= ~UPDATE_STATIC_BRIGHTNESS;
	}else{
		printk(KERN_ERR "powermate: unknown update required");
		pm->requires_update = 0; /* fudge the bug */
		return;
	}

/*	printk("powermate: %04x %04x\n", pm->configcr->wValue, pm->configcr->wIndex); */

	pm->configcr->bRequestType = 0x41; /* vendor request */
	pm->configcr->bRequest = 0x01;
	pm->configcr->wLength = 0;

	usb_fill_control_urb(pm->config, pm->udev, usb_sndctrlpipe(pm->udev, 0),
			     (void *) pm->configcr, 0, 0,
			     powermate_config_complete, pm);
	pm->config->setup_dma = pm->configcr_dma;
	pm->config->transfer_flags |= URB_NO_SETUP_DMA_MAP;

	if (usb_submit_urb(pm->config, GFP_ATOMIC))
		printk(KERN_ERR "powermate: usb_submit_urb(config) failed");
}

/* Called when our asynchronous control message completes. We may need to issue another immediately */
static void powermate_config_complete(struct urb *urb, struct pt_regs *regs)
{
	struct powermate_device *pm = urb->context;

	if (urb->status)
		printk(KERN_ERR "powermate: config urb returned %d\n", urb->status);
	
	down(&pm->lock);
	powermate_sync_state(pm);
	up(&pm->lock);
}

/* Set the LED up as described and begin the sync with the hardware if required */
static void powermate_pulse_led(struct powermate_device *pm, int static_brightness, int pulse_speed, 
				int pulse_table, int pulse_asleep, int pulse_awake)
{
	if (pulse_speed < 0)
		pulse_speed = 0;
	if (pulse_table < 0)
		pulse_table = 0;
	if (pulse_speed > 510)
		pulse_speed = 510;
	if (pulse_table > 2)
		pulse_table = 2;

	pulse_asleep = !!pulse_asleep;
	pulse_awake = !!pulse_awake;

	down(&pm->lock);

	/* mark state updates which are required */
	if (static_brightness != pm->static_brightness){
		pm->static_brightness = static_brightness;
		pm->requires_update |= UPDATE_STATIC_BRIGHTNESS;		
	}
	if (pulse_asleep != pm->pulse_asleep){
		pm->pulse_asleep = pulse_asleep;
		pm->requires_update |= (UPDATE_PULSE_ASLEEP | UPDATE_STATIC_BRIGHTNESS);
	}
	if (pulse_awake != pm->pulse_awake){
		pm->pulse_awake = pulse_awake;
		pm->requires_update |= (UPDATE_PULSE_AWAKE | UPDATE_STATIC_BRIGHTNESS);
	}
	if (pulse_speed != pm->pulse_speed || pulse_table != pm->pulse_table){
		pm->pulse_speed = pulse_speed;
		pm->pulse_table = pulse_table;
		pm->requires_update |= UPDATE_PULSE_MODE;
	}

	powermate_sync_state(pm);
   
	up(&pm->lock);
}

/* Callback from the Input layer when an event arrives from userspace to configure the LED */
static int powermate_input_event(struct input_dev *dev, unsigned int type, unsigned int code, int _value)
{
	unsigned int command = (unsigned int)_value;
	struct powermate_device *pm = dev->private;

	if (type == EV_MSC && code == MSC_PULSELED){
		/*  
		    bits  0- 7: 8 bits: LED brightness
		    bits  8-16: 9 bits: pulsing speed modifier (0 ... 510); 0-254 = slower, 255 = standard, 256-510 = faster.
		    bits 17-18: 2 bits: pulse table (0, 1, 2 valid)
		    bit     19: 1 bit : pulse whilst asleep?
		    bit     20: 1 bit : pulse constantly?
		*/  
		int static_brightness = command & 0xFF;   // bits 0-7
		int pulse_speed = (command >> 8) & 0x1FF; // bits 8-16
		int pulse_table = (command >> 17) & 0x3;  // bits 17-18
		int pulse_asleep = (command >> 19) & 0x1; // bit 19
		int pulse_awake  = (command >> 20) & 0x1; // bit 20
  
		powermate_pulse_led(pm, static_brightness, pulse_speed, pulse_table, pulse_asleep, pulse_awake);
	}

	return 0;
}

static int powermate_alloc_buffers(struct usb_device *udev, struct powermate_device *pm)
{
	pm->data = usb_buffer_alloc(udev, POWERMATE_PAYLOAD_SIZE,
				    SLAB_ATOMIC, &pm->data_dma);
	if (!pm->data)
		return -1;
	pm->configcr = usb_buffer_alloc(udev, sizeof(*(pm->configcr)),
					SLAB_ATOMIC, &pm->configcr_dma);
	if (!pm->configcr)
		return -1;

	return 0;
}

static void powermate_free_buffers(struct usb_device *udev, struct powermate_device *pm)
{
	if (pm->data)
		usb_buffer_free(udev, POWERMATE_PAYLOAD_SIZE,
				pm->data, pm->data_dma);
	if (pm->configcr)
		usb_buffer_free(udev, sizeof(*(pm->configcr)),
				pm->configcr, pm->configcr_dma);
}

/* Called whenever a USB device matching one in our supported devices table is connected */
static int powermate_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev (intf);
	struct usb_host_interface *interface;
	struct usb_endpoint_descriptor *endpoint;
	struct powermate_device *pm;
	int pipe, maxp;
	char path[64];

	interface = intf->altsetting + 0;
	endpoint = &interface->endpoint[0].desc;
	if (!(endpoint->bEndpointAddress & 0x80))
		return -EIO;
	if ((endpoint->bmAttributes & 3) != 3)
		return -EIO;

	usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
		0x0a, USB_TYPE_CLASS | USB_RECIP_INTERFACE,
		0, interface->desc.bInterfaceNumber, NULL, 0,
		HZ * USB_CTRL_SET_TIMEOUT);

	if (!(pm = kmalloc(sizeof(struct powermate_device), GFP_KERNEL)))
		return -ENOMEM;

	memset(pm, 0, sizeof(struct powermate_device));
	pm->udev = udev;

	if (powermate_alloc_buffers(udev, pm)) {
		powermate_free_buffers(udev, pm);
		kfree(pm);
		return -ENOMEM;
	}

	pm->irq = usb_alloc_urb(0, GFP_KERNEL);
	if (!pm->irq) {
		powermate_free_buffers(udev, pm);
		kfree(pm);
		return -ENOMEM;
	}

	pm->config = usb_alloc_urb(0, GFP_KERNEL);
	if (!pm->config) {
		usb_free_urb(pm->irq);
		powermate_free_buffers(udev, pm);
		kfree(pm);
		return -ENOMEM;
	}

	init_MUTEX(&pm->lock);
	init_input_dev(&pm->input);

	/* get a handle to the interrupt data pipe */
	pipe = usb_rcvintpipe(udev, endpoint->bEndpointAddress);
	maxp = usb_maxpacket(udev, pipe, usb_pipeout(pipe));

	if (maxp != POWERMATE_PAYLOAD_SIZE)
		printk("powermate: Expected payload of %d bytes, found %d bytes!\n", POWERMATE_PAYLOAD_SIZE, maxp);


	usb_fill_int_urb(pm->irq, udev, pipe, pm->data,
			 POWERMATE_PAYLOAD_SIZE, powermate_irq,
			 pm, endpoint->bInterval);
	pm->irq->transfer_dma = pm->data_dma;
	pm->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	/* register our interrupt URB with the USB system */
	if (usb_submit_urb(pm->irq, GFP_KERNEL)) {
		powermate_free_buffers(udev, pm);
		kfree(pm);
		return -EIO; /* failure */
	}

	switch (udev->descriptor.idProduct) {
	case POWERMATE_PRODUCT_NEW: pm->input.name = pm_name_powermate; break;
	case POWERMATE_PRODUCT_OLD: pm->input.name = pm_name_soundknob; break;
	default: 
	  pm->input.name = pm_name_soundknob;
	  printk(KERN_WARNING "powermate: unknown product id %04x\n", udev->descriptor.idProduct);
	}

	pm->input.private = pm;
	pm->input.evbit[0] = BIT(EV_KEY) | BIT(EV_REL) | BIT(EV_MSC);
	pm->input.keybit[LONG(BTN_0)] = BIT(BTN_0);
	pm->input.relbit[LONG(REL_DIAL)] = BIT(REL_DIAL);
	pm->input.mscbit[LONG(MSC_PULSELED)] = BIT(MSC_PULSELED);
	pm->input.id.bustype = BUS_USB;
	pm->input.id.vendor = udev->descriptor.idVendor;
	pm->input.id.product = udev->descriptor.idProduct;
	pm->input.id.version = udev->descriptor.bcdDevice;
	pm->input.event = powermate_input_event;

	input_register_device(&pm->input);

	usb_make_path(udev, path, 64);
	snprintf(pm->phys, 64, "%s/input0", path);
	printk(KERN_INFO "input: %s on %s\n", pm->input.name, pm->input.phys);
	
	/* force an update of everything */
	pm->requires_update = UPDATE_PULSE_ASLEEP | UPDATE_PULSE_AWAKE | UPDATE_PULSE_MODE | UPDATE_STATIC_BRIGHTNESS;
	powermate_pulse_led(pm, 0x80, 255, 0, 1, 0); // set default pulse parameters
  
	usb_set_intfdata(intf, pm);
	return 0;
}

/* Called when a USB device we've accepted ownership of is removed */
static void powermate_disconnect(struct usb_interface *intf)
{
	struct powermate_device *pm = usb_get_intfdata (intf);

	usb_set_intfdata(intf, NULL);
	if (pm) {
		down(&pm->lock);
		pm->requires_update = 0;
		usb_unlink_urb(pm->irq);
		input_unregister_device(&pm->input);
		usb_free_urb(pm->irq);
		usb_free_urb(pm->config);
		powermate_free_buffers(interface_to_usbdev(intf), pm);

		kfree(pm);
	}
}

static struct usb_device_id powermate_devices [] = {
	{ USB_DEVICE(POWERMATE_VENDOR, POWERMATE_PRODUCT_NEW) },
	{ USB_DEVICE(POWERMATE_VENDOR, POWERMATE_PRODUCT_OLD) },
	{ USB_DEVICE(CONTOUR_VENDOR, CONTOUR_JOG) },
	{ } /* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, powermate_devices);

static struct usb_driver powermate_driver = {
	.owner =	THIS_MODULE,
        .name =         "powermate",
        .probe =        powermate_probe,
        .disconnect =   powermate_disconnect,
        .id_table =     powermate_devices,
};

int powermate_init(void)
{
	if (usb_register(&powermate_driver) < 0)
		return -1;
	return 0;
}

void powermate_cleanup(void)
{
	usb_deregister(&powermate_driver);
}

module_init(powermate_init);
module_exit(powermate_cleanup);

MODULE_AUTHOR( "William R Sowerbutts" );
MODULE_DESCRIPTION( "Griffin Technology, Inc PowerMate driver" );
MODULE_LICENSE("GPL");
