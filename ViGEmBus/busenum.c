/*
MIT License

Copyright (c) 2016 Benjamin "Nefarius" H�glinger

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/


#include "busenum.h"
#include <wdmguid.h>
#include <usb.h>

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, Bus_PlugInDevice)
#pragma alloc_text (PAGE, Bus_UnPlugDevice)
#endif



//
// Simulates a device plug-in event.
// 
NTSTATUS Bus_PlugInDevice(
    _In_ WDFDEVICE Device,
    _In_ WDFREQUEST Request,
    _In_ BOOLEAN IsInternal,
    _Out_ size_t* Transferred)
{
    PDO_IDENTIFICATION_DESCRIPTION  description;
    NTSTATUS                        status;
    PVIGEM_PLUGIN_TARGET            plugIn;
    WDFFILEOBJECT                   fileObject;
    PFDO_FILE_DATA                  pFileData;
    size_t                          length = 0;

    PAGED_CODE();

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(VIGEM_PLUGIN_TARGET), (PVOID)&plugIn, &length);

    if (!NT_SUCCESS(status))
    {
        KdPrint((DRIVERNAME "WdfRequestRetrieveInputBuffer failed 0x%x\n", status));
        return status;
    }

    if ((sizeof(VIGEM_PLUGIN_TARGET) != plugIn->Size) || (length != plugIn->Size))
    {
        KdPrint((DRIVERNAME "Input buffer size mismatch"));
        return STATUS_INVALID_PARAMETER;
    }

    if (plugIn->SerialNo == 0)
    {
        KdPrint((DRIVERNAME "Serial no. 0 not allowed"));
        return STATUS_INVALID_PARAMETER;
    }

    *Transferred = length;
 
    fileObject = WdfRequestGetFileObject(Request);
    if (fileObject == NULL)
    {
        KdPrint((DRIVERNAME "File object associated with request is null"));
        return STATUS_INVALID_PARAMETER;
    }

    pFileData = FileObjectGetData(fileObject);
    if (pFileData == NULL)
    {
        KdPrint((DRIVERNAME "File object context associated with request is null"));
        return STATUS_INVALID_PARAMETER;
    }

    //
    // Initialize the description with the information about the newly
    // plugged in device.
    //
    WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER_INIT(&description.Header, sizeof(description));

    description.SerialNo = plugIn->SerialNo;
    description.TargetType = plugIn->TargetType;
    description.OwnerProcessId = CURRENT_PROCESS_ID();
    description.SessionId = pFileData->SessionId;
    description.OwnerIsDriver = IsInternal;

    // Set default IDs if supplied values are invalid
    if (plugIn->VendorId == 0 || plugIn->ProductId == 0)
    {
        switch (plugIn->TargetType)
        {
        case Xbox360Wired:

            description.VendorId = 0x045E;
            description.ProductId = 0x028E;

            break;
        case DualShock4Wired:

            description.VendorId = 0x054C;
            description.ProductId = 0x05C4;

            break;
        case XboxOneWired:

            description.VendorId = 0x0E6F;
            description.ProductId = 0x0139;

#if !DBG
            // TODO: implement and remove!
            return STATUS_NOT_SUPPORTED;
#endif

            break;
        }
    }
    else
    {
        description.VendorId = plugIn->VendorId;
        description.ProductId = plugIn->ProductId;
    }

    status = WdfChildListAddOrUpdateChildDescriptionAsPresent(WdfFdoGetDefaultChildList(Device), &description.Header, NULL);

    if (status == STATUS_OBJECT_NAME_EXISTS)
    {
        //
        // The description is already present in the list, the serial number is
        // not unique, return error.
        //
        status = STATUS_INVALID_PARAMETER;
    }

    return status;
}

//
// Simulates a device unplug event.
// 
NTSTATUS Bus_UnPlugDevice(
    _In_ WDFDEVICE Device,
    _In_ WDFREQUEST Request,
    _In_ BOOLEAN IsInternal,
    _Out_ size_t* Transferred)
{
    NTSTATUS                            status;
    WDFDEVICE                           hChild;
    WDFCHILDLIST                        list;
    WDF_CHILD_LIST_ITERATOR             iterator;
    WDF_CHILD_RETRIEVE_INFO             childInfo;
    PDO_IDENTIFICATION_DESCRIPTION      description;
    BOOLEAN                             unplugAll;
    PVIGEM_UNPLUG_TARGET                unPlug;
    WDFFILEOBJECT                       fileObject;
    PFDO_FILE_DATA                      pFileData = NULL;
    size_t                              length = 0;

    PAGED_CODE();

    KdPrint((DRIVERNAME "Entered Bus_UnPlugDevice\n"));

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(VIGEM_UNPLUG_TARGET), (PVOID)&unPlug, &length);

    if (!NT_SUCCESS(status))
    {
        KdPrint((DRIVERNAME "Bus_UnPlugDevice: WdfRequestRetrieveInputBuffer failed 0x%x\n", status));
        return status;
    }

    if ((sizeof(VIGEM_UNPLUG_TARGET) != unPlug->Size) || (length != unPlug->Size))
    {
        KdPrint((DRIVERNAME "Bus_UnPlugDevice: Input buffer size mismatch"));
        return STATUS_INVALID_PARAMETER;
    }

    *Transferred = length;
    unplugAll = (unPlug->SerialNo == 0);

    if (!IsInternal)
    {
        fileObject = WdfRequestGetFileObject(Request);
        if (fileObject == NULL)
        {
            KdPrint((DRIVERNAME "Bus_UnPlugDevice: File object associated with request is null"));
            return STATUS_INVALID_PARAMETER;
        }

        pFileData = FileObjectGetData(fileObject);
        if (pFileData == NULL)
        {
            KdPrint((DRIVERNAME "Bus_UnPlugDevice: File object context associated with request is null"));
            return STATUS_INVALID_PARAMETER;
        }
    }

    list = WdfFdoGetDefaultChildList(Device);

    WDF_CHILD_LIST_ITERATOR_INIT(&iterator, WdfRetrievePresentChildren);

    WdfChildListBeginIteration(list, &iterator);

    for (;;)
    {
        WDF_CHILD_RETRIEVE_INFO_INIT(&childInfo, &description.Header);
        WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER_INIT(&description.Header, sizeof(description));

        status = WdfChildListRetrieveNextDevice(list, &iterator, &hChild, &childInfo);

        // Error or no more children, end loop
        if (!NT_SUCCESS(status) || status == STATUS_NO_MORE_ENTRIES)
        {
            break;
        }

        // If unable to retrieve device
        if (childInfo.Status != WdfChildListRetrieveDeviceSuccess)
        {
            continue;
        }

        // Child isn't the one we looked for, skip
        if (!unplugAll && description.SerialNo != unPlug->SerialNo)
        {
            continue;
        }

        // Only unplug owned children
        if (IsInternal || description.SessionId == pFileData->SessionId)
        {
            // Unplug child
            status = WdfChildListUpdateChildDescriptionAsMissing(list, &description.Header);
            if (!NT_SUCCESS(status))
            {
                KdPrint((DRIVERNAME "Bus_UnPlugDevice: WdfChildListUpdateChildDescriptionAsMissing failed with status 0x%X\n", status));
            }
        }
    }

    WdfChildListEndIteration(list, &iterator);

    return STATUS_SUCCESS;
}

//
// Sends a report update to an XUSB PDO.
// 
NTSTATUS Bus_XusbSubmitReport(WDFDEVICE Device, ULONG SerialNo, PXUSB_SUBMIT_REPORT Report, BOOLEAN FromInterface)
{
    return Bus_SubmitReport(Device, SerialNo, Report, FromInterface);
}

//
// Queues an inverted call to receive XUSB-specific updates.
// 
NTSTATUS Bus_QueueNotification(WDFDEVICE Device, ULONG SerialNo, WDFREQUEST Request)
{
    NTSTATUS                    status = STATUS_INVALID_PARAMETER;
    WDFCHILDLIST                list;
    WDF_CHILD_RETRIEVE_INFO     info;
    WDFDEVICE                   hChild;
    PPDO_DEVICE_DATA            pdoData;
    PXUSB_DEVICE_DATA           xusbData;
    PDS4_DEVICE_DATA            ds4Data;


    KdPrint((DRIVERNAME "Entered Bus_QueueNotification\n"));

#pragma region Get PDO from child list

    list = WdfFdoGetDefaultChildList(Device);

    PDO_IDENTIFICATION_DESCRIPTION description;

    WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER_INIT(&description.Header, sizeof(description));

    description.SerialNo = SerialNo;

    WDF_CHILD_RETRIEVE_INFO_INIT(&info, &description.Header);

    hChild = WdfChildListRetrievePdo(list, &info);

#pragma endregion

    // Validate child
    if (hChild == NULL)
    {
        KdPrint((DRIVERNAME "Bus_QueueNotification: PDO with serial %d not found\n", SerialNo));
        return STATUS_NO_SUCH_DEVICE;
    }

    // Check common context
    pdoData = PdoGetData(hChild);
    if (pdoData == NULL)
    {
        KdPrint((DRIVERNAME "Bus_QueueNotification: PDO context not found\n"));
        return STATUS_INVALID_PARAMETER;
    }

    // Check if caller owns this PDO
    if (!IS_OWNER(pdoData))
    {
        KdPrint((DRIVERNAME "Bus_QueueNotification: PID mismatch: %d != %d\n", pdoData->OwnerProcessId, CURRENT_PROCESS_ID()));
        return STATUS_ACCESS_DENIED;
    }

    // Queue the request for later completion by the PDO and return STATUS_PENDING
    switch (pdoData->TargetType)
    {
    case Xbox360Wired:

        xusbData = XusbGetData(hChild);

        if (xusbData == NULL) break;

        status = WdfRequestForwardToIoQueue(Request, xusbData->PendingNotificationRequests);

        break;
    case DualShock4Wired:

        ds4Data = Ds4GetData(hChild);

        if (ds4Data == NULL) break;

        status = WdfRequestForwardToIoQueue(Request, ds4Data->PendingNotificationRequests);

        break;
    }

    if (!NT_SUCCESS(status))
    {
        KdPrint((DRIVERNAME "WdfRequestForwardToIoQueue failed with status 0x%X\n", status));
    }

    return (NT_SUCCESS(status)) ? STATUS_PENDING : status;
}

//
// Sends a report update to a DS4 PDO.
// 
NTSTATUS Bus_Ds4SubmitReport(WDFDEVICE Device, ULONG SerialNo, PDS4_SUBMIT_REPORT Report, BOOLEAN FromInterface)
{
    return Bus_SubmitReport(Device, SerialNo, Report, FromInterface);
}

NTSTATUS Bus_XgipSubmitReport(WDFDEVICE Device, ULONG SerialNo, PXGIP_SUBMIT_REPORT Report, BOOLEAN FromInterface)
{
    return Bus_SubmitReport(Device, SerialNo, Report, FromInterface);
}

NTSTATUS Bus_XgipSubmitInterrupt(WDFDEVICE Device, ULONG SerialNo, PXGIP_SUBMIT_INTERRUPT Report, BOOLEAN FromInterface)
{
    return Bus_SubmitReport(Device, SerialNo, Report, FromInterface);
}

NTSTATUS Bus_SubmitReport(WDFDEVICE Device, ULONG SerialNo, PVOID Report, BOOLEAN FromInterface)
{
    NTSTATUS                    status = STATUS_SUCCESS;
    WDFCHILDLIST                list;
    WDF_CHILD_RETRIEVE_INFO     info;
    WDFDEVICE                   hChild;
    PPDO_DEVICE_DATA            pdoData;
    WDFREQUEST                  usbRequest;
    PIRP                        pendingIrp;
    BOOLEAN                     changed;


    KdPrint((DRIVERNAME "Entered Bus_SubmitReport\n"));

#pragma region Get PDO from child list

    list = WdfFdoGetDefaultChildList(Device);

    PDO_IDENTIFICATION_DESCRIPTION description;

    WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER_INIT(&description.Header, sizeof(description));

    description.SerialNo = SerialNo;

    WDF_CHILD_RETRIEVE_INFO_INIT(&info, &description.Header);

    hChild = WdfChildListRetrievePdo(list, &info);

#pragma endregion

    // Validate child
    if (hChild == NULL)
    {
        KdPrint((DRIVERNAME "Bus_SubmitReport: PDO with serial %d not found\n", SerialNo));
        return STATUS_NO_SUCH_DEVICE;
    }

    // Check common context
    pdoData = PdoGetData(hChild);
    if (pdoData == NULL)
    {
        KdPrint((DRIVERNAME "Bus_SubmitReport: PDO context not found\n"));
        return STATUS_INVALID_PARAMETER;
    }

    // Check if caller owns this PDO
    if (!FromInterface && !IS_OWNER(pdoData))
    {
        KdPrint((DRIVERNAME "Bus_SubmitReport: PID mismatch: %d != %d\n", pdoData->OwnerProcessId, CURRENT_PROCESS_ID()));
        return STATUS_ACCESS_DENIED;
    }

    // Check if input is different from previous value
    switch (pdoData->TargetType)
    {
    case Xbox360Wired:

        changed = (RtlCompareMemory(XusbGetData(hChild)->Report,
            &((PXUSB_SUBMIT_REPORT)Report)->Report,
            sizeof(XUSB_REPORT)) != sizeof(XUSB_REPORT));

        break;
    case DualShock4Wired:

        changed = TRUE;

        break;
    case XboxOneWired:

        // TODO: necessary?
        changed = TRUE;

        break;
    default:

        changed = FALSE;

        break;
    }

    // Don't waste pending IRP if input hasn't changed
    if (!changed)
        return status;

    KdPrint((DRIVERNAME "Bus_SubmitReport: received new report\n"));

    // Get pending USB request
    switch (pdoData->TargetType)
    {
    case Xbox360Wired:

        status = WdfIoQueueRetrieveNextRequest(XusbGetData(hChild)->PendingUsbInRequests, &usbRequest);

        break;
    case DualShock4Wired:

        status = WdfIoQueueRetrieveNextRequest(Ds4GetData(hChild)->PendingUsbInRequests, &usbRequest);

        break;
    case XboxOneWired:

        // Request is control data
        if (((PXGIP_SUBMIT_INTERRUPT)Report)->Size == sizeof(XGIP_SUBMIT_INTERRUPT))
        {
            PXGIP_DEVICE_DATA xgip = XgipGetData(hChild);
            PXGIP_SUBMIT_INTERRUPT interrupt = (PXGIP_SUBMIT_INTERRUPT)Report;
            WDFMEMORY memory;
            WDF_OBJECT_ATTRIBUTES memAttribs;
            WDF_OBJECT_ATTRIBUTES_INIT(&memAttribs);

            memAttribs.ParentObject = hChild;

            // Allocate kernel memory
            status = WdfMemoryCreate(&memAttribs, NonPagedPool, VIGEM_POOL_TAG,
                interrupt->InterruptLength, &memory, NULL);
            if (!NT_SUCCESS(status))
            {
                KdPrint((DRIVERNAME "WdfMemoryCreate failed with status 0x%X\n", status));
                return status;
            }

            // Copy interrupt buffer to memory object
            status = WdfMemoryCopyFromBuffer(memory, 0, interrupt->Interrupt, interrupt->InterruptLength);
            if (!NT_SUCCESS(status))
            {
                KdPrint((DRIVERNAME "WdfMemoryCopyFromBuffer failed with status 0x%X\n", status));
                return status;
            }

            // Add memory object to collection
            status = WdfCollectionAdd(xgip->XboxgipSysInitCollection, memory);
            if (!NT_SUCCESS(status))
            {
                KdPrint((DRIVERNAME "WdfCollectionAdd failed with status 0x%X\n", status));
                return status;
            }

            // Check if all packets have been received
            xgip->XboxgipSysInitReady =
                WdfCollectionGetCount(xgip->XboxgipSysInitCollection) == XGIP_SYS_INIT_PACKETS;

            // If all packets are cached, start initialization timer
            if (xgip->XboxgipSysInitReady)
            {
                WdfTimerStart(xgip->XboxgipSysInitTimer, XGIP_SYS_INIT_PERIOD);
            }

            return status;
        }

        status = WdfIoQueueRetrieveNextRequest(XgipGetData(hChild)->PendingUsbInRequests, &usbRequest);

        break;
    default:

        return STATUS_NOT_SUPPORTED;
    }

    if (!NT_SUCCESS(status))
        return status;

    KdPrint((DRIVERNAME "Bus_SubmitReport: pending IRP found\n"));

    // Get pending IRP
    pendingIrp = WdfRequestWdmGetIrp(usbRequest);

    // Get USB request block
    PURB urb = (PURB)URB_FROM_IRP(pendingIrp);

    // Get transfer buffer
    PUCHAR Buffer = (PUCHAR)urb->UrbBulkOrInterruptTransfer.TransferBuffer;

    switch (pdoData->TargetType)
    {
    case Xbox360Wired:

        urb->UrbBulkOrInterruptTransfer.TransferBufferLength = XUSB_REPORT_SIZE;

        /* Copy report to cache and transfer buffer
         * The first two bytes are always the same, so we skip them */
        RtlCopyBytes(XusbGetData(hChild)->Report + 2, &((PXUSB_SUBMIT_REPORT)Report)->Report, sizeof(XUSB_REPORT));
        RtlCopyBytes(Buffer, XusbGetData(hChild)->Report, XUSB_REPORT_SIZE);

        break;
    case DualShock4Wired:

        urb->UrbBulkOrInterruptTransfer.TransferBufferLength = DS4_REPORT_SIZE;

        /* Copy report to cache and transfer buffer
         * Skip first byte as it contains the never changing report id */
        RtlCopyBytes(Ds4GetData(hChild)->Report + 1, &((PDS4_SUBMIT_REPORT)Report)->Report, sizeof(DS4_REPORT));
        RtlCopyBytes(Buffer, Ds4GetData(hChild)->Report, DS4_REPORT_SIZE);

        break;
    case XboxOneWired:

        // Request is input report
        if (((PXGIP_SUBMIT_REPORT)Report)->Size == sizeof(XGIP_SUBMIT_REPORT))
        {
            urb->UrbBulkOrInterruptTransfer.TransferBufferLength = XGIP_REPORT_SIZE;

            // Increase event counter on every call (can roll-over)
            XgipGetData(hChild)->Report[2]++;

            /* Copy report to cache and transfer buffer
             * Skip first four bytes as they are not part of the report */
            RtlCopyBytes(XgipGetData(hChild)->Report + 4, &((PXGIP_SUBMIT_REPORT)Report)->Report, sizeof(XGIP_REPORT));
            RtlCopyBytes(Buffer, XgipGetData(hChild)->Report, XGIP_REPORT_SIZE);

            break;
        }

        break;
    default:
        status = STATUS_INVALID_PARAMETER;
        break;
    }

    // Complete pending request
    WdfRequestComplete(usbRequest, status);

    return status;
}

