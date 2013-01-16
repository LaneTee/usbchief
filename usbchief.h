/*
* This file is part of Buildbot.  Buildbot is free software: you can
* redistribute it and/or modify it under the terms of the GNU General Public
* License as published by the Free Software Foundation, version 2.
*
* This program is distributed in the hope that it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
* FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
* details.
*
* You should have received a copy of the GNU General Public License along with
* this program; if not, write to the Free Software Foundation, Inc., 51
* Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*
* Copyright (c) 2012 Sven Schnelle <svens@stackframe.org>
*/

#ifndef __USBCHIEF_H
#define __USBCHIEF_H

#define WPP_CONTROL_GUIDS \
		WPP_DEFINE_CONTROL_GUID(UsbChiefGUID,(0, 0, 0, 0, 0), \
			WPP_DEFINE_BIT(MP_ERROR) \
			WPP_DEFINE_BIT(MP_WARNING) \
			WPP_DEFINE_BIT(MP_TRACE) \
			WPP_DEFINE_BIT(MP_INFO) \
			WPP_DEFINE_BIT(MP_LOAD))

typedef enum {
	MP_LOUD,
	MP_INFO,
	MP_TRACE,
	MP_WARNING,
	MP_ERROR
};

typedef enum {
	DEBUG_IOCTL=1,
	DEBUG_RW=2,
	DEBUG_CONFIG=4,
	DEBUG_POWER=8
};

#ifdef DBG
#define UsbChief_DbgPrint(_level, _x) \
	if ((_level) & DebugLevel || !(_level)) {			\
		DbgPrint("%s:%d ", __FUNCTION__, __LINE__); DbgPrint _x; \
	}
#else
#define UsbChief_DbgPrint(level, _x)
#endif

#define MAX_TRANSFER_SIZE 65535

DEFINE_GUID(GUID_CLASS_USBCHIEF_USB, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

typedef struct _FILE_CONTEXT {
	WDFUSBPIPE Pipe;
} FILE_CONTEXT, *PFILE_CONTEXT;

typedef struct _REQUEST_CONTEXT {
	PMDL Mdl;
	ULONG Length;
	ULONG Numxfer;
	ULONG_PTR VirtualAddress;
	WDFMEMORY UrbMemory;
} REQUEST_CONTEXT, *PREQUEST_CONTEXT;

typedef struct _DEVICE_CONTEXT {
	USB_DEVICE_DESCRIPTOR UsbDeviceDescriptor;
	PUSB_CONFIGURATION_DESCRIPTOR UsbConfigurationDescriptor;
	BOOLEAN IsDeviceHighSpeed;
	ULONG WaitWakeEnable;
	WDFUSBDEVICE WdfUsbTargetDevice;
	WDFUSBINTERFACE UsbInterface;
	UCHAR NumberConfiguredPipes;
	ULONG MaximumTransferSize;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

typedef struct _WORKITEM_CONTEXT {
	WDFDEVICE       Device;
	WDFUSBPIPE      Pipe;
} WORKITEM_CONTEXT, *PWORKITEM_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(WORKITEM_CONTEXT, GetWorkItemContext)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(REQUEST_CONTEXT, GetRequestContext)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(FILE_CONTEXT, GetFileContext)

typedef struct _IOCTL_DATA {
	BYTE  Request;
	BYTE __RESERVED;
	WORD Value;
	WORD Index;
	WORD Length;
	DWORD Buffer;
} IOCTL_DATA, *PIOCTL_DATA;

#define POOL_TAG 0x43544143

#define IOCTL_VENDOR_WRITE CTL_CODE(FILE_DEVICE_UNKNOWN, 0, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VENDOR_READ CTL_CODE(FILE_DEVICE_UNKNOWN, 1, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SELECT_CONFIGURATION CTL_CODE(FILE_DEVICE_UNKNOWN, 2, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_GET_FIRMWARE_VERSION CTL_CODE(FILE_DEVICE_UNKNOWN, 3, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif
