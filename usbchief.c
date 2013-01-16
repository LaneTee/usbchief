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

#include <initguid.h>
#include <ntddk.h>
#include <ntintsafe.h>
#include <usbdi.h>
#include <usbdlib.h>
#include <wdf.h>
#include <wdfusb.h>
#include <usbchief.h>
#include <usbchief.tmh>

static ULONG DebugLevel = 0;//0xffffffff;

#ifdef ALLOC_PRAGMA

static NTSTATUS UsbChief_ConfigureDevice(IN WDFDEVICE Device);
static NTSTATUS UsbChief_ReadAndSelectDescriptors(IN WDFDEVICE Device);
static NTSTATUS UsbChief_SetPowerPolicy(IN WDFDEVICE Device);
static NTSTATUS UsbChief_ResetPipe(IN WDFUSBPIPE Pipe);
static NTSTATUS UsbChief_SetPowerPolicy(IN WDFDEVICE Device);
static NTSTATUS UsbChief_ReadAndSelectDescriptors(IN WDFDEVICE Device);
static NTSTATUS UsbChief_ConfigureDevice(IN WDFDEVICE Device);
static NTSTATUS UsbChief_ResetDevice(IN WDFDEVICE Device);
static NTSTATUS UsbChief_SelectInterfaces(IN WDFDEVICE Device);
static WDFUSBPIPE UsbChief_GetPipeFromName(IN PDEVICE_CONTEXT DeviceContext,
					   IN PUNICODE_STRING FileName);

static EVT_WDF_DRIVER_DEVICE_ADD UsbChief_EvtDeviceAdd;
static EVT_WDF_DEVICE_PREPARE_HARDWARE UsbChief_EvtDevicePrepareHardware;
static EVT_WDF_DEVICE_FILE_CREATE UsbChief_EvtDeviceFileCreate;
static EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL UsbChief_EvtIoDeviceControl;
static EVT_WDF_IO_QUEUE_IO_READ UsbChief_EvtIoRead;
static EVT_WDF_IO_QUEUE_IO_WRITE UsbChief_EvtIoWrite;
static EVT_WDF_IO_QUEUE_IO_STOP UsbChief_EvtIoStop;
static EVT_WDF_IO_QUEUE_IO_RESUME UsbChief_EvtIoResume;
static EVT_WDF_DRIVER_DEVICE_ADD UsbChief_EvtDeviceAdd;
static EVT_WDF_WORKITEM UsbChief_ReadWriteWorkItem;
static EVT_WDF_REQUEST_COMPLETION_ROUTINE UsbChief_ReadCompletion;

#pragma alloc_text(PAGE, UsbChief_EvtDeviceAdd)
#pragma alloc_text(PAGE, UsbChief_ConfigureDevice)
#pragma alloc_text(PAGE, UsbChief_ReadAndSelectDescriptors)
#pragma alloc_text(PAGE, UsbChief_SetPowerPolicy)
#pragma alloc_text(PAGE, UsbChief_SelectInterfaces)
#pragma alloc_text(PAGE, UsbChief_EvtDevicePrepareHardware)
#pragma alloc_text(PAGE, UsbChief_EvtDeviceFileCreate)
#pragma alloc_text(PAGE, UsbChief_EvtIoDeviceControl)
#pragma alloc_text(PAGE, UsbChief_EvtIoRead)
#pragma alloc_text(PAGE, UsbChief_EvtIoWrite)
#pragma alloc_text(PAGE, UsbChief_EvtIoStop)
#pragma alloc_text(PAGE, UsbChief_EvtIoResume)
#pragma alloc_text(PAGE, UsbChief_EvtDeviceAdd)
#pragma alloc_text(PAGE, UsbChief_ResetPipe)
#pragma alloc_text(PAGE, UsbChief_ResetDevice)
#pragma alloc_text(PAGE, UsbChief_GetPipeFromName)

#endif

static NTSTATUS UsbChief_SelectInterfaces(IN WDFDEVICE Device)
{
	WDF_USB_DEVICE_SELECT_CONFIG_PARAMS configParams;
	NTSTATUS Status;
	PDEVICE_CONTEXT pDeviceContext;

	PAGED_CODE();

	pDeviceContext = GetDeviceContext(Device);

	WDF_USB_DEVICE_SELECT_CONFIG_PARAMS_INIT_SINGLE_INTERFACE( &configParams);

	Status = WdfUsbTargetDeviceSelectConfig(pDeviceContext->WdfUsbTargetDevice,
						WDF_NO_OBJECT_ATTRIBUTES,
						&configParams);

	if (NT_SUCCESS(Status) &&
	    WdfUsbTargetDeviceGetNumInterfaces(pDeviceContext->WdfUsbTargetDevice) > 0) {

		pDeviceContext->UsbInterface =
			configParams.Types.SingleInterface.ConfiguredUsbInterface;

		pDeviceContext->NumberConfiguredPipes =
			configParams.Types.SingleInterface.NumberConfiguredPipes;
	}
	return Status;

}

static NTSTATUS UsbChief_ConfigureDevice(IN WDFDEVICE Device)
{
	USHORT Size = 0;
	NTSTATUS Status;
	PDEVICE_CONTEXT pDeviceContext;
	PUSB_CONFIGURATION_DESCRIPTOR configurationDescriptor = NULL;
	WDF_OBJECT_ATTRIBUTES attributes;
	WDFMEMORY memory;

	PAGED_CODE();

	pDeviceContext = GetDeviceContext(Device);

	Status = WdfUsbTargetDeviceRetrieveConfigDescriptor(pDeviceContext->WdfUsbTargetDevice,
							    NULL, &Size);

	if (Status != STATUS_BUFFER_TOO_SMALL || Size == 0)
		return Status;

	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);

	attributes.ParentObject = pDeviceContext->WdfUsbTargetDevice;

	Status = WdfMemoryCreate(&attributes,
				 NonPagedPool,
				 POOL_TAG,
				 Size,
				 &memory,
				 &configurationDescriptor);
	if (!NT_SUCCESS(Status))
		return Status;

	Status = WdfUsbTargetDeviceRetrieveConfigDescriptor(pDeviceContext->WdfUsbTargetDevice,
							    configurationDescriptor,
							    &Size);
	if (!NT_SUCCESS(Status))
		return Status;

	pDeviceContext->UsbConfigurationDescriptor = configurationDescriptor;

	return UsbChief_SelectInterfaces(Device);
}

static NTSTATUS UsbChief_ReadAndSelectDescriptors(IN WDFDEVICE Device)
{
	PDEVICE_CONTEXT pDeviceContext;
	NTSTATUS Status;

	PAGED_CODE();

	pDeviceContext = GetDeviceContext(Device);

	if (!pDeviceContext->WdfUsbTargetDevice) {
		Status = WdfUsbTargetDeviceCreate(Device,
						  WDF_NO_OBJECT_ATTRIBUTES,
						  &pDeviceContext->WdfUsbTargetDevice);

		if (!NT_SUCCESS(Status))
			return Status;
	}

	WdfUsbTargetDeviceGetDeviceDescriptor(pDeviceContext->WdfUsbTargetDevice,
					      &pDeviceContext->UsbDeviceDescriptor);
	return UsbChief_ConfigureDevice(Device);
}

static NTSTATUS UsbChief_SetPowerPolicy(IN WDFDEVICE Device)
{
	WDF_DEVICE_POWER_POLICY_IDLE_SETTINGS idleSettings;
	WDF_DEVICE_POWER_POLICY_WAKE_SETTINGS wakeSettings;
	NTSTATUS Status = STATUS_SUCCESS;

	PAGED_CODE();

	WDF_DEVICE_POWER_POLICY_IDLE_SETTINGS_INIT(&idleSettings, IdleUsbSelectiveSuspend);
	idleSettings.IdleTimeout = 10000; // 10-sec

	Status = WdfDeviceAssignS0IdleSettings(Device, &idleSettings);
	if (!NT_SUCCESS(Status)) {
		UsbChief_DbgPrint(DEBUG_POWER, ("WdfDeviceSetPowerPolicyS0IdlePolicy failed  %!STATUS!\n", Status));
		return Status;
	}

	WDF_DEVICE_POWER_POLICY_WAKE_SETTINGS_INIT(&wakeSettings);

	Status = WdfDeviceAssignSxWakeSettings(Device, &wakeSettings);
	if (!NT_SUCCESS(Status)) {
		UsbChief_DbgPrint(DEBUG_POWER, ("WdfDeviceAssignSxWakeSettings failed  %!STATUS!\n", Status));
		return Status;
	}

	return Status;
}

static NTSTATUS UsbChief_EvtDevicePrepareHardware(IN WDFDEVICE Device,
					   IN WDFCMRESLIST ResourceList,
					   IN WDFCMRESLIST ResourceListTranslated)
{
	PDEVICE_CONTEXT pDeviceContext;
	WDF_USB_DEVICE_INFORMATION  info;
	NTSTATUS Status;
	UNREFERENCED_PARAMETER(Device);
	UNREFERENCED_PARAMETER(ResourceList);
	UNREFERENCED_PARAMETER(ResourceListTranslated);

	PAGED_CODE();

	UsbChief_DbgPrint(DEBUG_CONFIG, ("EvtDevicePrepareHardware\n"));

	pDeviceContext = GetDeviceContext(Device);

	Status = UsbChief_ReadAndSelectDescriptors(Device);

	if (!NT_SUCCESS(Status)) {
		UsbChief_DbgPrint(0, ("ReadandSelectDescriptors failed\n"));
		return Status;
	}

	WDF_USB_DEVICE_INFORMATION_INIT(&info);

	Status = WdfUsbTargetDeviceRetrieveInformation(pDeviceContext->WdfUsbTargetDevice,
						       &info);
	if (NT_SUCCESS(Status)) {
		pDeviceContext->IsDeviceHighSpeed =
			(info.Traits & WDF_USB_DEVICE_TRAIT_AT_HIGH_SPEED) ? TRUE : FALSE;

		UsbChief_DbgPrint(DEBUG_CONFIG, ("DeviceIsHighSpeed: %s\n",
				     pDeviceContext->IsDeviceHighSpeed ? "TRUE" : "FALSE"));
	} else {
		pDeviceContext->IsDeviceHighSpeed = FALSE;
	}

	UsbChief_DbgPrint(DEBUG_CONFIG, ("IsDeviceSelfPowered: %s\n",
			     (info.Traits & WDF_USB_DEVICE_TRAIT_SELF_POWERED) ? "TRUE" : "FALSE"));

	pDeviceContext->WaitWakeEnable =
		info.Traits & WDF_USB_DEVICE_TRAIT_REMOTE_WAKE_CAPABLE;

	UsbChief_DbgPrint(DEBUG_CONFIG, ("IsDeviceRemoteWakeable: %s\n",
			     (info.Traits & WDF_USB_DEVICE_TRAIT_REMOTE_WAKE_CAPABLE) ? "TRUE" : "FALSE"));

	if(pDeviceContext->WaitWakeEnable){
		Status = UsbChief_SetPowerPolicy(Device);
		if (!NT_SUCCESS (Status)) {
			UsbChief_DbgPrint(0, ("UsbChiefSetPowerPolicy failed\n"));
			return Status;
		}
	}
	return STATUS_SUCCESS;
}

static WDFUSBPIPE UsbChief_GetPipeFromName(IN PDEVICE_CONTEXT DeviceContext,
					   IN PUNICODE_STRING FileName)
{
	LONG ix;
	ULONG uval;
	ULONG nameLength;
	ULONG umultiplier;
	WDFUSBPIPE pipe = NULL;

	PAGED_CODE();

	nameLength = (FileName->Length / sizeof(WCHAR));

	if(nameLength != 0) {
		UsbChief_DbgPrint(DEBUG_RW, ("Filename = %wZ nameLength = %d\n", FileName, nameLength));

		ix = nameLength - 1;

		while((ix > -1) &&
		      ((FileName->Buffer[ix] < (WCHAR) '0')  ||
		       (FileName->Buffer[ix] > (WCHAR) '9')))
			ix--;

		if (ix > -1) {

			uval = 0;
			umultiplier = 1;

			while((ix > -1) &&
			      (FileName->Buffer[ix] >= (WCHAR) '0') &&
			      (FileName->Buffer[ix] <= (WCHAR) '9')) {

				uval += (umultiplier *
					 (ULONG) (FileName->Buffer[ix] - (WCHAR) '0'));

				ix--;
				umultiplier *= 10;
			}
			pipe = WdfUsbInterfaceGetConfiguredPipe(DeviceContext->UsbInterface,
								(UCHAR)uval, NULL);
		}
	}
	UsbChief_DbgPrint(DEBUG_RW, ("GetPipeFromName - ends\n"));
	return pipe;
}

static VOID UsbChief_EvtDeviceFileCreate(IN WDFDEVICE Device, IN WDFREQUEST Request,
				  IN WDFFILEOBJECT FileObject)
{
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	PUNICODE_STRING fileName;
	PFILE_CONTEXT pFileContext;
	PDEVICE_CONTEXT pDevContext;
	WDFUSBPIPE  pipe;

	PAGED_CODE();

	UsbChief_DbgPrint(DEBUG_RW|DEBUG_IOCTL, ("called\n"));

	pDevContext = GetDeviceContext(Device);
	pFileContext = GetFileContext(FileObject);

	fileName = WdfFileObjectGetFileName(FileObject);

	if (!fileName->Length) {
		status = STATUS_SUCCESS;
	} else {
		pipe = UsbChief_GetPipeFromName(pDevContext, fileName);

		if (pipe != NULL) {
			pFileContext->Pipe = pipe;

			WdfUsbTargetPipeSetNoMaximumPacketSizeCheck(pipe);

			status = STATUS_SUCCESS;
		} else {
			status = STATUS_INVALID_DEVICE_REQUEST;
		}
	}

	WdfRequestComplete(Request, status);
}

static VOID UsbChief_EvtIoDeviceControl(IN WDFQUEUE Queue, IN WDFREQUEST Request,
				 IN size_t OutputBufferLength, IN size_t InputBufferLength,
				 IN ULONG IoControlCode)
{
	NTSTATUS Status;
	size_t Length = 0;
	PIOCTL_DATA data;
	PDEVICE_CONTEXT pDeviceContext;
	UCHAR test[4096];
	UCHAR *config;
	WORD *version;
	URB Urb;
	WDF_USB_INTERFACE_SELECT_SETTING_PARAMS interfaceParams;
	ULONG i;
	UNREFERENCED_PARAMETER(OutputBufferLength);
	UNREFERENCED_PARAMETER(InputBufferLength);
	UNREFERENCED_PARAMETER(Queue);

	PAGED_CODE();

	pDeviceContext = GetDeviceContext(WdfIoQueueGetDevice(Queue));

	Length = 0;
	switch(IoControlCode) {
	case IOCTL_VENDOR_WRITE:

		if (InputBufferLength != sizeof(*data)) {
			UsbChief_DbgPrint(0, ("Invalid InputBuffer Size: %d/%d\n", InputBufferLength, sizeof(*data)));
			Status = STATUS_INVALID_DEVICE_REQUEST;
			goto out;
		}

		Status = WdfRequestRetrieveInputBuffer(Request, InputBufferLength, &data, &Length);
		if (!NT_SUCCESS(Status))
			goto out;

		UsbChief_DbgPrint(DEBUG_IOCTL, ("EvtIoDeviceControl: IOCTL_VENDOR_WRITE %x, Index %x, Value %x, Max Length %d, Buf %p\n",
				      data->Request, data->Index, data->Value, data->Length, data->Buffer));

		if (Length != sizeof(*data)) {
			UsbChief_DbgPrint(0, ("Failed to retrieve buffer: %d/%d\n", Length, sizeof(*data)));
			Status = STATUS_INVALID_DEVICE_REQUEST;
			goto out;
		}

		memset(&Urb, 0, sizeof(Urb));
		Urb.UrbHeader.Function = URB_FUNCTION_VENDOR_DEVICE;
		Urb.UrbHeader.Length = sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST);
		Urb.UrbControlVendorClassRequest.RequestTypeReservedBits = 0x40;
		Urb.UrbControlVendorClassRequest.TransferBufferLength = data->Length;
		Urb.UrbControlVendorClassRequest.TransferBuffer = (PVOID)data->Buffer;
		Urb.UrbControlVendorClassRequest.Request = data->Request;
		Urb.UrbControlVendorClassRequest.Value = data->Value;
		Urb.UrbControlVendorClassRequest.Index = data->Index;

		if (DebugLevel & DEBUG_IOCTL) {
			for(i = 0; i < data->Length; i++)
				DbgPrint("%02X ", ((PUCHAR)data->Buffer)[i]);
			DbgPrint("\n");
		}
		Status = WdfUsbTargetDeviceSendUrbSynchronously(pDeviceContext->WdfUsbTargetDevice, NULL, NULL, &Urb);

		if (!NT_SUCCESS(Status)) {
			UsbChief_DbgPrint(0, ("WdfUsbTargetDeviceSendControlTransferSynchronously failed\n"));
		}
		break;

	case IOCTL_VENDOR_READ:
		if (InputBufferLength != sizeof(*data)) {
			UsbChief_DbgPrint(0, ("Invalid InputBuffer Size: %d/%d\n", InputBufferLength, sizeof(*data)));
			Status = STATUS_INVALID_DEVICE_REQUEST;
			goto out;
		}

		Status = WdfRequestRetrieveInputBuffer(Request, InputBufferLength, &data, &Length);
		if (!NT_SUCCESS(Status))
			goto out;

		UsbChief_DbgPrint(DEBUG_IOCTL, ("EvtIoDeviceControl: IOCTL_VENDOR_READ %x, Index %x, Value %x, Max Length %d, Buf %p\n",
				      data->Request, data->Index, data->Value, data->Length, data->Buffer));

		if (Length != sizeof(*data)) {
			UsbChief_DbgPrint(0, ("Failed to retrieve buffer: %d/%d\n", Length, sizeof(*data)));
			Status = STATUS_INVALID_DEVICE_REQUEST;
			goto out;
		}

		if (data->Length > sizeof(test)) {
			Status = STATUS_INVALID_DEVICE_REQUEST;
			goto out;
		}

		memset(&Urb, 0, sizeof(Urb));
		Urb.UrbHeader.Function = URB_FUNCTION_VENDOR_DEVICE;
		Urb.UrbHeader.Length = sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST);
		Urb.UrbControlVendorClassRequest.RequestTypeReservedBits = 0xc0;
		Urb.UrbControlVendorClassRequest.TransferFlags = USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK;
		Urb.UrbControlVendorClassRequest.TransferBufferLength = data->Length;
		Urb.UrbControlVendorClassRequest.TransferBuffer = test;
		Urb.UrbControlVendorClassRequest.Request = data->Request;
		Urb.UrbControlVendorClassRequest.Value = data->Value;
		Urb.UrbControlVendorClassRequest.Index = data->Index;

		Status = WdfUsbTargetDeviceSendUrbSynchronously(pDeviceContext->WdfUsbTargetDevice, NULL, NULL, &Urb);

		if (DebugLevel & DEBUG_IOCTL && NT_SUCCESS(Status)) {
			for(i = 0; i < Urb.UrbControlVendorClassRequest.TransferBufferLength; i++)
				DbgPrint("%02X ", ((PUCHAR)test)[i]);
			DbgPrint("\n");
		}

		if (Urb.UrbControlVendorClassRequest.TransferBufferLength)
			memcpy((PVOID)data->Buffer, test, Urb.UrbControlVendorClassRequest.TransferBufferLength);
		Length = Urb.UrbControlVendorClassRequest.TransferBufferLength;
		break;

	case IOCTL_SELECT_CONFIGURATION:

		if (InputBufferLength != sizeof(*config)) {
			UsbChief_DbgPrint(0, ("Invalid InputBuffer Size: %d/%d\n", InputBufferLength, sizeof(*config)));
			Status = STATUS_INVALID_DEVICE_REQUEST;
			goto out;
		}

		config = 0;
		Status = WdfRequestRetrieveInputBuffer(Request, InputBufferLength, (PVOID)&config, &Length);
		if (!NT_SUCCESS(Status))
			goto out;

		if (Length != sizeof(*config)) {
			UsbChief_DbgPrint(0, ("Invalid Length: %d/%d\n", Length, sizeof(*config)));
			Status = STATUS_INVALID_DEVICE_REQUEST;
			goto out;
		}

		UsbChief_DbgPrint(DEBUG_IOCTL, ("EvtDeviceControl: IOCTL_SELECT_CONFIGURATION %x\n", *config));

		if (!pDeviceContext->UsbInterface) {
			Status = STATUS_INVALID_DEVICE_REQUEST;
			goto out;
		}

		WDF_USB_INTERFACE_SELECT_SETTING_PARAMS_INIT_SETTING (&interfaceParams, *config);

		Status = WdfUsbInterfaceSelectSetting(pDeviceContext->UsbInterface, WDF_NO_OBJECT_ATTRIBUTES,
						      &interfaceParams);
		break;

	case IOCTL_GET_FIRMWARE_VERSION:
		UsbChief_DbgPrint(DEBUG_IOCTL, ("EvtDeviceControl: GET_FIRMWARE_VERSION\n"));

		Status = WdfRequestRetrieveOutputBuffer(Request, OutputBufferLength, &version, &Length);
		if (!NT_SUCCESS(Status))
			goto out;

		if (Length != sizeof(*version)) {
			UsbChief_DbgPrint(0, ("Invalid Length: %d/%d\n", Length, sizeof(*version)));
			Status = STATUS_INVALID_DEVICE_REQUEST;
			goto out;
		}

		*version = pDeviceContext->UsbDeviceDescriptor.bcdDevice;
		break;

	default:
		UsbChief_DbgPrint(DEBUG_IOCTL, ("EvtDeviceControl %08x (Device %x, Method %x) unknown\n",
				      IoControlCode, DEVICE_TYPE_FROM_CTL_CODE(IoControlCode),
				      METHOD_FROM_CTL_CODE(IoControlCode)));
		Status = STATUS_INVALID_DEVICE_REQUEST;
		break;
	}
out:
	UsbChief_DbgPrint(DEBUG_IOCTL, ("EvtDeviceControl: Status %08x, Length %d\n", Status, Length));
	WdfRequestCompleteWithInformation(Request, Status, Length);
}

static VOID UsbChief_StopAllPipes(IN PDEVICE_CONTEXT DeviceContext)
{
	UCHAR count,i;

	count = DeviceContext->NumberConfiguredPipes;
	for (i = 0; i < count; i++) {
		WDFUSBPIPE pipe;
		pipe = WdfUsbInterfaceGetConfiguredPipe(DeviceContext->UsbInterface,
							i, NULL);
		WdfIoTargetStop(WdfUsbTargetPipeGetIoTarget(pipe),
				WdfIoTargetCancelSentIo);
	}
}


static VOID UsbChief_StartAllPipes(IN PDEVICE_CONTEXT DeviceContext)
{
	NTSTATUS status;
	UCHAR count,i;

	count = DeviceContext->NumberConfiguredPipes;
	for (i = 0; i < count; i++) {
		WDFUSBPIPE pipe;
		pipe = WdfUsbInterfaceGetConfiguredPipe(DeviceContext->UsbInterface,
							i, NULL);
		status = WdfIoTargetStart(WdfUsbTargetPipeGetIoTarget(pipe));
		if (!NT_SUCCESS(status)) {
			UsbChief_DbgPrint(0, ("StartAllPipes - failed pipe #%d\n", i));
		}
	}
}

static NTSTATUS UsbChief_ResetDevice(IN WDFDEVICE Device)
{
	PDEVICE_CONTEXT pDeviceContext;
	NTSTATUS status;

	UsbChief_DbgPrint(0, ("\n"));

	PAGED_CODE();

	pDeviceContext = GetDeviceContext(Device);

	UsbChief_StopAllPipes(pDeviceContext);

	status = WdfUsbTargetDeviceIsConnectedSynchronous(pDeviceContext->WdfUsbTargetDevice);

	if(NT_SUCCESS(status)) {
		status = WdfUsbTargetDeviceResetPortSynchronously(pDeviceContext->WdfUsbTargetDevice);
	}

	UsbChief_StartAllPipes(pDeviceContext);
	return status;
}

static NTSTATUS UsbChief_ResetPipe(IN WDFUSBPIPE Pipe)
{
	NTSTATUS status;

	PAGED_CODE();

	status = WdfUsbTargetPipeResetSynchronously(Pipe, WDF_NO_HANDLE, NULL);

	if (NT_SUCCESS(status)) {
		UsbChief_DbgPrint(DEBUG_CONFIG, ("success\n"));
		status = STATUS_SUCCESS;
	}
	else {
		UsbChief_DbgPrint(0, ("failed\n"));
	}
	return status;
}

static VOID UsbChief_ReadWriteWorkItem(IN WDFWORKITEM  WorkItem)
{
	PWORKITEM_CONTEXT pItemContext;
	NTSTATUS status;

	UsbChief_DbgPrint(DEBUG_RW, ("called\n"));

	pItemContext = GetWorkItemContext(WorkItem);

	status = UsbChief_ResetPipe(pItemContext->Pipe);
	if (!NT_SUCCESS(status)) {
		status = UsbChief_ResetDevice(pItemContext->Device);
		if(!NT_SUCCESS(status))
			UsbChief_DbgPrint(0, ("ResetDevice failed 0x%x\n", status));
	}
	WdfObjectDelete(WorkItem);
}

static NTSTATUS UsbChief_QueuePassiveLevelCallback(IN WDFDEVICE    Device,
				   IN WDFUSBPIPE   Pipe)
{
	NTSTATUS                       status = STATUS_SUCCESS;
	PWORKITEM_CONTEXT               context;
	WDF_OBJECT_ATTRIBUTES           attributes;
	WDF_WORKITEM_CONFIG             workitemConfig;
	WDFWORKITEM                     hWorkItem;

	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
	WDF_OBJECT_ATTRIBUTES_SET_CONTEXT_TYPE(&attributes, WORKITEM_CONTEXT);
	attributes.ParentObject = Device;

	WDF_WORKITEM_CONFIG_INIT(&workitemConfig, UsbChief_ReadWriteWorkItem);

	status = WdfWorkItemCreate( &workitemConfig,
				    &attributes,
				    &hWorkItem);

	if (!NT_SUCCESS(status))
		return status;

	context = GetWorkItemContext(hWorkItem);

	context->Device = Device;
	context->Pipe = Pipe;

	WdfWorkItemEnqueue(hWorkItem);
	return STATUS_SUCCESS;
}


static VOID UsbChief_ReadCompletion(IN WDFREQUEST Request, IN WDFIOTARGET Target,
			     PWDF_REQUEST_COMPLETION_PARAMS CompletionParams,
			     IN WDFCONTEXT Context)
{
	PMDL requestMdl;
	WDFUSBPIPE pipe;
	ULONG stageLength;
	NTSTATUS status;
	PREQUEST_CONTEXT rwContext;
	PURB urb;
	ULONG bytesRead;

	UNREFERENCED_PARAMETER(Context);
	rwContext = GetRequestContext(Request);

	pipe = (WDFUSBPIPE)Target;
	status = CompletionParams->IoStatus.Status;

	if (!NT_SUCCESS(status)){
		UsbChief_QueuePassiveLevelCallback(WdfIoTargetGetDevice(Target), pipe);
		goto End;
	}

	urb = (PURB) WdfMemoryGetBuffer(rwContext->UrbMemory, NULL);
	bytesRead = urb->UrbBulkOrInterruptTransfer.TransferBufferLength;
	rwContext->Numxfer += bytesRead;

	if (rwContext->Length == 0) {
		WdfRequestSetInformation(Request, rwContext->Numxfer);
		goto End;
	}

	if (rwContext->Length > MAX_TRANSFER_SIZE)
		stageLength = MAX_TRANSFER_SIZE;
	else
		stageLength = rwContext->Length;

	UsbChief_DbgPrint(DEBUG_RW, ("Stage next Read transfer... %d bytes remaing\n", rwContext->Length));
	MmPrepareMdlForReuse(rwContext->Mdl);

	status = WdfRequestRetrieveOutputWdmMdl(Request, &requestMdl);
	if(!NT_SUCCESS(status)){
		UsbChief_DbgPrint(0, ("WdfRequestRetrieveOutputWdmMdl for Read failed %x\n", status));
		goto End;
	}

	IoBuildPartialMdl(requestMdl, rwContext->Mdl,
			  (PVOID) rwContext->VirtualAddress, stageLength);

	urb->UrbBulkOrInterruptTransfer.TransferBufferLength = stageLength;

	rwContext->VirtualAddress += stageLength;
	rwContext->Length -= stageLength;

	status = WdfUsbTargetPipeFormatRequestForUrb(pipe, Request,
						     rwContext->UrbMemory, NULL);
	if (!NT_SUCCESS(status)) {
		UsbChief_DbgPrint(0, ("Failed to format requset for urb\n"));
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto End;
	}

	WdfRequestSetCompletionRoutine(Request, UsbChief_ReadCompletion, NULL);

	if (!WdfRequestSend(Request, WdfUsbTargetPipeGetIoTarget(pipe), WDF_NO_SEND_OPTIONS)) {
		UsbChief_DbgPrint(0, ("WdfRequestSend for Read failed\n"));
		status = WdfRequestGetStatus(Request);
		goto End;
	}
	return;

End:
	IoFreeMdl(rwContext->Mdl);

	UsbChief_DbgPrint(DEBUG_RW, ("Read request completed with status 0x%x\n", status));
	WdfRequestComplete(Request, status);
}

static VOID UsbChief_ReadEndPoint(IN WDFQUEUE Queue, IN WDFREQUEST Request,
			   IN ULONG totalLength)
{
	PMDL                    newMdl=NULL, requestMdl = NULL;
	PURB                    urb = NULL;
	WDFMEMORY               urbMemory;
	ULONG                   stageLength = 0;
	NTSTATUS                status;
	ULONG_PTR               virtualAddress = 0;
	PREQUEST_CONTEXT        rwContext = NULL;
	PFILE_CONTEXT           fileContext = NULL;
	WDFUSBPIPE              pipe;
	WDF_USB_PIPE_INFORMATION   pipeInfo;
	WDF_OBJECT_ATTRIBUTES   objectAttribs;
	USBD_PIPE_HANDLE        usbdPipeHandle;
	PDEVICE_CONTEXT         deviceContext;

	UsbChief_DbgPrint(DEBUG_RW, ("UsbChief_DispatchReadWrite - begins\n"));

	deviceContext = GetDeviceContext(WdfIoQueueGetDevice(Queue));
	fileContext = GetFileContext(WdfRequestGetFileObject(Request));
	pipe = fileContext->Pipe;

	WDF_USB_PIPE_INFORMATION_INIT(&pipeInfo);
	WdfUsbTargetPipeGetInformation(pipe, &pipeInfo);

	rwContext = GetRequestContext(Request);

	status = WdfRequestRetrieveOutputWdmMdl(Request, &requestMdl);
	if(!NT_SUCCESS(status)){
		UsbChief_DbgPrint(0, ("WdfRequestRetrieveOutputWdmMdl failed %x\n", status));
		goto Exit;
	}
	virtualAddress = (ULONG_PTR) MmGetMdlVirtualAddress(requestMdl);

	if (totalLength > MAX_TRANSFER_SIZE)
		stageLength = MAX_TRANSFER_SIZE;
	else
		stageLength = totalLength;

	newMdl = IoAllocateMdl((PVOID) virtualAddress, totalLength,
			       FALSE, FALSE, NULL);

	if (!newMdl) {
		UsbChief_DbgPrint(0, ("Failed to alloc mem for mdl\n"));
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto Exit;
	}

	IoBuildPartialMdl(requestMdl, newMdl, (PVOID)virtualAddress,
			  stageLength);

	WDF_OBJECT_ATTRIBUTES_INIT(&objectAttribs);
	objectAttribs.ParentObject = Request;

	status = WdfMemoryCreate(&objectAttribs,
				 NonPagedPool,
				 POOL_TAG,
				 sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
				 &urbMemory,
				 (PVOID*) &urb);

	if (!NT_SUCCESS(status)) {
		UsbChief_DbgPrint(0, ("Failed to alloc mem for urb\n"));
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto Exit;
	}

	usbdPipeHandle = WdfUsbTargetPipeWdmGetPipeHandle(pipe);

	UsbBuildInterruptOrBulkTransferRequest(urb,
					       sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
					       usbdPipeHandle,
					       NULL,
					       newMdl,
					       stageLength,
					       USBD_TRANSFER_DIRECTION_IN |
					       USBD_SHORT_TRANSFER_OK,
					       NULL);

	status = WdfUsbTargetPipeFormatRequestForUrb(pipe, Request, urbMemory, NULL  );
	if (!NT_SUCCESS(status)) {
		UsbChief_DbgPrint(0, ("Failed to format requset for urb\n"));
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto Exit;
	}
	WdfRequestSetCompletionRoutine(Request, UsbChief_ReadCompletion, NULL);

	rwContext->UrbMemory       = urbMemory;
	rwContext->Mdl             = newMdl;
	rwContext->Length          = totalLength - stageLength;
	rwContext->Numxfer         = 0;
	rwContext->VirtualAddress  = virtualAddress + stageLength;

	if (!WdfRequestSend(Request, WdfUsbTargetPipeGetIoTarget(pipe), WDF_NO_SEND_OPTIONS)) {
		status = WdfRequestGetStatus(Request);
		ASSERT(!NT_SUCCESS(status));
	}

Exit:
	if (!NT_SUCCESS(status)) {
		WdfRequestCompleteWithInformation(Request, status, 0);

		if (newMdl != NULL) {
			IoFreeMdl(newMdl);
		}
	}
}


static VOID UsbChief_EvtIoRead(IN WDFQUEUE Queue, IN WDFREQUEST Request, IN size_t Length)
{
	PFILE_CONTEXT           fileContext = NULL;
	WDFUSBPIPE              pipe;
	WDF_USB_PIPE_INFORMATION   pipeInfo;

	UNREFERENCED_PARAMETER(Queue);

	PAGED_CODE();

	UsbChief_DbgPrint(DEBUG_RW, ("EvtIoRead %d\n", Length));

	fileContext = GetFileContext(WdfRequestGetFileObject(Request));
	pipe = fileContext->Pipe;
	if (pipe == NULL) {
		UsbChief_DbgPrint(0, ("pipe handle is NULL\n"));
		WdfRequestCompleteWithInformation(Request, STATUS_INVALID_PARAMETER, 0);
		return;
	}
	WDF_USB_PIPE_INFORMATION_INIT(&pipeInfo);
	WdfUsbTargetPipeGetInformation(pipe, &pipeInfo);

	UsbChief_ReadEndPoint(Queue, Request, (ULONG) Length);
}

static VOID UsbChief_EvtIoWrite(IN WDFQUEUE Queue, IN WDFREQUEST Request, IN size_t Length)
{
	UNREFERENCED_PARAMETER(Queue);

	PAGED_CODE();

	UsbChief_DbgPrint(DEBUG_RW, ("EvtIoWrite %d\n", Length));
	WdfRequestCompleteWithInformation(Request, STATUS_INVALID_DEVICE_REQUEST, 0);
}

static VOID UsbChief_EvtIoStop(IN WDFQUEUE Queue, IN WDFREQUEST Request, IN ULONG ActionFlags)
{
	UNREFERENCED_PARAMETER(Queue);

	PAGED_CODE();

	UsbChief_DbgPrint(DEBUG_POWER, ("EvtIoStop %08x\n", ActionFlags));
	WdfRequestCompleteWithInformation(Request, STATUS_INVALID_DEVICE_REQUEST, 0);
}

static VOID UsbChief_EvtIoResume(IN WDFQUEUE Queue, IN WDFREQUEST Request)
{
	UNREFERENCED_PARAMETER(Queue);

	PAGED_CODE();

	UsbChief_DbgPrint(DEBUG_POWER, ("EvtIoResume\n"));
	WdfRequestCompleteWithInformation(Request, STATUS_INVALID_DEVICE_REQUEST, 0);
}

static NTSTATUS UsbChief_EvtDeviceAdd(IN WDFDRIVER Driver, IN PWDFDEVICE_INIT DeviceInit)
{
	WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
	WDF_OBJECT_ATTRIBUTES fileObjectAttributes, requestAttributes, fdoAttributes;
	WDF_FILEOBJECT_CONFIG fileConfig;
	NTSTATUS Status;
	WDFDEVICE device;
	WDF_IO_QUEUE_CONFIG ioQueueConfig;
	WDF_DEVICE_PNP_CAPABILITIES pnpCaps;
	WDFQUEUE queue;
	UNICODE_STRING linkname;

	UNREFERENCED_PARAMETER(Driver);
	PAGED_CODE();

	UsbChief_DbgPrint(DEBUG_CONFIG, ("ExtDeviceAdd\n"));

	/* Init PnP */
	WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
	pnpPowerCallbacks.EvtDevicePrepareHardware = UsbChief_EvtDevicePrepareHardware;
	WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

	/* Request Attributes */
	WDF_OBJECT_ATTRIBUTES_INIT(&requestAttributes);
	WDF_OBJECT_ATTRIBUTES_SET_CONTEXT_TYPE(&requestAttributes, REQUEST_CONTEXT);
	WdfDeviceInitSetRequestAttributes(DeviceInit, &requestAttributes);

	/* Fileobject init */

	WDF_FILEOBJECT_CONFIG_INIT(&fileConfig, UsbChief_EvtDeviceFileCreate,
			WDF_NO_EVENT_CALLBACK,
			WDF_NO_EVENT_CALLBACK);

	WDF_OBJECT_ATTRIBUTES_INIT(&fileObjectAttributes);
	WDF_OBJECT_ATTRIBUTES_SET_CONTEXT_TYPE(&fileObjectAttributes, FILE_CONTEXT);
	WdfDeviceInitSetFileObjectConfig(DeviceInit, &fileConfig, &fileObjectAttributes);

	WDF_OBJECT_ATTRIBUTES_INIT(&fdoAttributes);
	WDF_OBJECT_ATTRIBUTES_SET_CONTEXT_TYPE(&fdoAttributes, DEVICE_CONTEXT);

	Status = WdfDeviceCreate(&DeviceInit, &fdoAttributes, &device);
	if (!NT_SUCCESS(Status)) {
		UsbChief_DbgPrint(0, ("WdfDeviceCreate: %08x\n", Status));
		return Status;
	}

	RtlInitUnicodeString(&linkname, L"\\DosDevices\\ChiefUSB");
	Status = WdfDeviceCreateSymbolicLink(device, &linkname);
	if (!NT_SUCCESS(Status)) {
		UsbChief_DbgPrint(0, ("WdfDeviceCreateSymbolicLink: %08x\n", Status));
		goto out;
	}

	WDF_DEVICE_PNP_CAPABILITIES_INIT(&pnpCaps);
	pnpCaps.SurpriseRemovalOK = WdfTrue;
	WdfDeviceSetPnpCapabilities(device, &pnpCaps);

	/* Register I/O callbacks */
	WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&ioQueueConfig, WdfIoQueueDispatchParallel);
	ioQueueConfig.EvtIoRead = UsbChief_EvtIoRead;
	ioQueueConfig.EvtIoWrite = UsbChief_EvtIoWrite;
	ioQueueConfig.EvtIoDeviceControl = UsbChief_EvtIoDeviceControl;
	ioQueueConfig.EvtIoStop = UsbChief_EvtIoStop;
	ioQueueConfig.EvtIoResume = UsbChief_EvtIoResume;

	Status = WdfIoQueueCreate(device, &ioQueueConfig, WDF_NO_OBJECT_ATTRIBUTES, &queue);

	if (!NT_SUCCESS(Status)) {
		UsbChief_DbgPrint(0, ("WdfIoQueueCreate: %08x\n", Status));
		goto out;
	}

	Status = WdfDeviceCreateDeviceInterface(device, (LPGUID)&GUID_CLASS_USBCHIEF_USB, NULL);
	if (!NT_SUCCESS(Status)) {
		UsbChief_DbgPrint(0, ("WdfDeviceCreateDeviceInterface: %08x\n", Status));
		goto out;
	}
	UsbChief_DbgPrint(3, ("EvtDriverDeviceAdd successful\n"));
	return STATUS_SUCCESS;
out:
	return Status;
}

NTSTATUS DriverEntry(IN PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING RegistryPath)
{
	NTSTATUS Status;
	WDF_DRIVER_CONFIG Config;

	WPP_INIT_TRACING(DriverObject, RegistryPath);

	UsbChief_DbgPrint(DEBUG_CONFIG, ("starting\n"));
	WDF_DRIVER_CONFIG_INIT(&Config, UsbChief_EvtDeviceAdd);
	Status = WdfDriverCreate(DriverObject, RegistryPath,
		WDF_NO_OBJECT_ATTRIBUTES, &Config, WDF_NO_HANDLE);
	if (!NT_SUCCESS(Status)) {
		UsbChief_DbgPrint(0, ("WdfDriverCreate failed: 0x%08x\n", Status));
		return Status;
	}
	return STATUS_SUCCESS;
}
