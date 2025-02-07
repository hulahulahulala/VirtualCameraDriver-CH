/**************************************************************************

    AVStream Simulated Hardware Sample

    Copyright (c) 2001, Microsoft Corporation.

    File:

        device.cpp

    Abstract:

        This file contains the device level implementation of the AVStream
        hardware sample.  Note that this is not the "fake" hardware.  The
        "fake" hardware is in hwsim.cpp.

    History:

        created 3/9/2001

**************************************************************************/

#include "avshws.h"

//（总结：operator new是用来分配内存的函数，为new操作符调用。能够被重载（有限制）
PVOID operator new
(
    size_t          iSize,
    _When_((poolType & NonPagedPoolMustSucceed) != 0,
       __drv_reportError("Must succeed pool allocations are forbidden. "
             "Allocation failures cause a system crash"))
    POOL_TYPE       poolType
)
{
    PVOID result = ExAllocatePoolWithTag(poolType,iSize,'wNCK');//根据指定存储区类型参数分配一段空间，并把该空间的首地址作为返回值发送给调用者。

    if (result) {
        RtlZeroMemory(result,iSize);
    }

    return result;
}
//NonPagedPoolMustSucceed 从非分页内存池中分配内存，如果不能分配则产生bugcheck
PVOID operator new
(
    size_t          iSize,
    _When_((poolType & NonPagedPoolMustSucceed) != 0,
       __drv_reportError("Must succeed pool allocations are forbidden. "
             "Allocation failures cause a system crash"))
    POOL_TYPE       poolType,
    ULONG           tag
)
{
    PVOID result = ExAllocatePoolWithTag(poolType,iSize,tag);

    if (result) {
        RtlZeroMemory(result,iSize);
    }

    return result;
}

//这段代码是重载了数组版本的 operator new[] 函数，
//用于在 Windows 内核模式下动态分配数组类型的内存。它与之前提到的 operator new 函数类似，
//不同之处在于它用于分配数组类型的内存。
PVOID 
operator new[](
    size_t          iSize,
    _When_((poolType & NonPagedPoolMustSucceed) != 0,
        __drv_reportError("Must succeed pool allocations are forbidden. "
            "Allocation failures cause a system crash"))
    POOL_TYPE       poolType,
    ULONG           tag
)
{
    PVOID result = ExAllocatePoolWithTag(poolType, iSize, tag);

    if (result)
    {
        RtlZeroMemory(result, iSize);
    }

    return result;
}

/*++

Routine Description:

    Array delete() operator.

Arguments:

    pVoid -
        The memory to free.

Return Value:

    None

--*/
//__cdecl 是C Declaration的缩写（declaration，声明），cdecl调用方式又称为C调用方式，是C语言缺省的调用方式。
void 
__cdecl 
operator delete[](
    PVOID pVoid
)
{
    if (pVoid)
    {
        ExFreePool(pVoid);
    }
}

/*++

Routine Description:

    Sized delete() operator.

Arguments:

    pVoid -
        The memory to free.

    size -
        The size of the memory to free.

Return Value:

    None

--*/
void __cdecl operator delete
(
    void *pVoid,
    size_t /*size*/
)
{
    if (pVoid)
    {
        ExFreePool(pVoid);
    }
}

/*++

Routine Description:

    Sized delete[]() operator.

Arguments:

    pVoid -
        The memory to free.

    size -
        The size of the memory to free.

Return Value:

    None

--*/
void __cdecl operator delete[]
(
    void *pVoid,
    size_t /*size*/
)
{
    if (pVoid)
    {
        ExFreePool(pVoid);
    }
}

void __cdecl operator delete
(
    PVOID pVoid
    )
{
    if (pVoid) {
        ExFreePool(pVoid);
    }
}

/**************************************************************************

    PAGEABLE CODE
可以将全部或部分驱动程序设置为可分页。分页驱动程序代码可以减小驱动程序加载映像的大小，
从而释放系统空间用于其他用途。它对于偶尔使用的设备（如调制解调器和 CD-ROM）的驱动程序或
很少调用的驱动程序部分最为实用。
**************************************************************************/

#ifdef ALLOC_PRAGMA
#pragma code_seg("PAGE")
#endif // ALLOC_PRAGMA

NTSTATUS
CCaptureDevice::
DispatchCreate (
    IN PKSDEVICE Device
    )

/*++

Routine Description:

    Create the capture device.  This is the creation dispatch for the
    capture device.

Arguments:

    Device -
        The AVStream device being created.

Return Value:

    Success / Failure

--*/

{
//PAGED_CODE 宏确保调用方线程在足够低的允许分页的 IRQL 上运行。
    PAGED_CODE();

    NTSTATUS Status;
//这里看起来像是在设置一个新的设备了
    CCaptureDevice *CapDevice = new (NonPagedPoolNx, 'veDC') CCaptureDevice (Device);

    if (!CapDevice) {
        //
        // Return failure if we couldn't create the pin.
        //
        Status = STATUS_INSUFFICIENT_RESOURCES;

    } else {

        //
        // Add the item to the object bag if we were successful.
        // Whenever the device goes away, the bag is cleaned up and
        // we will be freed.
        //
        // For backwards compatibility with DirectX 8.0, we must grab
        // the device mutex before doing this.  For Windows XP, this is
        // not required, but it is still safe.
        //
        KsAcquireDevice (Device);//KsAcquireDevice 函数通过获取设备互斥体来获取 Device 的同步访问。
	/*
	[in] ObjectBag
	KSOBJECT_BAG (等效于要向其添加所请求项的 PVOID) 类型。 每个 AVStream 对象 (例如 ，KSFILTER 和 KSPIN) 都包含一个名为 Bag 的成员。 在此参数中传递该成员。
	[in] Item
	指向要添加到对象包的项的指针。
	[in, optional] Free
	从对象包中删除项目或删除对象包时调用的函数。 此函数通常用于释放与 Item 关联的任何动态内存。
     	*/
        Status = KsAddItemToObjectBag (
            Device -> Bag,
            reinterpret_cast <PVOID> (CapDevice),
            reinterpret_cast <PFNKSFREE> (CCaptureDevice::Cleanup)
            );
        KsReleaseDevice (Device);

        if (!NT_SUCCESS (Status)) {
            delete CapDevice;
        } else {
            Device -> Context = reinterpret_cast <PVOID> (CapDevice);
        }

    }

    return Status;

}

/*************************************************/


NTSTATUS
CCaptureDevice::
PnpStart (
    IN PCM_RESOURCE_LIST TranslatedResourceList,//CM_PARTIAL_RESOURCE_LIST结构指定分配给设备的一组不同类型的系统硬件资源。 此结构包含在 CM_FULL_RESOURCE_DESCRIPTOR 结构中。
    IN PCM_RESOURCE_LIST UntranslatedResourceList
    )

/*++

Routine Description:

    Called at Pnp start.  We start up our virtual hardware simulation.

Arguments:

    TranslatedResourceList -
        The translated resource list from Pnp

    UntranslatedResourceList -
        The untranslated resource list from Pnp

Return Value:

    Success / Failure

--*/

{

    PAGED_CODE();

    //
    // Normally, we'd do things here like parsing the resource lists and
    // connecting our interrupt.  Since this is a simulation, there isn't
    // much to parse.  The parsing and connection should be the same as
    // any WDM driver.  The sections that will differ are illustrated below
    // in setting up a simulated DMA.
    //

    NTSTATUS Status = STATUS_SUCCESS;

    if (!m_Device -> Started) {
        // Create the Filter for the device
        KsAcquireDevice(m_Device);//KsAcquireDevice 函数通过获取设备互斥体来获取 Device 的同步访问。
        Status = KsCreateFilterFactory( m_Device->FunctionalDeviceObject,
                                        &CaptureFilterDescriptor,
                                        L"GLOBAL",
                                        NULL,
                                        KSCREATE_ITEM_FREEONSTOP,
                                        NULL,
                                        NULL,
                                        NULL );//** KsCreateFilterFactory** 函数将筛选器工厂添加到给定的设备。
        KsReleaseDevice(m_Device);

    }
    //
    // By PnP, it's possible to receive multiple starts without an intervening
    // stop (to reevaluate resources, for example).  Thus, we only perform
    // creations of the simulation on the initial start and ignore any 
    // subsequent start.  Hardware drivers with resources should evaluate
    // resources and make changes on 2nd start.
    //
    if (NT_SUCCESS(Status) && (!m_Device -> Started)) {

        m_HardwareSimulation = new (NonPagedPoolNx, 'miSH') CHardwareSimulation (this);
        if (!m_HardwareSimulation) {
            //
            // If we couldn't create the hardware simulation, fail.
            //
            Status = STATUS_INSUFFICIENT_RESOURCES;
    
        } else {
            Status = KsAddItemToObjectBag (
                m_Device -> Bag,
                reinterpret_cast <PVOID> (m_HardwareSimulation),
                reinterpret_cast <PFNKSFREE> (CHardwareSimulation::Cleanup)
                );

            if (!NT_SUCCESS (Status)) {
                delete m_HardwareSimulation;
            }
        }
    }
    
    return Status;

}

/*************************************************/


void
CCaptureDevice::
PnpStop (
    )

/*++

Routine Description:

    This is the pnp stop dispatch for the capture device.  It releases any
    adapter object previously allocated by IoGetDmaAdapter during Pnp Start.

Arguments:

    None

Return Value:

    None

--*/

{

    PAGED_CODE();

    if (m_DmaAdapterObject) {
        //
        // Return the DMA adapter back to the system.
        //
        m_DmaAdapterObject -> DmaOperations -> 
            PutDmaAdapter (m_DmaAdapterObject);

        m_DmaAdapterObject = NULL;
    }

}

/*************************************************/


NTSTATUS
CCaptureDevice::
AcquireHardwareResources (
    IN ICaptureSink *CaptureSink,
    IN PKS_VIDEOINFOHEADER VideoInfoHeader
    )

/*++

Routine Description:

    Acquire hardware resources for the capture hardware.  If the 
    resources are already acquired, this will return an error.
    The hardware configuration must be passed as a VideoInfoHeader.

Arguments:

    CaptureSink -
        The capture sink attempting to acquire resources.  When scatter /
        gather mappings are completed, the capture sink specified here is
        what is notified of the completions.
 尝试获取资源的捕获接收器。 完成分散/收集映射后，此处指定的捕获接收器是完成通知的内容。

    VideoInfoHeader -
        Information about the capture stream.  This **MUST** remain
        stable until the caller releases hardware resources.  Note
        that this could also be guaranteed by bagging it in the device
        object bag as well.
有关捕获流的信息。 此 **必须** 保持稳定，直到调用方释放硬件资源。 请注意，这也可以通过将其装袋在设备对象包中来保证。

Return Value:

    Success / Failure

--*/

{

    PAGED_CODE();

    NTSTATUS Status = STATUS_SUCCESS;

    //
    // If we're the first pin to go into acquire (remember we can have
    // a filter in another graph going simultaneously), grab the resources.
    //
    if (InterlockedCompareExchange (
        &m_PinsWithResources,
        1,
        0) == 0) {

        m_VideoInfoHeader = VideoInfoHeader;

        //
        // If there's an old hardware simulation sitting around for some
        // reason, blow it away.
        //
        if (m_ImageSynth) {
            delete m_ImageSynth;
            m_ImageSynth = NULL;
        }
    
        //
        // Create the necessary type of image synthesizer.
        //
        if (m_VideoInfoHeader -> bmiHeader.biBitCount == 24 &&
            m_VideoInfoHeader -> bmiHeader.biCompression == KS_BI_RGB) {
    
            //
            // If we're RGB24, create a new RGB24 synth.  RGB24 surfaces
            // can be in either orientation.  The origin is lower left if
            // height < 0.  Otherwise, it's upper left.
            //
            m_ImageSynth = new (NonPagedPoolNx, 'RysI') 
                CRGB24Synthesizer (
                    m_VideoInfoHeader -> bmiHeader.biHeight >= 0
                    );
    
        } else
        if (m_VideoInfoHeader -> bmiHeader.biBitCount == 16 &&
           (m_VideoInfoHeader -> bmiHeader.biCompression == FOURCC_YUY2)) {
    
            //
            // If we're UYVY, create the YUV synth.
            //
            m_ImageSynth = new(NonPagedPoolNx, 'YysI') CYUVSynthesizer;
    
        }
        else
            //
            // We don't synthesize anything but RGB 24 and UYVY.
            //
            Status = STATUS_INVALID_PARAMETER;
    
        if (NT_SUCCESS (Status) && !m_ImageSynth) {
    
            Status = STATUS_INSUFFICIENT_RESOURCES;
    
        } 

        if (NT_SUCCESS (Status)) {
            //
            // If everything has succeeded thus far, set the capture sink.
            //
            m_CaptureSink = CaptureSink;

        } else {
            //
            // If anything failed in here, we release the resources we've
            // acquired.
            //
            ReleaseHardwareResources ();
        }
    
    } else {

        //
        // TODO: Better status code?
        //
        Status = STATUS_SHARING_VIOLATION;

    }

    return Status;

}

/*************************************************/


void
CCaptureDevice::
ReleaseHardwareResources (
    )

/*++

Routine Description:

    Release hardware resources.  This should only be called by
    an object which has acquired them.

Arguments:

    None

Return Value:

    None

--*/

{

    PAGED_CODE();

    //
    // Blow away the image synth.
    //
    if (m_ImageSynth) {
        delete m_ImageSynth;
        m_ImageSynth = NULL;

    }

    m_VideoInfoHeader = NULL;
    m_CaptureSink = NULL;

    //
    // Release our "lock" on hardware resources.  This will allow another
    // pin (perhaps in another graph) to acquire them.
    //
    InterlockedExchange (
        &m_PinsWithResources,
        0
        );

}

/*************************************************/


NTSTATUS
CCaptureDevice::
Start (
    )

/*++

Routine Description:

    Start the capture device based on the video info header we were told
    about when resources were acquired.

Arguments:

    None

Return Value:

    Success / Failure

--*/

{

    PAGED_CODE();

    m_LastMappingsCompleted = 0;
    m_InterruptTime = 0;

    return
        m_HardwareSimulation -> Start (
            m_ImageSynth,
            m_VideoInfoHeader -> AvgTimePerFrame,
            m_VideoInfoHeader -> bmiHeader.biWidth,
            ABS (m_VideoInfoHeader -> bmiHeader.biHeight),
            m_VideoInfoHeader -> bmiHeader.biSizeImage
            );


}

/*************************************************/


NTSTATUS
CCaptureDevice::
Pause (
    IN BOOLEAN Pausing
    )

/*++

Routine Description:

    Pause or unpause the hardware simulation.  This is an effective start
    or stop without resetting counters and formats.  Note that this can
    only be called to transition from started -> paused -> started.  Calling
    this without starting the hardware with Start() does nothing.

Arguments:

    Pausing -
        An indicatation of whether we are pausing or unpausing

        TRUE -
            Pause the hardware simulation

        FALSE -
            Unpause the hardware simulation

Return Value:

    Success / Failure

--*/

{

    PAGED_CODE();

    return
        m_HardwareSimulation -> Pause (
            Pausing
            );

}

/*************************************************/


NTSTATUS
CCaptureDevice::
Stop (
    )

/*++

Routine Description:

    Stop the capture device.

Arguments:

    None

Return Value:

    Success / Failure

--*/

{

    PAGED_CODE();

    return
        m_HardwareSimulation -> Stop ();

}

/*************************************************/


ULONG
CCaptureDevice::
ProgramScatterGatherMappings (
    IN PKSSTREAM_POINTER Clone,
    IN PUCHAR *Buffer,
    IN PKSMAPPING Mappings,
    IN ULONG MappingsCount
    )

/*++

Routine Description:

    Program the scatter / gather mappings for the "fake" hardware.

Arguments:

    Buffer -
        Points to a pointer to the virtual address of the topmost
        scatter / gather chunk.  The pointer will be updated as the
        device "programs" mappings.  Reason for this is that we get
        the physical addresses and sizes, but must calculate the virtual
        addresses...  This is used as scratch space for that.

    Mappings -
        An array of mappings to program

    MappingsCount -
        The count of mappings in the array

Return Value:

    The number of mappings successfully programmed

--*/

{

    PAGED_CODE();

    

    return 
        m_HardwareSimulation -> ProgramScatterGatherMappings (
            Clone,
            Buffer,
            Mappings,
            MappingsCount,
            sizeof (KSMAPPING)
            );

}

/*************************************************************************

    LOCKED CODE

**************************************************************************/

#ifdef ALLOC_PRAGMA
#pragma code_seg()
#endif // ALLOC_PRAGMA


ULONG
CCaptureDevice::
QueryInterruptTime (
    )

/*++

Routine Description:

    Return the number of frame intervals that have elapsed since the
    start of the device.  This will be the frame number.
    返回自设备启动以来经过的帧间隔数。 这将是帧编号。

Arguments:

    None

Return Value:

    The interrupt time of the device (the number of frame intervals that
    have elapsed since the start of the device).

--*/

{

    return m_InterruptTime;

}

/*************************************************/


void
CCaptureDevice::
Interrupt (
    )

/*++

Routine Description:

    This is the "faked" interrupt service routine for this device.  It
    is called at dispatch level by the hardware simulation.

Arguments:

    None

Return Value:

    None

--*/

{

    m_InterruptTime++;

    //
    // Realistically, we'd do some hardware manipulation here and then queue
    // a DPC.  Since this is fake hardware, we do what's necessary here.  This
    // is pretty much what the DPC would look like short of the access
    // of hardware registers (ReadNumberOfMappingsCompleted) which would likely
    // be done in the ISR.
    //
    ULONG NumMappingsCompleted = 
        m_HardwareSimulation -> ReadNumberOfMappingsCompleted ();

    //
    // Inform the capture sink that a given number of scatter / gather
    // mappings have completed.
    //
    m_CaptureSink -> CompleteMappings (
        NumMappingsCompleted - m_LastMappingsCompleted
        );

    m_LastMappingsCompleted = NumMappingsCompleted;

}

/**************************************************************************

    DESCRIPTOR AND DISPATCH LAYOUT

**************************************************************************/

//
// CaptureFilterDescriptor:
//
// The filter descriptor for the capture device.
DEFINE_KSFILTER_DESCRIPTOR_TABLE (FilterDescriptors) { 
    &CaptureFilterDescriptor
};

//
// CaptureDeviceDispatch:
//
// This is the dispatch table for the capture device.  Plug and play
// notifications as well as power management notifications are dispatched
// through this table.
//
const
KSDEVICE_DISPATCH //KSDEVICE_DISPATCH结构描述了客户端可以提供的回调，以接收设备创建和 PnP 事件的通知。
CaptureDeviceDispatch = {
    CCaptureDevice::DispatchCreate,         // Pnp Add Device
    CCaptureDevice::DispatchPnpStart,       // Pnp Start
    NULL,                                   // Post-Start
    NULL,                                   // Pnp Query Stop
    NULL,                                   // Pnp Cancel Stop
    CCaptureDevice::DispatchPnpStop,        // Pnp Stop
    NULL,                                   // Pnp Query Remove
    NULL,                                   // Pnp Cancel Remove
    NULL,                                   // Pnp Remove
    NULL,                                   // Pnp Query Capabilities
    NULL,                                   // Pnp Surprise Removal
    NULL,                                   // Power Query Power
    NULL,                                   // Power Set Power
    NULL                                    // Pnp Query Interface
};

//
// CaptureDeviceDescriptor:
//
// This is the device descriptor for the capture device.  It points to the
// dispatch table and contains a list of filter descriptors that describe
// filter-types that this device supports.  Note that the filter-descriptors
// can be created dynamically and the factories created via 
// KsCreateFilterFactory as well.  
//
const
KSDEVICE_DESCRIPTOR
CaptureDeviceDescriptor = {
    &CaptureDeviceDispatch,
    0,
    NULL
};

/**************************************************************************

    INITIALIZATION CODE

**************************************************************************/

extern "C" DRIVER_INITIALIZE DriverEntry;

//DriverEntry 是在加载驱动程序后调用的第一个驱动程序提供的例程。 它负责初始化驱动程序。
//DriverObject [in]
//指向 DRIVER_OBJECT 结构的指针，该结构表示驱动程序的 WDM 驱动程序对象。
//RegistryPath [in]
//指向 UNICODE_STRING 结构的指针，该结构指定注册表中驱动程序 的 Parameters 键 的路径。
extern "C"
NTSTATUS
DriverEntry (
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath
    )

/*++

Routine Description:

    Driver entry point.  Pass off control to the AVStream initialization
    function (KsInitializeDriver) and return the status code from it.

Arguments:

    DriverObject -
        The WDM driver object for our driver

    RegistryPath -
        The registry path for our registry info

Return Value:

    As from KsInitializeDriver

--*/

{
    //
    // Simply pass the device descriptor and parameters off to AVStream
    // to initialize us.  This will cause filter factories to be set up
    // at add & start.  Everything is done based on the descriptors passed
    // here.
    //
/*只需将设备描述符和参数传递给AVStream即可初始化我们。这将导致在添加和启动时设置过滤器工厂。一切都是基于这里传递的描述符完成的。*/
	return
        KsInitializeDriver (
            DriverObject,
            RegistryPath,
            &CaptureDeviceDescriptor
            );
}

void CCaptureDevice::SetData(PVOID data, ULONG dataLength)
{
	m_HardwareSimulation->SetData(data, dataLength);
}
