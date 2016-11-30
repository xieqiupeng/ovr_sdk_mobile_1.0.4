/************************************************************************************

PublicHeader:   OVR_Capture.h
Filename    :   OVR_Capture_AsyncStream.cpp
Content     :   Oculus performance capture library. Interface for async data streaming.
Created     :   January, 2015
Notes       : 
Author      :   James Dolan

Copyright   :   Copyright 2015 Oculus VR, LLC. All Rights reserved.

************************************************************************************/

#include "OVR_Capture_AsyncStream.h"
#include "OVR_Capture_Local.h"
#include "OVR_Capture_Socket.h"
#include "OVR_Capture_FileIO.h"

#include <stdio.h>

#include <vector>

#if defined(OVR_CAPTURE_POSIX)
	#include <sys/syscall.h>
#endif

namespace OVR
{
namespace Capture
{

	static ThreadLocalKey            g_tlskey   = NullThreadLocalKey;
	static CriticalSection           g_listlock;
	static std::vector<AsyncStream*> g_streams;

#if defined(OVR_CAPTURE_SELF_PROFILE)
	static const Label        g_flushAllLabel("AsyncStream_FlushAll");
	static const Label        g_flushLabel("AsyncStream_Flush");
#endif

	template<typename T>
	inline void Swap(T &a, T &b)
	{
		T temp = a;
		a      = b;
		b      = temp;
	}


	/******************
	* SocketOutStream *
	******************/

	SocketOutStream::SocketOutStream(Socket &s) :
		m_socket(s)
	{
	}

	bool SocketOutStream::Send(const void *buffer, UInt32 size)
	{
		return m_socket.Send(buffer, size);
	}


	/******************
	* FileOutStream   *
	******************/

	FileOutStream::FileOutStream(FileHandle f) :
		m_file(f)
	{
	}

	bool FileOutStream::Send(const void *buffer, UInt32 size)
	{
		return (WriteFile(m_file, buffer, size) == (int)size);
	}


	/******************
	* AsyncStream     *
	******************/

	// Initialize the per-thread stream system... MUST be called before being connected!
	void AsyncStream::Init(void)
	{
		OVR_CAPTURE_ASSERT(g_tlskey == NullThreadLocalKey);
		g_tlskey = CreateThreadLocalKey();
		OVR_CAPTURE_ASSERT(g_streams.empty());
	}

	// Release the per-thread streams... Should be called when connection is closed.
	void AsyncStream::Shutdown(void)
	{
		// destroy our thread local key...
		OVR_CAPTURE_ASSERT(g_tlskey != NullThreadLocalKey);
		DestroyThreadLocalKey(g_tlskey);
		g_tlskey = NullThreadLocalKey;

		// delete all the async streams...
		g_listlock.Lock();
		for(auto curr=g_streams.begin(); curr!=g_streams.end(); curr++)
		{
			delete *curr;
		}
		g_streams.clear();
		g_listlock.Unlock();
	}

	// Acquire a per-thread stream for the current thread...
	AsyncStream *AsyncStream::Acquire(void)
	{
		AsyncStream *stream = (AsyncStream*)GetThreadLocalValue(g_tlskey);
		if(!stream)
		{
			stream = new AsyncStream();
			SetThreadLocalValue(g_tlskey, stream);
			g_listlock.Lock();
			g_streams.push_back(stream);
			g_listlock.Unlock();
		}
		return stream;
	}

	// Flush all existing thread streams... returns false on socket error
	bool AsyncStream::FlushAll(OutStream &outStream)
	{
	#if defined(OVR_CAPTURE_SELF_PROFILE)
		const CPUScope cpuzone(g_flushAllLabel);
	#endif

		bool okay = true;

		g_listlock.Lock();
		for(auto curr=g_streams.begin(); curr!=g_streams.end(); curr++)
		{
			AsyncStream *stream = *curr;
			okay = stream->Flush(outStream);
			if(!okay) break;
		}
		g_listlock.Unlock();

		return okay;
	}

	// Clears the contents of all streams.
	void AsyncStream::ClearAll(void)
	{
		g_listlock.Lock();
		for(auto curr=g_streams.begin(); curr!=g_streams.end(); curr++)
		{
			AsyncStream *stream = *curr;
			SpinLock(stream->m_bufferLock);
			stream->m_cacheTail = stream->m_cacheBegin;
			stream->m_flushTail = stream->m_flushBegin;
			SpinUnlock(stream->m_bufferLock);
			stream->m_gate.Open();
		}
		g_listlock.Unlock();
	}


	// Flushes all available packets over the network... returns number of bytes sent
	bool AsyncStream::Flush(OutStream &outStream)
	{
	#if defined(OVR_CAPTURE_SELF_PROFILE)
		const CPUScope cpuzone(g_flushLabel);
	#endif

		bool okay = true;

		// Take ownership of any pending data...
		SpinLock(m_bufferLock);
		Swap(m_cacheBegin, m_flushBegin);
		Swap(m_cacheTail,  m_flushTail);
		Swap(m_cacheEnd,   m_flushEnd);
		SpinUnlock(m_bufferLock);

		// Signal that we just swapped in a new buffer... wake up any threads that were waiting on us to flush.
		m_gate.Open();

		if(m_flushTail > m_flushBegin)
		{
			const UInt32 sendSize = (UInt32)(size_t)(m_flushTail - m_flushBegin);

			// first send stream header...
			StreamHeaderPacket streamheader;
			streamheader.threadID   = m_threadID;
			streamheader.streamSize = sendSize;
			okay = outStream.Send(&streamheader, sizeof(streamheader));

			// This send payload...
			okay = okay && outStream.Send(m_flushBegin, sendSize);
			m_flushTail = m_flushBegin;
		}

		OVR_CAPTURE_ASSERT(m_flushBegin == m_flushTail); // should be empty at this point...

		return okay;
	}

	AsyncStream::AsyncStream(void)
	{
		m_bufferLock  = 0;

	#if defined(OVR_CAPTURE_DARWIN)
		OVR_CAPTURE_STATIC_ASSERT(sizeof(mach_port_t) <= sizeof(UInt32));
		union
		{
			UInt32       i;
			mach_port_t  t;
		};
		i = 0;
		t = pthread_mach_thread_np(pthread_self());
		m_threadID = i;
	#elif defined(OVR_CAPTURE_LINUX) && defined(SYS_gettid)
		// Some Linux distros don't have gettid()... so use syscall() instead.
		// We don't static assert on sizeof(long) because PIDs are limited by
		// /proc/sys/kernel/pid_max, which is typically a fairly small number
		// that can safely fit in 32-bits.
		// TODO: read /proc/sys/kernel/pid_max and assert at runtime?
		m_threadID = static_cast<UInt32>(syscall(SYS_gettid));
	#elif defined(OVR_CAPTURE_POSIX)
		OVR_CAPTURE_STATIC_ASSERT(sizeof(pid_t) <= sizeof(UInt32));
		union
		{
			UInt32 i;
			pid_t  t;
		};
		i = 0;
		t = gettid();
		m_threadID = i;
	#elif defined(OVR_CAPTURE_WINDOWS)
		OVR_CAPTURE_STATIC_ASSERT(sizeof(DWORD) == sizeof(UInt32));
		m_threadID = static_cast<UInt32>(GetCurrentThreadId());
	#else
		#error UNKNOWN PLATFORM!
	#endif

		m_cacheBegin  = new UInt8[s_bufferSize];
		m_cacheTail   = m_cacheBegin;
		m_cacheEnd    = m_cacheBegin + s_bufferSize;

		m_flushBegin  = new UInt8[s_bufferSize];
		m_flushTail   = m_flushBegin;
		m_flushEnd    = m_flushBegin + s_bufferSize;

		// Make sure we are open by default... we don't close until we fill the buffer...
		m_gate.Open();

		// Try and acquire thread name...
		SendThreadName();
	}

	AsyncStream::~AsyncStream(void)
	{
		if(m_cacheBegin) delete [] m_cacheBegin;
		if(m_flushBegin) delete [] m_flushBegin;
	}

	void AsyncStream::SendThreadName(void)
	{
		char name[32] = {};
		GetThreadName(m_threadID, name, sizeof(name));
		if(name[0])
		{
			ThreadNamePacket packet = {};
			packet.threadID = m_threadID;
			WritePacket(packet, name, static_cast<ThreadNamePacket::PayloadSizeType>( StringLength(name) ));
		}
	}

} // namespace Capture
} // namespace OVR
