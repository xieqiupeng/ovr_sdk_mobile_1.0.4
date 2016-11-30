/************************************************************************************

PublicHeader:   OVR_Capture.h
Filename    :   OVR_Capture_PerfEvent.h
Content     :   Oculus performance capture library. Support for linux kernel perf events
Created     :   March, 2016
Notes       : 
Author      :   James Dolan

Copyright   :   Copyright 2015 Oculus VR, LLC. All Rights reserved.

************************************************************************************/

#include "OVR_Capture_PerfEvent.h"

#if defined(OVR_CAPTURE_HAS_PERF)

#include <OVR_Capture_Packets.h>

#include "OVR_Capture_Local.h"
#include "OVR_Capture_AsyncStream.h"

#include <poll.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <dirent.h>

#include <vector>

namespace OVR
{
namespace Capture
{

	/*********************
	* Begin perf_event.h *
	*********************/

	// perf_event.h isn't publicly visible in the NDK until android-21.
	// but we currently build against android-19. Eventually we will update
	// and just include perf_event.h, but in the mean time just
	// tossing the bits we need from it into here.

	enum perf_type_id
	{
		PERF_TYPE_HARDWARE = 0,
		PERF_TYPE_SOFTWARE = 1,
		PERF_TYPE_TRACEPOINT = 2,
		PERF_TYPE_HW_CACHE = 3,
		PERF_TYPE_RAW = 4,
		PERF_TYPE_BREAKPOINT = 5,
		PERF_TYPE_MAX,
	};

	enum perf_hw_id
	{
		PERF_COUNT_HW_CPU_CYCLES = 0,
		PERF_COUNT_HW_INSTRUCTIONS = 1,
		PERF_COUNT_HW_CACHE_REFERENCES = 2,
		PERF_COUNT_HW_CACHE_MISSES = 3,
		PERF_COUNT_HW_BRANCH_INSTRUCTIONS = 4,
		PERF_COUNT_HW_BRANCH_MISSES = 5,
		PERF_COUNT_HW_BUS_CYCLES = 6,
		PERF_COUNT_HW_STALLED_CYCLES_FRONTEND = 7,
		PERF_COUNT_HW_STALLED_CYCLES_BACKEND = 8,
		PERF_COUNT_HW_REF_CPU_CYCLES = 9,
		PERF_COUNT_HW_MAX,
	};

	enum perf_sw_ids
	{
		PERF_COUNT_SW_CPU_CLOCK = 0,
		PERF_COUNT_SW_TASK_CLOCK = 1,
		PERF_COUNT_SW_PAGE_FAULTS = 2,
		PERF_COUNT_SW_CONTEXT_SWITCHES = 3,
		PERF_COUNT_SW_CPU_MIGRATIONS = 4,
		PERF_COUNT_SW_PAGE_FAULTS_MIN = 5,
		PERF_COUNT_SW_PAGE_FAULTS_MAJ = 6,
		PERF_COUNT_SW_ALIGNMENT_FAULTS = 7,
		PERF_COUNT_SW_EMULATION_FAULTS = 8,
		PERF_COUNT_SW_DUMMY = 9,
		PERF_COUNT_SW_MAX,
	};

	enum perf_event_read_format
	{
		PERF_FORMAT_TOTAL_TIME_ENABLED = 1U << 0,
		PERF_FORMAT_TOTAL_TIME_RUNNING = 1U << 1,
		PERF_FORMAT_ID = 1U << 2,
		PERF_FORMAT_GROUP = 1U << 3,
		PERF_FORMAT_MAX = 1U << 4,
	};

	enum perf_event_sample_format
	{
		PERF_SAMPLE_IP = 1U << 0,
		PERF_SAMPLE_TID = 1U << 1,
		PERF_SAMPLE_TIME = 1U << 2,
		PERF_SAMPLE_ADDR = 1U << 3,
		PERF_SAMPLE_READ = 1U << 4,
		PERF_SAMPLE_CALLCHAIN = 1U << 5,
		PERF_SAMPLE_ID = 1U << 6,
		PERF_SAMPLE_CPU = 1U << 7,
		PERF_SAMPLE_PERIOD = 1U << 8,
		PERF_SAMPLE_STREAM_ID = 1U << 9,
		PERF_SAMPLE_RAW = 1U << 10,
		PERF_SAMPLE_BRANCH_STACK = 1U << 11,
		PERF_SAMPLE_REGS_USER = 1U << 12,
		PERF_SAMPLE_STACK_USER = 1U << 13,
		PERF_SAMPLE_WEIGHT = 1U << 14,
		PERF_SAMPLE_DATA_SRC = 1U << 15,
		PERF_SAMPLE_IDENTIFIER = 1U << 16,
		PERF_SAMPLE_TRANSACTION = 1U << 17,
		PERF_SAMPLE_MAX = 1U << 18,
	};

	enum perf_event_type
	{
		PERF_RECORD_MMAP = 1,
		PERF_RECORD_LOST = 2,
		PERF_RECORD_COMM = 3,
		PERF_RECORD_EXIT = 4,
		PERF_RECORD_THROTTLE = 5,
		PERF_RECORD_UNTHROTTLE = 6,
		PERF_RECORD_FORK = 7,
		PERF_RECORD_READ = 8,
		PERF_RECORD_SAMPLE = 9,
		PERF_RECORD_MMAP2 = 10,
		PERF_RECORD_MAX,
	};

	struct perf_event_mmap_page
	{
		UInt32 version;
		UInt32 compat_version;
		UInt32 lock;
		UInt32 index;
		Int64  offset;
		UInt64 time_enabled;
		UInt64 time_running;
		union
		{
			UInt64 capabilities;
			struct
			{
				UInt64 cap_bit0 : 1,
				cap_bit0_is_deprecated : 1,
				cap_user_rdpmc : 1,
				cap_user_time : 1,
				cap_user_time_zero : 1,
				cap_____res : 59;
			};
		};
		UInt16 pmc_width;
		UInt16 time_shift;
		UInt32 time_mult;
		UInt64 time_offset;
		UInt64 time_zero;
		UInt32 size;
		UInt8  __reserved[118*8+4];
		UInt64 data_head;
		UInt64 data_tail;
	};

	struct perf_event_header
	{
		UInt32 type;
		UInt16 misc;
		UInt16 size;
	};

	// Normally this is found in <linux/perf_event.h>, but that isn't available in android-19
	struct perf_event_attr
	{
		UInt32 type;
		UInt32 size;
		UInt64 config;
		union
		{
			UInt64 sample_period;
			UInt64 sample_freq;
		};
		UInt64 sample_type;
		UInt64 read_format;
		UInt64 disabled : 1,
		       inherit : 1,
		       pinned : 1,
		       exclusive : 1,
		       exclude_user : 1,
		       exclude_kernel : 1,
		       exclude_hv : 1,
		       exclude_idle : 1,
		       mmap : 1,
		       comm : 1,
		       freq : 1,
		       inherit_stat : 1,
		       enable_on_exec : 1,
		       task : 1,
		       watermark : 1,
		       precise_ip : 2,
		       mmap_data : 1,
		       sample_id_all : 1,
		       exclude_host : 1,
		       exclude_guest : 1,
		       exclude_callchain_kernel : 1,
		       exclude_callchain_user : 1,
		       mmap2 :  1,
		       comm_exec :  1,
		       use_clockid :  1,
		       __reserved_1 : 38;
		union
		{
			UInt32 wakeup_events;
			UInt32 wakeup_watermark;
		};
		UInt32 bp_type;
		union
		{
			UInt64 bp_addr;
			UInt64 config1;
		};
		union
		{
			UInt64 bp_len;
			UInt64 config2;
		};
		UInt64 branch_sample_type;
		UInt64 sample_regs_user;
		UInt32 sample_stack_user;
		Int32  clockid;
	};


	/*****************
	* Record Structs *
	*****************/

	// These are perf_event record structs. They can vary in size based on what flags are set in perf_event_attr.
	// This might seem like crazy template insanity, but setting up the structs this way allows for a safe way
	// of toggling between different versions without macro insanity.

	template<UInt32 sampleType, UInt32 readFormat>
	struct PerfEventSampleRecord_;

	template<>
	struct PerfEventSampleRecord_<PERF_SAMPLE_TIME | PERF_SAMPLE_CPU | PERF_SAMPLE_READ, PERF_FORMAT_TOTAL_TIME_RUNNING>
	{
		UInt64 time;         /* if PERF_SAMPLE_TIME */
		UInt32 cpu, res;     /* if PERF_SAMPLE_CPU */
		UInt64 value;        /* if PERF_SAMPLE_READ */
		UInt64 time_running; /* if PERF_SAMPLE_READ && PERF_FORMAT_TOTAL_TIME_RUNNING */
	};

	template<>
	struct PerfEventSampleRecord_<PERF_SAMPLE_TID | PERF_SAMPLE_TIME | PERF_SAMPLE_CPU | PERF_SAMPLE_READ, PERF_FORMAT_TOTAL_TIME_RUNNING>
	{
		UInt32 pid, tid;     /* if PERF_SAMPLE_TID */
		UInt64 time;         /* if PERF_SAMPLE_TIME */
		UInt32 cpu, res;     /* if PERF_SAMPLE_CPU */
		UInt64 value;        /* if PERF_SAMPLE_READ */
		UInt64 time_running; /* if PERF_SAMPLE_READ && PERF_FORMAT_TOTAL_TIME_RUNNING */
	};

	template<UInt32 sampleType, UInt32 readFormat>
	struct PerfEventSampleRecordContainer : public PerfEventSampleRecord_<sampleType, readFormat>
	{
		static const UInt32 s_sampleType = sampleType;
		static const UInt32 s_readFormat = readFormat;
	};

	typedef PerfEventSampleRecordContainer<PERF_SAMPLE_TIME | PERF_SAMPLE_CPU | PERF_SAMPLE_READ, PERF_FORMAT_TOTAL_TIME_RUNNING> PerfEventSampleRecord;

	struct PerfEventForkRecord
	{
		UInt32 pid, ppid;
		UInt32 tid, ptid;
		UInt64 time;
	};

	struct PerfEventExitRecord
	{
		UInt32 pid, ppid;
		UInt32 tid, ptid;
		UInt64 time;
	};

	/***********************
	* Buffer Configuration *
	***********************/


	static const size_t g_pagesize   = getpagesize();
	static const size_t g_headersize = g_pagesize;
	static const size_t g_recordsize = g_pagesize * (g_pagesize>=4096 ? 2 : 4);
	static const size_t g_mapsize    = g_headersize + g_recordsize;
	static const size_t g_recordmask = g_recordsize-1;

	/*******************
	* Helper Functions *
	*******************/

	// TODO: wrap this up a bit better...
	template<typename T>
	static T ReadRingBuffer(const UInt8 *buffer, UInt64 head, UInt64 tail)
	{
		head = head & g_recordmask;
		tail = tail & g_recordmask;
		if(head > tail || tail+sizeof(T) <= g_recordsize)
		{
			// Contiguous read... just dereference in place
			return *reinterpret_cast<const T*>(buffer + tail);
		}
		else
		{
			// Wrapped read... we need to perform two copies to get the final struct
			union
			{
				T      ret;
				UInt8  raw[sizeof(T)];
			};
			const UInt64 size0 = g_recordsize - tail;
			const UInt64 size1 = sizeof(T)    - size0;
			MemoryCopy(raw,       buffer+tail, size0);
			MemoryCopy(raw+size0, buffer,      size1);
			return ret;
		}
	}

	// see http://man7.org/linux/man-pages/man2/perf_event_open.2.html
	static int perf_event_open(struct perf_event_attr *attr, pid_t pid, int cpu, int group_fd, unsigned long flags)
	{
		return syscall( __NR_perf_event_open, attr, pid, cpu, group_fd, flags);
	}


	/************
	* PerfEvent *
	************/


	PerfEvent::PerfEvent(void)
	{
	}

	PerfEvent::~PerfEvent(void)
	{
		// Final cleanup...
		while(m_tasks.empty() == false)
		{
			DetachTask(m_tasks.begin());
		}
		m_tasks.clear();
	}

	void PerfEvent::Start(void)
	{
		m_gate.Close();
		Thread::Start();
		m_threadCreationTime = GetNanoseconds();
		m_gate.Open();
	}

	void PerfEvent::AttachAllTasks(void)
	{
		DIR *dir = opendir("/proc/self/task/");
		if(dir)
		{
			while(true)
			{
				dirent *task = readdir(dir);
				if(!task)
					break;
				if(!isdigit(task->d_name[0]))
					continue;

				const pid_t tid = static_cast<pid_t>(atoi(task->d_name));

				AttachTask(tid);
			}
			closedir(dir);
		}
	}

	void PerfEvent::OnThreadExecute(void)
	{
		SetThreadName("CapturePerf");

		// Must first wait for our thread creation timestamp to be acquired...
		m_gate.WaitForOpen();

		const UInt32 perfThreadID = static_cast<UInt32>(gettid());
		UInt64       timeOffset   = 0;

		std::vector<UInt32> deadThreads;

		while(!QuitSignaled())
		{
			// wait 50ms between samples...
			ThreadSleepMilliseconds(50);

			// Acquire connection lock before attempting to write data to the AsyncStream
			if(!TryLockConnection())
				break;

			AsyncStream &stream = *AsyncStream::Acquire();

			for(PerfTaskMap::iterator task=m_tasks.begin(); task!=m_tasks.end(); task++)
			{
				if(!task->second.mapped)
					continue;

				perf_event_mmap_page *eventPage = (perf_event_mmap_page*)task->second.mapped;

				UInt64 head = 0;
				UInt64 tail = 0;

				// seqlock that guarantees not to block the writer... keeps trying
				// to read until the lock stops changing.
				while(true)
				{
					// acquire current value...
					const auto lock = eventPage->lock;
					FullMemoryBarrier();

					// if its locked for write, try again...
					if(lock & 1)
						continue;

					// Locate the region of the buffer that we can safely read...
					head = eventPage->data_head;
					tail = eventPage->data_tail;

					// If the lock value is unchanged, we have successfully acquired exclusive
					// access to this region (tail->head) of the ring buffer!
					FullMemoryBarrier();
					if(lock == eventPage->lock)
						break;
				}

				// Iterate through all available records and dump them out
				const UInt8 *buffer = reinterpret_cast<const UInt8*>(task->second.mapped) + g_headersize;
				while(tail<head)
				{
					// Check if buffer not big enough to contain header...
					if(tail + sizeof(perf_event_header) > head)
						break;

					// Read header...
					const perf_event_header header = ReadRingBuffer<perf_event_header>(buffer, head, tail);

					// Check if buffer not big enough to contain entire record...
					if(tail + header.size > head)
						break;

					// Locate the beginning of the payload...
					const UInt64 headertail = tail + sizeof(perf_event_header);

					// Handle events...
					if(header.type == PERF_RECORD_SAMPLE && header.size == sizeof(perf_event_header)+sizeof(PerfEventSampleRecord))
					{
						const PerfEventSampleRecord record = ReadRingBuffer<PerfEventSampleRecord>(buffer, head, headertail);
						if(timeOffset)
						{
							CPUContextSwitch packet;
							packet.timestampLeave = record.time + timeOffset;
							packet.timestampEnter = packet.timestampLeave - (record.time_running - task->second.totalTime);
							packet.threadID       = static_cast<UInt32>(task->first);
							packet.cpuID          = record.cpu;
							stream.WritePacket(packet);
						}
						task->second.totalTime = record.time_running;
					}
					else if(header.type == PERF_RECORD_FORK && header.size == sizeof(perf_event_header)+sizeof(PerfEventForkRecord))
					{
						const PerfEventForkRecord record = ReadRingBuffer<PerfEventForkRecord>(buffer, head, headertail);
						AttachTask(record.tid);
						if(record.tid == perfThreadID)
						{
							// Calculate the difference between the two clocks...
							// Super hacky way of estimating the clock offset between GetNanoseconds
							// and perf_event_open... we count on the 'FORK' event for the PerfEvent
							// thread to calculate the offset. This isn't super accurate, but should
							// put us in the sub-millisecond range.
							timeOffset = m_threadCreationTime - record.time;
						}
					}
					else if(header.type == PERF_RECORD_EXIT && header.size == sizeof(perf_event_header)+sizeof(PerfEventExitRecord))
					{
						const PerfEventExitRecord record = ReadRingBuffer<PerfEventExitRecord>(buffer, head, headertail);
						deadThreads.push_back(record.tid);
					}
					else
					{
						Logf(Log_Warning, "Record %u %u %u", header.type, header.misc, header.size);
					}

					tail += header.size;
				}

				// Move the tail pointer up to mark that we processed that data...
				eventPage->data_tail = tail;

			} // for(PerfTaskMap::const_iterator task=m_tasks.begin(); task!=m_tasks.end(); task++)

			// Clean up dead threads...
			for(auto i=deadThreads.begin(); i!=deadThreads.end(); i++)
			{
				DetachTask(*i);
			}
			deadThreads.clear();

			// Unlock connection only after all events are processed.
			UnlockConnection();

		} // while(!QuitSignaled() && IsConnected())

	}

	void PerfEvent::AttachTask(pid_t tid)
	{
		if(!TryLockConnection())
			return;

		if(m_tasks.find(tid) == m_tasks.end())
		{
			PerfTask task = {};

			perf_event_attr attr = {};

			attr.type          = PERF_TYPE_SOFTWARE;
			attr.size          = sizeof(attr);
			attr.config        = PERF_COUNT_SW_CONTEXT_SWITCHES;

			attr.read_format   = PerfEventSampleRecord::s_readFormat;
			attr.sample_type   = PerfEventSampleRecord::s_sampleType;
			attr.sample_period = 1;

			// Get notifications for newly created threads...
			attr.task          = 1;

			task.fd = perf_event_open(&attr, tid, -1, -1, 0 );
			if(task.fd < 0)
			{
				Logf(Log_Error, "perf_event_open failure errno=%d", errno);
			}
			else if(s_verbose)
			{
				Logf(Log_Info, "perf_event_open success tid=%d fd=%d", tid, task.fd);
			}

			if(task.fd >= 0)
			{
				task.mapped = mmap(nullptr, g_mapsize, PROT_READ | PROT_WRITE, MAP_SHARED, task.fd, 0);
			}

			if(task.mapped == nullptr || task.mapped == MAP_FAILED)
			{
				Logf(Log_Warning, "mmap failure fd=%d errno=%d", task.fd, errno);
				task.mapped = nullptr;
			}
			else if(s_verbose)
			{
				Logf(Log_Info, "mmap success fd=%d ptr=%p", task.fd, task.mapped);
			}

			if(s_retainFileHandle == false && task.fd >= 0)
			{
				// mmap docs say the file handle doesn't need to remain open.
				// Not sure if this buys us anything but since we don't actually
				// ever read the counters lets release it.
				close(task.fd);
				task.fd = -1;
			}

			if(task.mapped || task.fd >= 0)
			{
				// TODO: we might also want to turn on attr.comm to monitor for name changes
				char threadName[32] = {};
				ThreadNamePacket packet = {};
				packet.threadID = static_cast<UInt32>(tid);
				GetThreadName(packet.threadID, threadName, sizeof(threadName));
				if(threadName[0])
				{
					AsyncStream::Acquire()->WritePacket(packet, threadName, static_cast<ThreadNamePacket::PayloadSizeType>( StringLength(threadName) ));
				}
			}

			if(task.mapped || task.fd >= 0)
			{
				m_tasks[tid] = task;
			}
		}

		UnlockConnection();
	}

	void PerfEvent::DetachTask(pid_t tid)
	{
		auto found = m_tasks.find(tid);
		if(found != m_tasks.end())
		{
			DetachTask(found);
		}
	}

	void PerfEvent::DetachTask(PerfTaskMap::iterator task)
	{
		if(task != m_tasks.end())
		{
			if(task->second.mapped)
				munmap(task->second.mapped, g_mapsize);
			if(task->second.fd)
				close(task->second.fd);
			m_tasks.erase(task);
		}
	}

} // namespace Capture
} // namespace OVR

#endif // #if defined(OVR_CAPTURE_ANDROID)
