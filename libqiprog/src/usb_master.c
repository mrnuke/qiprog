/*
 * qiprog - Reference implementation of the QiProg protocol
 *
 * Copyright (C) 2013 Alexandru Gagniuc <mr.nuke.me@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @defgroup usb_master_file QiProg USB host driver
 *
 * @ingroup qiprog_drivers
 *
 * @author @htmlonly &copy; @endhtmlonly 2013 Alexandru Gagniuc <mr.nuke.me@gmail.com>
 *
 * @brief <b>QiProg USB host driver</b>
 *
 * This file contains the host-side driver for communicating with USB QiProg
 * devices. The driver serializes QiProg calls to messages over the USB bus.
 */

/** @{ */

#include <qiprog_usb_host.h>
#include "qiprog_internal.h"

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#define LOG_DOMAIN "usb_host: "
#define qi_err(str, ...)	qi_perr(LOG_DOMAIN str,  ##__VA_ARGS__)
#define qi_warn(str, ...)	qi_pwarn(LOG_DOMAIN str, ##__VA_ARGS__)
#define qi_info(str, ...)	qi_pinfo(LOG_DOMAIN str, ##__VA_ARGS__)
#define qi_dbg(str, ...)	qi_pdbg(LOG_DOMAIN str,  ##__VA_ARGS__)
#define qi_spew(str, ...)	qi_pspew(LOG_DOMAIN str, ##__VA_ARGS__)

struct qiprog_driver qiprog_usb_master_drv;

/**
 * @brief Private per-device context for USB devices
 */
struct usb_master_priv {
	libusb_device_handle *handle;
	libusb_device *usb_dev;
};

/**
 * @brief Helper to create a new USB QiProg device
 */
static struct qiprog_device *new_usb_prog(libusb_device *libusb_dev,
					  struct qiprog_context *ctx)
{
	/*
	 * Peter Stuge is the person who started it all. He is also the de-facto
	 * USB expert for free software hackers to go to with questions. As a
	 * result, every QiProg device connected via USB shall be named after
	 * him.
	 */
	struct qiprog_device *peter_stuge;
	struct usb_master_priv *priv;

	peter_stuge = qiprog_new_device(ctx);

	if (peter_stuge == NULL)
		return NULL;

	if ((priv = malloc(sizeof(*priv))) == NULL) {
		qiprog_free_device(peter_stuge);
		qi_warn("Could not allocate memory for device. Aborting");
		return NULL;
	}

	peter_stuge->drv = &qiprog_usb_master_drv;
	priv->usb_dev = libusb_dev;
	/* Don't create a handle until the device is opened */
	priv->handle = NULL;
	peter_stuge->priv = priv;

	return peter_stuge;
}

/**
 * @brief Decide if given USB device is a QiProg device
 */
static bool is_interesting(libusb_device *dev)
{
	int ret;
	struct libusb_device_descriptor descr;

	ret = libusb_get_device_descriptor(dev, &descr);
	if (ret != LIBUSB_SUCCESS) {
		qi_warn("Could not get descriptor: %s", libusb_error_name(ret));
		return false;
	}

	if ((descr.idVendor != USB_VID_OPENMOKO) ||
	    (descr.idProduct != USB_PID_OPENMOKO_VULTUREPROG)) {
		return false;
	}

	return true;
}

/**
 * @brief QiProg driver 'scan' member
 */
qiprog_err scan(struct qiprog_context *ctx, struct dev_list *qi_list)
{
	libusb_device **list;
	libusb_device *device;
	struct qiprog_device *qi_dev;
	ssize_t cnt;
	ssize_t i;

	/* Discover devices */
	cnt = libusb_get_device_list(NULL, &list);

	/* Not finding any devices is not an error */
	if (cnt < 0)
		return QIPROG_SUCCESS;

	for (i = 0; i < cnt; i++) {
		device = list[i];
		if (is_interesting(device)) {
			qi_dev = new_usb_prog(device, ctx);
			if (qi_dev == NULL) {
				libusb_free_device_list(list, 1);
				qi_err("Malloc failure");
				return QIPROG_ERR_MALLOC;
			}
			dev_list_append(qi_list, qi_dev);
		}
	}

	libusb_free_device_list(list, 0);
	return QIPROG_SUCCESS;
}

/**
 * @brief QiProg driver 'dev_open' member
 */
static qiprog_err dev_open(struct qiprog_device *dev)
{
	int ret;
	struct usb_master_priv *priv;

	if (!dev)
		return QIPROG_ERR_ARG;
	if (!(priv = dev->priv))
		return QIPROG_ERR_ARG;

	ret = libusb_open(priv->usb_dev, &(priv->handle));
	if (ret != LIBUSB_SUCCESS) {
		qi_err("Could not open device: %s", libusb_error_name(ret));
		return QIPROG_ERR;
	}

	ret = libusb_claim_interface(priv->handle, 0);
	if (ret != LIBUSB_SUCCESS) {
		qi_warn("Could not claim interface: %s",
			libusb_error_name(ret));
		return QIPROG_ERR;
	}

	return QIPROG_SUCCESS;
}

/**
 * @brief QiProg driver 'get_capabilities' member
 */
static qiprog_err get_capabilities(struct qiprog_device *dev,
				   struct qiprog_capabilities *caps)
{
	int ret, i;
	uint8_t buf[64];
	struct usb_master_priv *priv;
	struct qiprog_capabilities *le_caps;

	if (!dev)
		return QIPROG_ERR_ARG;
	if (!(priv = dev->priv))
		return QIPROG_ERR_ARG;

	ret = libusb_control_transfer(priv->handle, 0xc0,
				      QIPROG_GET_CAPABILITIES, 0, 0,
				      (void *)buf, sizeof(*caps), 3000);
	if (ret < LIBUSB_SUCCESS) {
		qi_err("Control transfer failed: %s", libusb_error_name(ret));
		return QIPROG_ERR;
	}

	/* USB is LE, we are host-endian */
	le_caps = (void *)buf;
	caps->bus_master = le32_to_h(&(le_caps->bus_master));
	caps->instruction_set = le16_to_h(&(le_caps->instruction_set));
	caps->max_direct_data = le32_to_h(&(le_caps->max_direct_data));
	for (i = 0; i < 10; i++)
		caps->voltages[i] = le16_to_h(&(le_caps->voltages[i]));

	return QIPROG_SUCCESS;
}

static qiprog_err set_bus(struct qiprog_device *dev, enum qiprog_bus bus)
{
	int ret;
	uint16_t wValue, wIndex;
	struct usb_master_priv *priv;

	if (!dev)
		return QIPROG_ERR_ARG;
	if (!(priv = dev->priv))
		return QIPROG_ERR_ARG;
	if (!bus)
		return QIPROG_ERR_ARG;

	/* Most significant 16 bits of the QIPROG_BUS_ constant */
	wValue = bus >> 16;
	/* Least significant 16 bits of the QIPROG_BUS_ constant */
	wIndex = bus & 0xffff;

	/*
	 * FIXME: This doesn't seem to return an error when the device NAKs the
	 * request.
	 */
	ret = libusb_control_transfer(priv->handle, 0x40,
				      QIPROG_SET_BUS, wValue, wIndex,
				      NULL, 0, 3000);
	if (ret < LIBUSB_SUCCESS) {
		qi_err("Control transfer failed: %s", libusb_error_name(ret));
		return QIPROG_ERR;
	}

	return QIPROG_SUCCESS;
}

/**
 * @brief QiProg driver 'read_chip_id' member
 */
static qiprog_err read_chip_id(struct qiprog_device *dev,
			       struct qiprog_chip_id ids[9])
{
	int ret, i;
	uint8_t buf[64];
	struct usb_master_priv *priv;
	struct qiprog_chip_id *le_ids;

	if (!dev)
		return QIPROG_ERR_ARG;
	if (!(priv = dev->priv))
		return QIPROG_ERR_ARG;

	ret = libusb_control_transfer(priv->handle, 0xc0,
				      QIPROG_READ_DEVICE_ID, 0, 0,
				      (void *)buf, sizeof(*ids) * 9, 3000);
	if (ret < LIBUSB_SUCCESS) {
		qi_err("Control transfer failed: %s", libusb_error_name(ret));
		return QIPROG_ERR;
	}

	/* USB is LE, we are host-endian */
	le_ids = (void *)buf;
	for (i = 0; i < 9; i++) {
		ids[i].id_method = le_ids[i].id_method;
		ids[i].vendor_id = le16_to_h(&(le_ids[i].vendor_id));
		ids[i].device_id = le32_to_h(&(le_ids[i].device_id));
	}

	return QIPROG_SUCCESS;
}

/**
 * @brief QiProg driver 'read8' member
 *
 * TODO: Try to unify read 8/16/32 into one common function
 */
static qiprog_err read8(struct qiprog_device *dev, uint32_t addr,
			uint8_t *data)
{
	int ret;
	uint16_t wValue, wIndex;
	struct usb_master_priv *priv;

	if (!dev)
		return QIPROG_ERR_ARG;
	if (!(priv = dev->priv))
		return QIPROG_ERR_ARG;

	/* Most significant 16 bits of the memory address to read from */
	wValue = addr >> 16;
	/* Least significant 16 bits of the memory address to read from */
	wIndex = addr & 0xffff;

	ret = libusb_control_transfer(priv->handle, 0xc0,
				      QIPROG_READ8, wValue, wIndex,
				      (void *)data, sizeof(*data), 3000);
	if (ret < LIBUSB_SUCCESS) {
		qi_err("Control transfer failed: %s", libusb_error_name(ret));
		return QIPROG_ERR;
	}

	return QIPROG_SUCCESS;
}

/**
 * @brief QiProg driver 'read16' member
 */
static qiprog_err read16(struct qiprog_device *dev, uint32_t addr,
			 uint16_t *data)
{
	int ret;
	uint8_t buf[64];
	uint16_t wValue, wIndex;
	struct usb_master_priv *priv;

	if (!dev)
		return QIPROG_ERR_ARG;
	if (!(priv = dev->priv))
		return QIPROG_ERR_ARG;

	/* Most significant 16 bits of the memory address to read from */
	wValue = addr >> 16;
	/* Least significant 16 bits of the memory address to read from */
	wIndex = addr & 0xffff;

	ret = libusb_control_transfer(priv->handle, 0xc0,
				      QIPROG_READ16, wValue, wIndex,
				      (void *)buf, sizeof(*data), 3000);
	if (ret < LIBUSB_SUCCESS) {
		qi_err("Control transfer failed: %s", libusb_error_name(ret));
		return QIPROG_ERR;
	}

	/* USB is LE, we are host-endian */
	*data = le16_to_h(buf);

	return QIPROG_SUCCESS;
}

/**
 * @brief QiProg driver 'read32' member
 */
static qiprog_err read32(struct qiprog_device *dev, uint32_t addr,
			 uint32_t *data)
{
	int ret;
	uint8_t buf[64];
	uint16_t wValue, wIndex;
	struct usb_master_priv *priv;

	if (!dev)
		return QIPROG_ERR_ARG;
	if (!(priv = dev->priv))
		return QIPROG_ERR_ARG;

	/* Most significant 16 bits of the memory address to read from */
	wValue = addr >> 16;
	/* Least significant 16 bits of the memory address to read from */
	wIndex = addr & 0xffff;

	ret = libusb_control_transfer(priv->handle, 0xc0,
				      QIPROG_READ32, wValue, wIndex,
				      (void *)buf, sizeof(*data), 3000);
	if (ret < LIBUSB_SUCCESS) {
		qi_err("Control transfer failed: %s", libusb_error_name(ret));
		return QIPROG_ERR;
	}

	/* USB is LE, we are host-endian */
	*data = le32_to_h(buf);

	return QIPROG_SUCCESS;
}

/**
 * @brief QiProg driver 'write8' member
 *
 * TODO: Try to unify write 8/16/32 into one common function
 */
static qiprog_err write8(struct qiprog_device *dev, uint32_t addr, uint8_t data)
{
	int ret;
	uint16_t wValue, wIndex;
	struct usb_master_priv *priv;

	if (!dev)
		return QIPROG_ERR_ARG;
	if (!(priv = dev->priv))
		return QIPROG_ERR_ARG;

	/* Most significant 16 bits of the memory address to read from */
	wValue = addr >> 16;
	/* Least significant 16 bits of the memory address to read from */
	wIndex = addr & 0xffff;

	ret = libusb_control_transfer(priv->handle, 0x40,
				      QIPROG_WRITE8, wValue, wIndex,
				      (void *)&data, sizeof(data), 3000);
	if (ret < LIBUSB_SUCCESS) {
		qi_err("Control transfer failed: %s", libusb_error_name(ret));
		return QIPROG_ERR;
	}

	return QIPROG_SUCCESS;
}

/**
 * @brief QiProg driver 'write16' member
 */
static qiprog_err write16(struct qiprog_device *dev, uint32_t addr,
			  uint16_t data)
{
	int ret;
	uint8_t buf[64];
	uint16_t wValue, wIndex;
	struct usb_master_priv *priv;

	if (!dev)
		return QIPROG_ERR_ARG;
	if (!(priv = dev->priv))
		return QIPROG_ERR_ARG;

	/* Most significant 16 bits of the memory address to read from */
	wValue = addr >> 16;
	/* Least significant 16 bits of the memory address to read from */
	wIndex = addr & 0xffff;

	/* USB is LE, we are host-endian */
	h_to_le16(data, buf);

	ret = libusb_control_transfer(priv->handle, 0x40,
				      QIPROG_WRITE16, wValue, wIndex,
				      (void *)buf, sizeof(data), 3000);
	if (ret < LIBUSB_SUCCESS) {
		qi_err("Control transfer failed: %s", libusb_error_name(ret));
		return QIPROG_ERR;
	}

	return QIPROG_SUCCESS;
}

/**
 * @brief QiProg driver 'write32' member
 */
static qiprog_err write32(struct qiprog_device *dev, uint32_t addr,
			  uint32_t data)
{
	int ret;
	uint8_t buf[64];
	uint16_t wValue, wIndex;
	struct usb_master_priv *priv;

	if (!dev)
		return QIPROG_ERR_ARG;
	if (!(priv = dev->priv))
		return QIPROG_ERR_ARG;

	/* Most significant 16 bits of the memory address to read from */
	wValue = addr >> 16;
	/* Least significant 16 bits of the memory address to read from */
	wIndex = addr & 0xffff;

	/* USB is LE, we are host-endian */
	h_to_le32(data, buf);

	ret = libusb_control_transfer(priv->handle, 0x40,
				      QIPROG_WRITE32, wValue, wIndex,
				      (void *)buf, sizeof(data), 3000);
	if (ret < LIBUSB_SUCCESS) {
		qi_err("Control transfer failed: %s", libusb_error_name(ret));
		return QIPROG_ERR;
	}

	return QIPROG_SUCCESS;
}

/**
 * @brief QiProg driver 'set_address' member
 */
static qiprog_err set_address(struct qiprog_device *dev, uint32_t start,
			      uint32_t end)
{
	int ret;
	uint8_t buf[64];
	struct usb_master_priv *priv;
	struct qiprog_address *le_addrs;

	if (!dev)
		return QIPROG_ERR_ARG;
	if (!(priv = dev->priv))
		return QIPROG_ERR_ARG;

	qi_spew("Setting address range 0x%.8lx -> 0x%.8lx\n", start, end);

	/* USB is LE, we are host-endian */
	le_addrs = (void *)buf;
	h_to_le32(start, &(le_addrs->start_address));
	h_to_le32(end, &(le_addrs->max_address));

	ret = libusb_control_transfer(priv->handle, 0x40,
				      QIPROG_SET_ADDRESS, 0, 0,
			              (void *)buf, sizeof(*le_addrs), 3000);
	if (ret < LIBUSB_SUCCESS) {
		qi_err("Control transfer failed: %s", libusb_error_name(ret));
		return QIPROG_ERR;
	}

	return QIPROG_SUCCESS;
}

/**
 * @brief The actual USB host driver structure
 */
struct qiprog_driver qiprog_usb_master_drv = {
	.scan = scan,
	.dev_open = dev_open,
	.set_bus = set_bus,
	.get_capabilities = get_capabilities,
	.read_chip_id = read_chip_id,
	.set_address = set_address,
	.read8 = read8,
	.read16 = read16,
	.read32 = read32,
	.write8 = write8,
	.write16 = write16,
	.write32 = write32,
};

/** @} */
