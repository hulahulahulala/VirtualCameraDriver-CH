/**************************************************************************

    AVStream Simulated Hardware Sample

    Copyright (c) 2001, Microsoft Corporation.

    File:

        hwsim.cpp

    Abstract:
        
        This file is the hardware simulation header.  

        The simulation fakes "DMA" transfers, scatter gather mapping handling, 
        ISR's, etc...  The ISR routine in here will be called when an ISR 
        would be generated by the fake hardware and it will directly call into 
        the device level ISR for more accurate simulation.

    History:

        created 3/9/2001

**************************************************************************/

//
// SCATTER_GATHER_MAPPINGS_MAX:
//
// The maximum number of entries in the hardware's scatter/gather list.  I
// am making this so large for a few reasons:
//
//     1) we're faking this with uncompressed surfaces -- 
//            these are large buffers which will map to a lot of s/g entries
//     2) the fake hardware implementation requires at least one frame's
//            worth of s/g entries to generate a frame
//
#define SCATTER_GATHER_MAPPINGS_MAX 128

//
// SCATTER_GATHER_ENTRY:
//
// This structure is used to keep the scatter gather table for the fake
// hardware as a doubly linked list.
//
typedef struct _SCATTER_GATHER_ENTRY {

    LIST_ENTRY ListEntry;
    PKSSTREAM_POINTER CloneEntry;
    PUCHAR Virtual;
    ULONG ByteCount;

} SCATTER_GATHER_ENTRY, *PSCATTER_GATHER_ENTRY;

//
// CHardwareSimulation:
//
// The hardware simulation class.
//
class CHardwareSimulation {

private:

    //
    // The image synthesizer.  This is a piece of code which actually draws
    // the requested images.
    //图像合成器。这是一段实际绘制所需图像的代码。
    //
    CImageSynthesizer *m_ImageSynth;

    //
    // The synthesis buffer.  This is a private buffer we use to generate the
    // capture image in.  The fake "scatter / gather" mappings are filled
    // in from this buffer during each interrupt.
    //合成缓冲液。这是一个我们用来在中生成捕获图像的专用缓冲区。在每次中断期间，
    //伪“散射/聚集”映射都会从该缓冲区中填充。
    //
    PUCHAR m_SynthesisBuffer;

	//
	// A temporary frame buffer;
	//
	PUCHAR m_TemporaryBuffer;

    //
    // Key information regarding the frames we generate.
    //
    LONGLONG m_TimePerFrame;
    ULONG m_Width;
    ULONG m_Height;
    ULONG m_ImageSize;

    //
    // Scatter gather mappings for the simulated hardware.
    //模拟硬件的分散-聚集映射。
    //
    KSPIN_LOCK m_ListLock;
    LIST_ENTRY m_ScatterGatherMappings;

    //
    // Lookaside for memory for the scatter / gather entries on the scatter /
    // gather list.
    //在分散/聚集列表中查找分散/聚集项的内存。
    //
    NPAGED_LOOKASIDE_LIST m_ScatterGatherLookaside;

    //
    // The current state of the fake hardware.
    //
    HARDWARE_STATE m_HardwareState;

    //
    // The pause / stop hardware flag and event.
    //暂停/停止硬件标志和事件。
    //
    BOOLEAN m_StopHardware;
    KEVENT m_HardwareEvent;

    //
    // Maximum number of scatter / gather mappins in the s/g table of the
    // fake hardware.
    //伪硬件的s/g表中分散/聚集映射引脚的最大数量。
    //
    ULONG m_ScatterGatherMappingsMax;

    //
    // Number of scatter / gather mappings that have been completed (total)
    // since the start of the hardware or any reset.
    //自硬件启动或任何重置以来已完成的分散/聚集映射数（总计）。
    //
    ULONG m_NumMappingsCompleted;

    //
    // Number of scatter / gather mappings that are queued for this hardware.
    //
    ULONG m_ScatterGatherMappingsQueued;
    ULONG m_ScatterGatherBytesQueued;

    //
    // Number of frames skipped due to lack of scatter / gather mappings.
    //
    ULONG m_NumFramesSkipped;

    //
    // The "Interrupt Time".  Number of "fake" interrupts that have occurred
    // since the hardware was started.
    //“中断时间”。自硬件启动以来发生的“假”中断数。
    // 
    ULONG m_InterruptTime;

    //
    // The system time at start.
    //
    LARGE_INTEGER m_StartTime;
    
    //
    // The DPC used to "fake" ISR
    //DPC曾经“伪造”ISR
    //
    KDPC m_IsrFakeDpc;
    KTIMER m_IsrTimer;

    //
    // The hardware sink that will be used for interrupt notifications.
    //
    IHardwareSink *m_HardwareSink;

    //
    // FillScatterGatherBuffers():
    //
    // This is called by the hardware simulation to fill a series of scatter /
    // gather buffers with synthesized data.
    //
    NTSTATUS
    FillScatterGatherBuffers (
        );

public:

    LONG GetSkippedFrameCount()
    {
        return InterlockedExchange((LONG*)&this->m_NumFramesSkipped, this->m_NumFramesSkipped);
    }
    //
    // CHardwareSimulation():
    //
    // The hardware simulation constructor.  Since the new operator will
    // have zeroed the memory, only initialize non-NULL, non-0 fields. 
    //
    CHardwareSimulation (
        IN IHardwareSink *HardwareSink
        );

    //
    // ~CHardwareSimulation():
    //
    // The hardware simulation destructor.
    //
    ~CHardwareSimulation (
        )
    {
    }

    //
    // Cleanup():
    //
    // This is the free callback for the bagged hardware sim.  Not providing
    // one will call ExFreePool, which is not what we want for a constructed
    // C++ object.  This simply deletes the simulation.
    //这是袋装硬件sim的免费回调。如果不提供，将调用ExFreePool，这不是我们构建的C++
    //对象所希望的。这只是删除模拟。
    //
    static
    void
    Cleanup (
        IN CHardwareSimulation *HwSim
        )
    {
        delete HwSim;
    }

    //
    // FakeHardware():
    //
    // Called from the simulated interrupt.  First we fake the hardware's
    // actions (at DPC) then we call the "Interrupt service routine" on
    // the hardware sink.
    // 从模拟中断调用。首先，我们伪造硬件的操作（在DPC），然后在硬件接收器上调用“中断服务例程”。
    //
    void
    FakeHardware (
        );

    //
    // Start():
    //
    // "Start" the fake hardware.  This will start issuing interrupts and 
    // DPC's. 
    //
    // The frame rate, image size, and a synthesizer must be provided.
    //
    NTSTATUS
    Start (
        CImageSynthesizer *ImageSynth,
        IN LONGLONG TimePerFrame,
        IN ULONG Width,
        IN ULONG Height,
        IN ULONG ImageSize
        );

    //
    // Pause():
    //
    // "Pause" or "unpause" the fake hardware.  This will stop issuing 
    // interrupts or DPC's on a pause and restart them on an unpause.  Note
    // that this will not reset counters as a Stop() would.
    //
    NTSTATUS
    Pause (
        IN BOOLEAN Pausing
        );

    //
    // Stop():
    //
    // "Stop" the fake hardware.  This will stop issuing interrupts and
    // DPC's.
    //
    NTSTATUS
    Stop (
        );

    //
    // ProgramScatterGatherMappings():
    //
    // Program a series of scatter gather mappings into the fake hardware.
    //
    ULONG
    ProgramScatterGatherMappings (
        IN PKSSTREAM_POINTER Clone,
        IN PUCHAR *Buffer,
        IN PKSMAPPING Mappings,
        IN ULONG MappingsCount,
        IN ULONG MappingStride
        );

    //
    // Initialize():
    //
    // Initialize a piece of simulated hardware.
    //
    static 
    CHardwareSimulation *
    Initialize (
        IN KSOBJECT_BAG Bag,
        IN IHardwareSink *HardwareSink
        );

    //
    // ReadNumberOfMappingsCompleted():
    //
    // Read the number of mappings completed since the last hardware reset.
    //
    ULONG
    ReadNumberOfMappingsCompleted (
        );


	//
	// SetData();
	//
	// Sets the virtual frame buffer of the simulation.
	//
	void SetData(PVOID data, ULONG dataLength);
};

