/************************************************************************************

PublicHeader:   OVR_Capture.h
Filename    :   OVR_Capture.cpp
Content     :   Oculus performance capture library
Created     :   January, 2015
Notes       : 
Author      :   James Dolan

Copyright   :   Copyright 2015 Oculus VR, LLC. All Rights reserved.

************************************************************************************/

#include <OVR_Capture.h>
#include "OVR_Capture_Local.h"
#include "OVR_Capture_Socket.h"
#include "OVR_Capture_AsyncStream.h"
#include "OVR_Capture_StandardSensors.h"
#include "OVR_Capture_PerfEvent.h"
#include "OVR_Capture_Packets.h"
#include "OVR_Capture_FileIO.h"
#include "OVR_Capture_Hash.h"

#if defined(OVR_CAPTURE_DARWIN)
	#include <mach/mach_time.h>
	#include <pthread.h>
#elif defined(OVR_CAPTURE_POSIX)
	#include <pthread.h>
	#include <malloc.h>
#endif

#if defined(OVR_CAPTURE_POSIX)
	#include <pthread.h>
#endif

#if defined(OVR_CAPTURE_HAS_MACH_ABSOLUTE_TIME)
	#include <mach/mach_time.h>
#endif

#if defined(OVR_CAPTURE_USE_CLOCK_GETTIME)
	#include <time.h>
#endif

#if defined(OVR_CAPTURE_HAS_GETTIMEOFDAY)
	#include <sys/time.h>
#endif

#include <memory.h>
#include <new>
#include <unordered_map>
#include <unordered_set>

#include <stdio.h>

namespace OVR
{
namespace Capture
{

	static const UInt16         g_zeroConfigPort  = ZeroConfigPacket::s_broadcastPort;
	static const UInt16         g_socketPortBegin = 3030;
	static const UInt16         g_socketPortEnd   = 3040;

	static Thread              *g_server          = nullptr;

	static UInt32               g_initFlags       = 0;
	static volatile UInt32      g_connectionFlags = 0;

	// reference count for the number of current functions depending on
	// an active connection. i.e., when we disconnect we wait for this
	// refcount to reach zero before cleaning up any data structures or
	// output streams.
	static volatile int         g_refcount        = 0;

	// Every time a new connection is made, this value gets incremented.
	// This allows us to detect when certain cached values need to be invalidated
	// between connections.
	static volatile UInt32      g_connectionVersion = 0;

	static OnConnectFunc        g_onConnect       = nullptr;
	static OnDisconnectFunc     g_onDisconnect    = nullptr;

	class SensorState
	{
		public:
			SensorState(void)
			{
			}

		public:
			float              minValue     = 0.0f;
			float              maxValue     = 0.0f;
			SensorInterpolator interpolator = Sensor_Interp_Linear;
			SensorUnits        units        = Sensor_Unit_None;
	};

	typedef std::unordered_map<UInt32,SensorState> SensorMap;

	static RWLock    g_sensorLock;
	static SensorMap g_sensors;


	template<typename T>
	class ParameterState
	{
		public:
			ParameterState(void)
			{
				currentValue = defaultValue = minValue = maxValue = static_cast<T>(0);
				useCurrent = false;
			}

		public:
			T    currentValue;
			T    defaultValue;
			T    minValue;
			T    maxValue;
			bool useCurrent;
	};

	typedef ParameterState<float> FloatParam;
	typedef std::unordered_map<UInt32,FloatParam> FloatParamMap;

	typedef ParameterState<int> IntParam;
	typedef std::unordered_map<UInt32,IntParam> IntParamMap;

	typedef ParameterState<bool> BoolParam;
	typedef std::unordered_map<UInt32,BoolParam> BoolParamMap;

	typedef bool ButtonParam;
	typedef std::unordered_map<UInt64,ButtonParam> ButtonParamMap;

	static RWLock         g_paramLock;
	static FloatParamMap  g_floatParams;
	static IntParamMap    g_intParams;
	static BoolParamMap   g_boolParams;
	static ButtonParamMap g_buttonParams;

	// Used for marking which dynamic label hashes have already been sent over the wire.
	static RWLock                     g_knownLabelLock;
	static std::unordered_set<UInt32> g_knownLabelHashes;
	

	/***********************************
	 * Helper Functions                 *
	 ***********************************/

	bool TryLockConnection(void)
	{
		// First check to see if we are even connected, if not early out without acquiring a lock...
		if(!IsConnected())
			return false;
		// increment ref count locking out the Server thread from deleting the AsyncStream.
		AtomicAdd(g_refcount, 1);
		// Once locked, check again to see if we are still connected... if not unlock and return false.
		if(!IsConnected())
		{
			AtomicSub(g_refcount, 1);
			return false;
		}
		// success! we must be connected and acquired a lock!
		return true;
	}

	bool TryLockConnection(const CaptureFlag feature)
	{
		// First check to see if we are even connected, if not early out without acquiring a lock...
		if(!CheckConnectionFlag(feature))
			return false;
		// increment ref count locking out the Server thread from deleting the AsyncStream.
		AtomicAdd(g_refcount, 1);
		// Once locked, check again to see if we are still connected... if not unlock and return false.
		if(!CheckConnectionFlag(feature))
		{
			AtomicSub(g_refcount, 1);
			return false;
		}
		// success! we must be connected and acquired a lock!
		return true;
	}

	void UnlockConnection(void)
	{
		// decrement refcount and do sanity check to make sure we don't go negative.
		const int i = AtomicSub(g_refcount, 1);
		OVR_CAPTURE_ASSERT(i >= 1);
		OVR_CAPTURE_UNUSED(i);
	}

	// Dynamically acquire a Label hash from a c-string... must be guarded by TryLockConnection/UnlockConnection!!!
	UInt32 ComputeLabelHash(const Label &label)
	{
		// Because ComputeLabelHash is guarded by a TryLockConnection block, we don't need to worry about
		// g_connectionVersion updating during this call, so we can load it once and assume its constant.
		const UInt32 connectionVersion = g_connectionVersion;

		// First, quickly check to see if the label was already hashed for this connection...
		if(label.m_version == connectionVersion)
		{
			// memory barrier to make sure hash is loaded after verifying connection.
			FullMemoryBarrier();
			return label.m_hash;
		}

		// If that test fails, then this is either the first time we have seen the cacheable label
		// or its a dynamically constructed label. Either way, lets find out if someone else has
		// hashed this string before. That way we only obtain a write lock and send the label packet
		// once per unique string.

		// Compute a hash for the string...
		const UInt32 hash = StringHash32(label.m_name);

		// check to see if this hash is already known
		g_knownLabelLock.ReadLock();
		const bool knownHash = (g_knownLabelHashes.find(hash) != g_knownLabelHashes.end());
		g_knownLabelLock.ReadUnlock();

		// if this is a new label, send the key/value pair and mark it as known...
		if(knownHash == false)
		{
			// mark this hash as being sent...
			g_knownLabelLock.WriteLock();
			g_knownLabelHashes.insert(hash);
			g_knownLabelLock.WriteUnlock();

			// send the new label packet... its okay if this gets sent multiple times when there is a race condition
			// probably better than holding the write lock longer. OVRMonitor will simply overwrite the same entry
			// a few times. But that would be incredibly rare, only of multiple threads hit the same label for the
			// first time at the same time. So its really just not a scenario we care about optimizing.
			LabelPacket packet;
			packet.labelID = hash;
			AsyncStream::Acquire()->WritePacket( packet, label.m_name, static_cast<LabelPacket::PayloadSizeType>( StringLength(label.m_name) ) );
		}

		// Update the label's cached values! Make sure the hash updates before the version. That way we can assure that
		// the hash is up to date when we check the version.
		label.m_hash = hash;
		FullMemoryBarrier();
		label.m_version = connectionVersion;

		return hash;
	}

	static bool InitInternal(UInt32 flags, OnConnectFunc onConnect, OnDisconnectFunc onDisconnect)
	{
		// If we are already initialized... don't re-initialize.
		if(g_initFlags)
			return false;

		// sanitize input flags;
		flags = flags & All_Flags;

		// If no capture features are enabled... then don't initialize anything!
		if(!flags)
			return false;

		g_initFlags       = flags;
		g_connectionFlags = 0;

		g_onConnect    = onConnect;
		g_onDisconnect = onDisconnect;

		OVR_CAPTURE_ASSERT(!g_server);

		return true;
	}

	static void ClearParameters(void)
	{
		g_sensorLock.WriteLock();
		g_sensors.clear();
		g_sensorLock.WriteUnlock();

		g_paramLock.WriteLock();
		g_floatParams.clear();
		g_intParams.clear();
		g_boolParams.clear();
		g_buttonParams.clear();
		g_paramLock.WriteUnlock();

		g_knownLabelLock.WriteLock();
		g_knownLabelHashes.clear();
		g_knownLabelLock.WriteUnlock();
	}

	template<typename PacketType, bool hasPayload> struct PayloadSizer;
	template<typename PacketType> struct PayloadSizer<PacketType, true>
	{
		static UInt32 GetSizeOfPayloadSizeType(void)
		{
			return sizeof(typename PacketType::PayloadSizeType);
		}
	};
	template<typename PacketType> struct PayloadSizer<PacketType, false>
	{
		static UInt32 GetSizeOfPayloadSizeType(void)
		{
			return 0;
		}
	};

	template<typename PacketType> static PacketDescriptorPacket BuildPacketDescriptorPacket(void)
	{
		PacketDescriptorPacket desc = {0};
		desc.packetID              = PacketType::s_packetID;
		desc.version               = PacketType::s_version;
		desc.sizeofPacket          = sizeof(PacketType);
		desc.sizeofPayloadSizeType = PayloadSizer<PacketType, PacketType::s_hasPayload>::GetSizeOfPayloadSizeType();
		return desc;
	}

	static const PacketDescriptorPacket g_packetDescs[] =
	{
		BuildPacketDescriptorPacket<ThreadNamePacket>(),
		BuildPacketDescriptorPacket<LabelPacket>(),
		BuildPacketDescriptorPacket<FrameIndexPacket>(),
		BuildPacketDescriptorPacket<VSyncPacket>(),
		BuildPacketDescriptorPacket<CPUZoneEnterPacket>(),
		BuildPacketDescriptorPacket<CPUZoneLeavePacket>(),
		BuildPacketDescriptorPacket<GPUZoneEnterPacket>(),
		BuildPacketDescriptorPacket<GPUZoneLeavePacket>(),
		BuildPacketDescriptorPacket<GPUClockSyncPacket>(),
		BuildPacketDescriptorPacket<SensorRangePacket>(),
		BuildPacketDescriptorPacket<SensorPacket>(),
		BuildPacketDescriptorPacket<FrameBufferPacket>(),
		BuildPacketDescriptorPacket<LogPacket>(),
		BuildPacketDescriptorPacket<FloatParamRangePacket>(),
		BuildPacketDescriptorPacket<FloatParamPacket>(),
		BuildPacketDescriptorPacket<IntParamRangePacket>(),
		BuildPacketDescriptorPacket<IntParamPacket>(),
		BuildPacketDescriptorPacket<BoolParamPacket>(),
		BuildPacketDescriptorPacket<MemoryAllocPacket>(),
		BuildPacketDescriptorPacket<MemoryReallocPacket>(),
		BuildPacketDescriptorPacket<MemoryFreePacket>(),
		BuildPacketDescriptorPacket<HeadTransformPacket>(),
		BuildPacketDescriptorPacket<CPUContextSwitch>(),
		BuildPacketDescriptorPacket<ButtonParamPacket>(),
	};
	static const UInt32 g_numPacketDescs = sizeof(g_packetDescs) / sizeof(g_packetDescs[0]);

	/***********************************
	 * Servers                         *
	 ***********************************/

	// Thread/Socket that sits in the background waiting for incoming connections...
	// Not to be confused with the ZeroConfig hosts who advertises our existence, this
	// is the socket that actually accepts the incoming connections and creates the socket.
	class RemoteServer : public Thread
	{
		public:
			static RemoteServer *Create(void)
			{
			#if defined(OVR_CAPTURE_WINDOWS)
				// Make sure winsock is initialized...
				WSADATA wsdata = {0};
				if(WSAStartup(MAKEWORD(2,2), &wsdata) != 0)
					return nullptr;
			#endif

				// Find the first open port...
				for(UInt16 port=g_socketPortBegin; port<g_socketPortEnd; port++)
				{
					SocketAddress listenAddr = SocketAddress::Any(port);
					Socket *listenSocket     = Socket::Create(Socket::Type_Stream);
					if(listenSocket && !listenSocket->Bind(listenAddr))
					{
						listenSocket->Release();
						listenSocket = nullptr;
					}
					if(listenSocket && !listenSocket->Listen(1))
					{
						listenSocket->Release();
						listenSocket = nullptr;
					}
					if(listenSocket)
					{
						return new RemoteServer(listenSocket, port);
					}
				}
				return nullptr;
			}

			virtual ~RemoteServer(void)
			{
				// Signal and wait for quit...
				if(m_listenSocket) m_listenSocket->Shutdown();
				QuitAndWait();

				// Cleanup just in case the thread doesn't...
				if(m_streamSocket) m_streamSocket->Release();
				if(m_listenSocket) m_listenSocket->Release();

			#if defined(OVR_CAPTURE_WINDOWS)
				// WinSock reference counts...
				WSACleanup();
			#endif
			}

		private:
			RemoteServer(Socket *listenSocket, UInt16 listenPort)
			{
				m_streamSocket = nullptr;
				m_listenSocket = listenSocket;
				m_listenPort   = listenPort;
			}

			virtual void OnThreadExecute(void)
			{
				SetThreadName("CaptureServer");

				// Acquire the process name...
			#if defined(OVR_CAPTURE_WINDOWS)
				char packageName[64] = {0};
				GetModuleFileNameA(nullptr, packageName, sizeof(packageName));
				if(!packageName[0])
				{
					StringCopy(packageName, "Unknown", sizeof(packageName));
				}
			#elif defined(OVR_CAPTURE_ANDROID) || defined(OVR_CAPTURE_LINUX)
				char packageName[64] = {0};
				char cmdlinepath[64] = {0};
				FormatString(cmdlinepath, sizeof(cmdlinepath), "/proc/self/cmdline");
				if(ReadFileLine(cmdlinepath, packageName, sizeof(packageName)) <= 0)
				{
					StringCopy(packageName, "Unknown", sizeof(packageName));
				}
			#else
				char packageName[64] = {0};
				StringCopy(packageName, "Unknown", sizeof(packageName));
			#endif

				while(m_listenSocket && !QuitSignaled())
				{
					// Start auto-discovery thread...
					ZeroConfigHost *zeroconfig = ZeroConfigHost::Create(g_zeroConfigPort, m_listenPort, packageName);

					// If zeroconfig fails to create then we really can't do anything and should just abort...
					if(zeroconfig == nullptr)
						break;

					// start zero config thread...
					zeroconfig->Start();

					// try and accept a new socket connection...
					SocketAddress streamAddr;
					m_streamSocket = m_listenSocket->Accept(streamAddr);

					// Once connected, shut the auto-discovery thread down.
					zeroconfig->Release();

					// If no connection was established, something went totally wrong and we should just abort...
					if(!m_streamSocket)
						break;

					// Before we start sending capture data... first must exchange connection headers...
					// First attempt to read in the request header from the Client...
					ConnectionHeaderPacket clientHeader = {0};
					if(!m_streamSocket->Receive(&clientHeader, sizeof(clientHeader)))
					{
						m_streamSocket->Release();
						m_streamSocket = nullptr;
						continue;
					}

					// Sanity check the incoming header size...
					if(clientHeader.size != sizeof(clientHeader))
					{
						m_streamSocket->Release();
						m_streamSocket = nullptr;
						continue;
					}

					// Sanity check the incoming header version...
					if(clientHeader.version != ConnectionHeaderPacket::s_version)
					{
						m_streamSocket->Release();
						m_streamSocket = nullptr;
						continue;
					}

					// Load our connection flags...
					const UInt32 connectionFlags = clientHeader.flags & g_initFlags;

					// Build and send return header... We *always* send the return header so that if we don't
					// like something (like version number or feature flags), the client has some hint as to
					// what we didn't like.
					ConnectionHeaderPacket serverHeader = {0};
					serverHeader.size    = sizeof(serverHeader);
					serverHeader.version = ConnectionHeaderPacket::s_version;
					serverHeader.flags   = connectionFlags;
					if(!m_streamSocket->Send(&serverHeader, sizeof(serverHeader)))
					{
						m_streamSocket->Release();
						m_streamSocket = nullptr;
						continue;
					}

					// Check version number...
					if(clientHeader.version != serverHeader.version)
					{
						m_streamSocket->Release();
						m_streamSocket = nullptr;
						continue;
					}

					// Check that we have any capture features even turned on...
					if(!connectionFlags)
					{
						m_streamSocket->Release();
						m_streamSocket = nullptr;
						continue;
					}

					// Finally, send our packet descriptors...
					const PacketDescriptorHeaderPacket packetDescHeader = { g_numPacketDescs };
					if(!m_streamSocket->Send(&packetDescHeader, sizeof(packetDescHeader)))
					{
						m_streamSocket->Release();
						m_streamSocket = nullptr;
						continue;
					}
					if(!m_streamSocket->Send(&g_packetDescs, sizeof(g_packetDescs)))
					{
						m_streamSocket->Release();
						m_streamSocket = nullptr;
						continue;
					}

					// Connection established!

					// Initialize the per-thread stream system before flipping on g_connectionFlags...
					AsyncStream::Init();

					// Make sure the CaptureServer is the first thread with an AsyncStream.
					// this helps make sure that the initial Label packets get through first.
					AsyncStream::Acquire();

				#if defined(OVR_CAPTURE_HAS_GLES3)
					OnConnectGLES3();
				#endif

					if(g_onConnect)
					{
						// Call back into the app to notify a connection is being established.
						// We intentionally do this before enabling the connection flags.
						g_onConnect(connectionFlags);
					}

					// Update connection version... must happen before setting g_connectionFlags!
					g_connectionVersion++;
					FullMemoryBarrier();

					// Signal that we are connected!
					AtomicExchange(g_connectionFlags, connectionFlags);

					// Start CPU/GPU/Thermal sensors...
					StandardSensors stdsensors;
					if(CheckConnectionFlag(Enable_CPU_Clocks) || CheckConnectionFlag(Enable_GPU_Clocks) || CheckConnectionFlag(Enable_Thermal_Sensors))
					{
						stdsensors.Start();
					}

				#if defined(OVR_CAPTURE_HAS_PERF)
					// Start our perf kernel hooks for CPU scheduling...
					PerfEvent perfEvent;
					if(CheckConnectionFlag(Enable_CPU_Scheduler))
					{
						perfEvent.AttachAllTasks();
						perfEvent.Start();
					}
				#endif

					// Spin as long as we are connected flushing data from our data stream...
					while(!QuitSignaled())
					{
						const UInt64 flushBeginTime = GetNanoseconds();
						const UInt32 waitflags = m_streamSocket->WaitFor(Socket::WaitFlag_Read | Socket::WaitFlag_Write | Socket::WaitFlag_Timeout, 2);
						if(waitflags & Socket::WaitFlag_Timeout)
						{
							// Connection likely failed somehow...
							break;
						}
						if(waitflags & Socket::WaitFlag_Read)
						{
							PacketHeader header;
							m_streamSocket->Receive((char*)&header, sizeof(header));
							if (header.packetID == FloatParamPacket::s_packetID)
							{
								FloatParamPacket packet;
								m_streamSocket->Receive((char*)&packet, sizeof(packet));
								g_paramLock.WriteLock();
								FloatParamMap::iterator found = g_floatParams.find(packet.labelID);
								if(found != g_floatParams.end())
								{
									(*found).second.currentValue = packet.value;
									(*found).second.useCurrent   = true;
								}
								g_paramLock.WriteUnlock();
							}
							else if (header.packetID == IntParamPacket::s_packetID)
							{
								IntParamPacket packet;
								m_streamSocket->Receive((char*)&packet, sizeof(packet));
								g_paramLock.WriteLock();
								IntParamMap::iterator found = g_intParams.find(packet.labelID);
								if(found != g_intParams.end())
								{
									(*found).second.currentValue = packet.value;
									(*found).second.useCurrent   = true;
								}
								g_paramLock.WriteUnlock();
							}
							else if (header.packetID == BoolParamPacket::s_packetID)
							{
								BoolParamPacket packet;
								m_streamSocket->Receive((char*)&packet, sizeof(packet));
								g_paramLock.WriteLock();
								BoolParamMap::iterator found = g_boolParams.find(packet.labelID);
								if(found != g_boolParams.end())
								{
									(*found).second.currentValue = packet.value ? true : false;
									(*found).second.useCurrent   = true;
								}
								g_paramLock.WriteUnlock();
							}
							else if (header.packetID == ButtonParamPacket::s_packetID)
							{
								ButtonParamPacket packet;
								m_streamSocket->Receive((char*)&packet, sizeof(packet));
								g_paramLock.WriteLock();
								ButtonParamMap::iterator found = g_buttonParams.find(packet.labelID);
								if(found != g_buttonParams.end())
								{
									found->second = true;
								}
								g_paramLock.WriteUnlock();
							}
							else
							{
								Logf(Log_Warning, "OVR::Capture::RemoteServer; Received Invalid Capture Packet");
							}
						}
						if(waitflags & Socket::WaitFlag_Write)
						{
							// Socket is ready to write data... so now is a good time to flush pending capture data.
							SocketOutStream outStream(*m_streamSocket);
							if(!AsyncStream::FlushAll(outStream))
							{
								// Error occurred... shutdown the connection.
								break;
							}
						}
						const UInt64 flushEndTime   = GetNanoseconds();
						const UInt64 flushDeltaTime = flushEndTime - flushBeginTime;
						const UInt64 sleepTime      = 4000000; // 4ms
						if(flushDeltaTime < sleepTime)
						{
							// Sleep just a bit to keep the thread from killing a core and to let a good chunk of data build up
							ThreadSleepNanoseconds((UInt32)(sleepTime - flushDeltaTime));
						}
					}

					// Clear the connection flags...
					AtomicExchange(g_connectionFlags, (UInt32)0);

					// Close down our sensor thread...
				#if defined(OVR_CAPTURE_HAS_PERF)
					perfEvent.QuitAndWait();
				#endif
					stdsensors.QuitAndWait();

					// Connection was closed at some point, lets clean up our socket...
					m_streamSocket->Shutdown();
					m_streamSocket->Release();
					m_streamSocket = nullptr;

					if(g_onDisconnect)
					{
						// After the connection is fully shut down, notify the app.
						g_onDisconnect();
					}

				
				#if defined(OVR_CAPTURE_HAS_GLES3)
					OnDisconnectGLES3();
				#endif

					// Clear the buffers for all AsyncStreams to guarantee that no event is 
					// stalled waiting for room on a buffer. Then we wait until there there
					// are no events still writing out.
					AsyncStream::ClearAll();
					while(AtomicGet(g_refcount) > 0)
					{
						ThreadSleepMilliseconds(1);
					}

					// Finally, release any AsyncStreams that were created during this session
					// now that we can safely assume there are no events actively trying to
					// write out to a stream.
					AsyncStream::Shutdown();

					// Clear remote parameter store...
					ClearParameters();
				} // while(m_listenSocket && !QuitSignaled())
			}

		private:
			Socket *m_streamSocket;
			Socket *m_listenSocket;
			UInt16  m_listenPort;
	};

	// Connection "server" that stores capture stream straight to disk
	class LocalServer : public Thread
	{
		public:
			static LocalServer *Create(const char *outPath)
			{
				// Load our connection flags...
				const UInt32 connectionFlags = g_initFlags;

				// Check that we have any capture features even turned on...
				if(!connectionFlags)
				{
					return nullptr;
				}

				// Attempt to open our destination file...
				FileHandle file = OpenFile(outPath, true);
				if(file == NullFileHandle)
				{
					return nullptr;
				}
				FileOutStream outStream(file);

				// Build and send return header...
				ConnectionHeaderPacket serverHeader = {0};
				serverHeader.size    = sizeof(serverHeader);
				serverHeader.version = ConnectionHeaderPacket::s_version;
				serverHeader.flags   = connectionFlags;
				if(!outStream.Send(&serverHeader, sizeof(serverHeader)))
				{
					CloseFile(file);
					return nullptr;
				}

				// Finally, send our packet descriptors...
				const PacketDescriptorHeaderPacket packetDescHeader = { g_numPacketDescs };
				if(!outStream.Send(&packetDescHeader, sizeof(packetDescHeader)))
				{
					CloseFile(file);
					return nullptr;
				}
				if(!outStream.Send(&g_packetDescs, sizeof(g_packetDescs)))
				{
					CloseFile(file);
					return nullptr;
				}

				// "Connection" established!

				return new LocalServer(file, connectionFlags);
			}

			virtual ~LocalServer(void)
			{
				QuitAndWait();

				if(m_file != NullFileHandle)
				{
					CloseFile(m_file);
					m_file = NullFileHandle;
				}
			}

		private:
			LocalServer(FileHandle file, UInt32 connectionFlags) :
				m_file(file),
				m_connectionFlags(connectionFlags)
			{
			}

			virtual void OnThreadExecute(void)
			{
				SetThreadName("CaptureServer");

				// Initialize the per-thread stream system before flipping on g_connectionFlags...
				AsyncStream::Init();

				// Make sure the CaptureServer is the first thread with an AsyncStream.
				// this helps make sure that the initial Label packets get through first.
				AsyncStream::Acquire();

			#if defined(OVR_CAPTURE_HAS_GLES3)
				OnConnectGLES3();
			#endif

				if(g_onConnect)
				{
					// Call back into the app to notify a connection is being established.
					// We intentionally do this before enabling the connection flags.
					g_onConnect(m_connectionFlags);
				}

				// Update connection version... must happen before setting g_connectionFlags!
				g_connectionVersion++;
				FullMemoryBarrier();

				// Signal that we are connected!
				AtomicExchange(g_connectionFlags, m_connectionFlags);

				// Start CPU/GPU/Thermal sensors...
				StandardSensors stdsensors;
				if(CheckConnectionFlag(Enable_CPU_Clocks) || CheckConnectionFlag(Enable_GPU_Clocks) || CheckConnectionFlag(Enable_Thermal_Sensors))
				{
					stdsensors.Start();
				}

			#if defined(OVR_CAPTURE_HAS_PERF)
				// Start our perf kernel hooks for CPU scheduling...
				PerfEvent perfEvent;
				if(CheckConnectionFlag(Enable_CPU_Scheduler))
				{
					perfEvent.AttachAllTasks();
					perfEvent.Start();
				}
			#endif

				FileOutStream outStream(m_file);

				// as long as we are running, continuously flush the latest stream data to disk...
				while(!QuitSignaled())
				{
					const UInt64 flushBeginTime = GetNanoseconds();
					if(!AsyncStream::FlushAll(outStream))
					{
						break;
					}
					const UInt64 flushEndTime   = GetNanoseconds();
					const UInt64 flushDeltaTime = flushEndTime - flushBeginTime;
					const UInt64 sleepTime      = 4000000; // 4ms
					if(flushDeltaTime < sleepTime)
					{
						// Sleep just a bit to keep the thread from killing a core and to let a good chunk of data build up
						ThreadSleepNanoseconds((UInt32)(sleepTime - flushDeltaTime));
					}
				}

				// Clear the connection flags...
				AtomicExchange(g_connectionFlags, (UInt32)0);

				if(g_onDisconnect)
				{
					// After the connection is fully shut down, notify the app.
					g_onDisconnect();
				}

			#if defined(OVR_CAPTURE_HAS_GLES3)
				OnDisconnectGLES3();
			#endif

				// Clear the buffers for all AsyncStreams to guarantee that no event is 
				// stalled waiting for room on a buffer. Then we wait until there there
				// are no events still writing out.
				AsyncStream::ClearAll();
				while(AtomicGet(g_refcount) > 0)
				{
					ThreadSleepMilliseconds(1);
				}

				// Close down our sensor thread...
			#if defined(OVR_CAPTURE_HAS_PERF)
				perfEvent.QuitAndWait();
			#endif
				stdsensors.QuitAndWait();

				// Finally, release any AsyncStreams that were created during this session
				// now that we can safely assume there are no events actively trying to
				// write out to a stream.
				AsyncStream::Shutdown();

				// Clear remote parameter store...
				ClearParameters();
			}

		private:
			FileHandle m_file;
			UInt32     m_connectionFlags;
	};

	/***********************************
	 * Public API                       *
	 ***********************************/


	// Get current time in microseconds...
	UInt64 GetNanoseconds(void)
	{
	#if defined(OVR_CAPTURE_HAS_MACH_ABSOLUTE_TIME)
		// OSX/iOS doesn't have clock_gettime()... but it does have gettimeofday(), or even better mach_absolute_time()
		// which is about 50% faster than gettimeofday() and higher precision!
		// Only 24.5ns per GetNanoseconds() call! But we can do better...
		// It seems that modern Darwin already returns nanoseconds, so numer==denom
		// when we test that assumption it brings us down to 16ns per GetNanoseconds() call!!!
		// Timed on MacBookPro running OSX.
		static mach_timebase_info_data_t info = {0};
		if(!info.denom)
			mach_timebase_info(&info);
		const UInt64 t = mach_absolute_time();
		if(info.numer==info.denom)
			return t;
		return (t * info.numer) / info.denom;

	#elif defined(OVR_CAPTURE_HAS_CLOCK_GETTIME)
		// 23ns per call on i7 Desktop running Ubuntu 64
		// >800ns per call on Galaxy Note 4 running Android 4.3!!!
		struct timespec tp;
		clock_gettime(CLOCK_MONOTONIC, &tp);
		return ((UInt64)tp.tv_sec)*1000000000 + (UInt64)tp.tv_nsec;

	#elif defined(OVR_CAPTURE_HAS_GETTIMEOFDAY)
		// Just here for reference... this timer is only microsecond level of precision, and >2x slower than the mach timer...
		// And on non-mach platforms clock_gettime() is the preferred method...
		// 34ns per call on MacBookPro running OSX...
		// 23ns per call on i7 Desktop running Ubuntu 64
		// >800ns per call on Galaxy Note 4 running Android 4.3!!!
		struct timeval tv;
		gettimeofday(&tv, 0);
		const UInt64 us = ((UInt64)tv.tv_sec)*1000000 + (UInt64)tv.tv_usec;
		return us*1000;

	#elif defined(OVR_CAPTURE_WINDOWS)
		static double tonano = 0.0;
		if(!tonano)
		{
			LARGE_INTEGER f;
			QueryPerformanceFrequency(&f);
			tonano = 1000000000.0 / f.QuadPart;
		}
		LARGE_INTEGER c;
		QueryPerformanceCounter(&c);
		return (UInt64)(c.QuadPart * tonano);

	#else
		#error Unknown Platform!

	#endif
	}

	// Initializes the Capture system remote server.
	// should be called before any other Capture call.
	bool InitForRemoteCapture(UInt32 flags, OnConnectFunc onConnect, OnDisconnectFunc onDisconnect)
	{
		if(!InitInternal(flags, onConnect, onDisconnect))
			return false;

		g_server = RemoteServer::Create();
		if(g_server)
			g_server->Start();

		return true;
	}

	// Initializes the Capture system to store capture stream to disk, starting immediately.
	// should be called before any other Capture call.
	bool InitForLocalCapture(const char *outPath, UInt32 flags, OnConnectFunc onConnect, OnDisconnectFunc onDisconnect)
	{
		if(!InitInternal(flags, onConnect, onDisconnect))
			return false;

		g_server = LocalServer::Create(outPath);
		if(g_server)
			g_server->Start();

		return true;
	}

	// Closes the capture system... no other Capture calls on *any* thread should be called after this.
	void Shutdown(void)
	{
		if(g_server)
		{
			delete g_server;
			g_server = nullptr;
		}
		OVR_CAPTURE_ASSERT(!g_connectionFlags);
		g_initFlags       = 0;
		g_connectionFlags = 0;
		g_onConnect       = nullptr;
		g_onDisconnect    = nullptr;
	}

	// Indicates that the capture system is currently connected...
	bool IsConnected(void)
	{
		return AtomicGet(g_connectionFlags) != 0;
	}

	// Check to see if (a) a connection is established and (b) that a particular capture feature is enabled on the connection.
	bool CheckConnectionFlag(const CaptureFlag feature)
	{
		return (AtomicGet(g_connectionFlags) & feature) != 0;
	}

	// Mark the currently referenced frame index on this thread...
	// You may call this from any thread that generates frame data to help track latency and missed frames.
	void FrameIndex(const UInt64 frameIndex)
	{
		if(TryLockConnection())
		{
			FrameIndexPacket packet;
			packet.timestamp  = GetNanoseconds();
			packet.frameIndex = frameIndex;
			AsyncStream::Acquire()->WritePacket(packet);
			UnlockConnection();
		}
	}

	// Mark the start of vsync... this value should be comparable to the same reference point as GetNanoseconds()
	void VSyncTimestamp(UInt64 nanoseconds)
	{
		if(TryLockConnection())
		{
			VSyncPacket packet;
			packet.timestamp = nanoseconds;
			AsyncStream::Acquire()->WritePacket(packet);
			UnlockConnection();
		}
	}

	// Upload the framebuffer for the current frame... should be called once a frame!
	void FrameBuffer(UInt64 timestamp, FrameBufferFormat format, UInt32 width, UInt32 height, const void *buffer)
	{
		if(TryLockConnection(Enable_FrameBuffer_Capture))
		{
			UInt32 pixelSize = 0;
			switch(format)
			{
				case FrameBuffer_RGB_565:   pixelSize=16; break;
				case FrameBuffer_RGBA_8888: pixelSize=32; break;
				case FrameBuffer_DXT1:      pixelSize=4;
					if(width&3 || height&3)
					{
						Logf(Log_Warning, "OVR::Capture::FrameBuffer(): requires DXT1 texture dimensions to be multiples of 4");
						return;
					}
					break;
			}
			if(!pixelSize)
			{
				Logf(Log_Warning, "OVR::Capture::FrameBuffer(): Format (%u) not supported!", (UInt32)format);
				return;
			}
			const UInt32 payloadSize = (pixelSize * width * height) >> 3; // pixelSize is in bits, divide by 8 to give us payload byte size
			FrameBufferPacket packet;
			packet.format     = format;
			packet.width      = width;
			packet.height     = height;
			packet.timestamp  = timestamp;
			// TODO: we should probably just send framebuffer packets directly over the network rather than
			//       caching them due to their size and to reduce latency.
			AsyncStream::Acquire()->WritePacket(packet, buffer, payloadSize);
			UnlockConnection();
		}
	}

	// Misc application message logging...
	void Logf(LogPriority priority, const char *format, ...)
	{
		if(TryLockConnection(Enable_Logging))
		{
			va_list args;
			va_start(args, format);
			Logv(priority, format, args);
			va_end(args);
			UnlockConnection();
		}
	}

	void Logv(LogPriority priority, const char *format, va_list args)
	{
		if(TryLockConnection(Enable_Logging))
		{
			const size_t bufferMaxSize = 512;
			char buffer[bufferMaxSize];
			const int bufferSize = FormatStringV(buffer, bufferMaxSize, format, args);
			if(bufferSize > 0)
			{
				Log(priority, buffer);
			}
			UnlockConnection();
		}
	}

	void Log(LogPriority priority, const char *str)
	{
		if(str && str[0] && TryLockConnection(Enable_Logging))
		{
			LogPacket packet;
			packet.timestamp = GetNanoseconds();
			packet.priority  = priority;
			AsyncStream::Acquire()->WritePacket(packet, str, static_cast<LogPacket::PayloadSizeType>( StringLength(str)) );
			UnlockConnection();
		}
	}

	// Mark a CPU profiled region.... Begin(); DoSomething(); End();
	// Nesting is allowed. And every Begin() should ALWAYS have a matching End()!!!
	void EnterCPUZone(const Label &label)
	{
		if(TryLockConnection(Enable_CPU_Zones))
		{
			CPUZoneEnterPacket packet;
			packet.labelID   = ComputeLabelHash(label);
			packet.timestamp = GetNanoseconds();
			AsyncStream::Acquire()->WritePacket(packet);
			UnlockConnection();
		}
	}

	void LeaveCPUZone(void)
	{
		if(TryLockConnection(Enable_CPU_Zones))
		{
			CPUZoneLeavePacket packet;
			packet.timestamp = GetNanoseconds();
			AsyncStream::Acquire()->WritePacket(packet);
			UnlockConnection();
		}
	}

	// Report memory allocation events...
	void MemoryAlloc(size_t size, const void *ptr)
	{
		if(TryLockConnection(Enable_Memory))
		{
			MemoryAllocPacket packet;
			packet.timestamp = GetNanoseconds();
			packet.size      = static_cast<UInt32>(size);
			packet.ptr       = reinterpret_cast<uintptr_t>(ptr);
			AsyncStream::Acquire()->WritePacket(packet);
			UnlockConnection();
		}
	}

	// Report memory allocation events...
	void MemoryRealloc(size_t size, const void *oldPtr, const void *newPtr)
	{
		if(TryLockConnection(Enable_Memory))
		{
			MemoryReallocPacket packet;
			packet.timestamp = GetNanoseconds();
			packet.size      = static_cast<UInt32>(size);
			packet.oldPtr    = reinterpret_cast<uintptr_t>(oldPtr);
			packet.newPtr    = reinterpret_cast<uintptr_t>(newPtr);
			AsyncStream::Acquire()->WritePacket(packet);
			UnlockConnection();
		}
	}

	// Report memory allocation events...
	void MemoryFree(const void *ptr)
	{
		if(TryLockConnection(Enable_Memory))
		{
			MemoryFreePacket packet;
			packet.timestamp = GetNanoseconds();
			packet.ptr       = reinterpret_cast<uintptr_t>(ptr);
			AsyncStream::Acquire()->WritePacket(packet);
			UnlockConnection();
		}
	}

	// Set the absolute value of a sensor, may be called at any frequency.
	// minValue/maxValue/interpolator/units should remain constant!
	void Sensor(const Label &label, float value, float minValue, float maxValue, SensorInterpolator interpolator, SensorUnits units)
	{
		if(TryLockConnection())
		{
			const UInt32 labelid   = ComputeLabelHash(label);
			const UInt64 timestamp = GetNanoseconds();

			bool dirtySensor = false;

			// First check to see if the SensorMap is out of date...
			g_sensorLock.ReadLock();
			SensorMap::iterator found = g_sensors.find(labelid);
			if(found != g_sensors.end())
			{
				const SensorState &sensorState = found->second;
				if(sensorState.minValue != minValue || sensorState.maxValue != maxValue)
				{
					dirtySensor = true;
				}
			}
			else
			{
				dirtySensor = true;
			}
			g_sensorLock.ReadUnlock();

			// If the SensorMap is out of sync, we need to add it to update the map and notify the remote client...
			if(dirtySensor)
			{
				// Update Sensor map...
				g_sensorLock.WriteLock();
				SensorState &sensorState = g_sensors[labelid];
				sensorState.minValue     = minValue;
				sensorState.maxValue     = maxValue;
				sensorState.interpolator = interpolator;
				sensorState.units        = units;
				g_sensorLock.WriteUnlock();

				// Update remote client...
				SensorRangePacket packet;
				packet.labelID      = labelid;
				packet.interpolator = interpolator;
				packet.units        = units;
				packet.minValue     = minValue;
				packet.maxValue     = maxValue;
				AsyncStream::Acquire()->WritePacket(packet);
			}

			// Finally, send the current sensor value to the remote client...
			// TODO: if interpolator==NEAREST, should we only send this packet when the value changes?
			SensorPacket packet;
			packet.labelID   = labelid;
			packet.timestamp = timestamp;
			packet.value     = value;
			AsyncStream::Acquire()->WritePacket(packet);

			UnlockConnection();
		}
	}

	// Set the current head pose
	void HeadPose(const Quaternion &orientation, const Vector3 &position)
	{
		if(TryLockConnection(Enable_Head_Tracking))
		{
			HeadTransformPacket packet;
			packet.timestamp   = GetNanoseconds();
			packet.orientation = orientation;
			packet.position    = position;
			AsyncStream::Acquire()->WritePacket(packet);
			UnlockConnection();
		}
	}

	// Get a user controllable variable... remotely tweaked in real-time via OVRMonitor.
	float GetVariable(const Label &label, float defaultValue, float minValue, float maxValue)
	{
		// If not connected, just pass through the default value...
		float returnValue = defaultValue;

		if(TryLockConnection())
		{
			const UInt32 labelid = ComputeLabelHash(label);

			// Check to see if this parameter already exists, if so just return the current value...
			g_paramLock.ReadLock();
			FloatParamMap::iterator found = g_floatParams.find(labelid);
			if(found != g_floatParams.end())
			{
				const FloatParam oldParam = (*found).second;
				// update return value from cached parameter...
				if(oldParam.useCurrent)
				{
					returnValue = oldParam.currentValue;
				}
				// if the parameter range hasn't changed, just return the current value... else we need to update the param.
				if(oldParam.defaultValue == defaultValue && oldParam.minValue == minValue && oldParam.maxValue == maxValue)
				{
					g_paramLock.ReadUnlock();
					UnlockConnection();
					return returnValue;
				}
			}
			g_paramLock.ReadUnlock();

			// New parameter... add it to the param map...
			g_paramLock.WriteLock();
			FloatParam &newParam  = g_floatParams[labelid];
			newParam.defaultValue = defaultValue;
			newParam.minValue     = minValue;
			newParam.maxValue     = maxValue;
			g_paramLock.WriteUnlock();

			// Report new parameter to remote monitor...
			FloatParamRangePacket packet;
			packet.labelID = labelid; 
			packet.value   = defaultValue;
			packet.valMin  = minValue;
			packet.valMax  = maxValue;
			AsyncStream::Acquire()->WritePacket(packet);

			UnlockConnection();
		}

		// This is a new variable, so just return the default...
		return returnValue;
	}

	// Get a user controllable variable... remotely tweaked in real-time via OVRMonitor.
	int GetVariable(const Label &label, int defaultValue, int minValue, int maxValue)
	{
		// If not connected, just pass through the default value...
		int returnValue = defaultValue;

		if(TryLockConnection())
		{
			const UInt32 labelid = ComputeLabelHash(label);

			// Check to see if this parameter already exists, if so just return the current value...
			g_paramLock.ReadLock();
			IntParamMap::iterator found = g_intParams.find(labelid);
			if(found != g_intParams.end())
			{
				const IntParam oldParam = (*found).second;
				// update return value from cached parameter...
				if(oldParam.useCurrent)
				{
					returnValue = oldParam.currentValue;
				}
				// if the parameter range hasn't changed, just return the current value... else we need to update the param.
				if(oldParam.defaultValue == defaultValue && oldParam.minValue == minValue && oldParam.maxValue == maxValue)
				{
					g_paramLock.ReadUnlock();
					UnlockConnection();
					return returnValue;
				}
			}
			g_paramLock.ReadUnlock();

			// New parameter... add it to the param map...
			g_paramLock.WriteLock();
			IntParam &newParam    = g_intParams[labelid];
			newParam.defaultValue = defaultValue;
			newParam.minValue     = minValue;
			newParam.maxValue     = maxValue;
			g_paramLock.WriteUnlock();

			// Report new parameter to remote monitor...
			IntParamRangePacket packet;
			packet.labelID = labelid; 
			packet.value   = defaultValue;
			packet.valMin  = minValue;
			packet.valMax  = maxValue;
			AsyncStream::Acquire()->WritePacket(packet);

			UnlockConnection();
		}

		// This is a new variable, so just return the default...
		return returnValue;
	}

	// Get a user controllable variable... remotely tweaked in real-time via OVRMonitor.
	bool GetVariable(const Label &label, bool defaultValue)
	{
		// If not connected, just pass through the default value...
		bool returnValue = defaultValue;

		if(TryLockConnection())
		{
			const UInt32 labelid = ComputeLabelHash(label);

			// Check to see if this parameter already exists, if so just return the current value...
			g_paramLock.ReadLock();
			BoolParamMap::iterator found = g_boolParams.find(labelid);
			if(found != g_boolParams.end())
			{
				const BoolParam oldParam = (*found).second;
				// update return value from cached parameter...
				if(oldParam.useCurrent)
				{
					returnValue = oldParam.currentValue;
				}
				// if the parameter range hasn't changed, just return the current value... else we need to update the param.
				if(oldParam.defaultValue == defaultValue)
				{
					g_paramLock.ReadUnlock();
					UnlockConnection();
					return returnValue;
				}
			}
			g_paramLock.ReadUnlock();

			// New parameter... add it to the param map...
			g_paramLock.WriteLock();
			BoolParam &newParam   = g_boolParams[labelid];
			newParam.defaultValue = defaultValue;
			g_paramLock.WriteUnlock();

			// Report new parameter to remote monitor...
			BoolParamPacket packet;
			packet.labelID = labelid; 
			packet.value   = defaultValue ? 1 : 0;
			AsyncStream::Acquire()->WritePacket(packet);

			UnlockConnection();
		}

		// This is a new variable, so just return the default...
		return returnValue;
	}

	// Check to see if a remote button was clicked.
	bool ButtonClicked(const Label &label)
	{
		if(TryLockConnection())
		{
			const UInt32 labelid = ComputeLabelHash(label);

			// Check to see if this parameter already exists, if so just return the current value...
			g_paramLock.ReadLock();
			ButtonParamMap::iterator found = g_buttonParams.find(labelid);
			if(found != g_buttonParams.end())
			{
				const bool wasClicked = (*found).second;
				(*found).second = false;
				g_paramLock.ReadUnlock();
				UnlockConnection();
				return wasClicked;
			}
			g_paramLock.ReadUnlock();

			// New parameter... add it to the param map...
			g_paramLock.WriteLock();
			g_buttonParams[labelid] = false;
			g_paramLock.WriteUnlock();

			// Report new parameter to remote monitor...
			ButtonParamPacket packet;
			packet.labelID = labelid; 
			AsyncStream::Acquire()->WritePacket(packet);

			UnlockConnection();
		}

		// either no connection or new button
		return false;
	}

} // namespace Capture
} // namespace OVR
