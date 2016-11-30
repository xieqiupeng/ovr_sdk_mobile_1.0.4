# Oculus Mobile Performance Capture Library
OVRCapture is a small static library that serves up real-time performance data to Oculus Remote Monitor. For convenience it is integrated into VrApi automatically, which means any application that integrates and initializes VrApi can use it without integrating OVRCapture directly to gain insight into inner workings of Oculus' runtime and process-wide information like Logcat/stdout logs, memory allocations and CPU scheduling events.

Integrating OVRCapture directly allows applications to feed application specific data such as CPU performance zones, and runtime tunable parameters that VrApi alone simply doesn't have knowledge of.

More information about Oculus Remote Monitor can be found on the [Oculus Developer site](https://developer.oculus.com/documentation/mobilesdk/latest).

# Prerequisites
For Android applications, network access is required in order for Oculus Remote Monitor to connect. This can be done by adding the following line to your AndroidManifest.xml: `<uses-permission android:name="android.permission.INTERNET" />`

# Integration
## Linking
### ndk-build
If your application is using ndk-build based makefiles, linking to VrCapture is easy.

- Include at the bottom of your Android.mk... `include <PATH_TO_VRCAPTURE>/Projects/Android/jni/Android.mk`
- Between `include $(CLEAR_VARS)` and `include $(BUILD_SHARED_LIBRARY)` add... `LOCAL_STATIC_LIBRARIES += vrcapture`

And thats it, VrCapture will now be linked into your application and header search paths setup properly. This is also convenient because it will automatically compile OVRCapture to your target architecture.

### Other
If you are using a custom build system there are three things to know.

- The command-line to build OVRCapture is... `ndk-build -j8 -C <PATH_TO_VRCAPTURE>/Projects/Android`
- The compiler arguments are... `-I<PATH_TO_VRCAPTURE>/Include`
- The linker arguments are... `-L<PATH_TO_VRCAPTURE>/Projects/Android/obj/local/<TARGET_ARCHITECTURE> -lvrcapture`

## Initialize and Shutdown network server
Capturing over socket is a quick and convenient way of analyzing/monitoring applications in real-time. Connections can be remotely established, disconnected and reestablished at any time during the life of the app so long as `InitForRemoteCapture()` has been called.

- Include the primary VrCapture header... `#include <OVR_Capture.h>`
- Somewhere near the beginning of your application... `OVR::Capture::InitForRemoteCapture();`
- Somewhere near the end of your application... `OVR::Capture::Shutdown();`

## Initialize and Shutdown local file capture
Capturing straight to disk is convenient for scenarios where network performance does not allow for robust real-time capturing or for setting up automated performance testing. Once initialized in this mode OVRCapture will continuously capture a stream of data to disk until `Shutdown()` is called.

- Enable external storage write permissions in your AndroidManifest.xml... `<uses-permission android:name="android.permission.WRITE_EXTERNAL_STORAGE"/>`
- Include the primary VrCapture header... `#include <OVR_Capture.h>`
- Somewhere near the beginning of your application... `OVR::Capture:: InitForLocalCapture("/sdcard/capture.dat");`
- Somewhere near the end of your application... `OVR::Capture::Shutdown();`

To pull a capture off your device...

- Oculus Remote Monitor expects capture file to be gzip compressed, so before downloading from the device make sure you compress your capture.dat file... `adb shell gzip -9 /sdcard/capture.dat`
- Download your compressed capture file... `adb pull /sdcard/capture.dat.gz capture.dat`

## CPU Zones
One of the fundamental pieces of OVRMonitor is its ability to handle a large number of performance zones. Scoped performance zones can be dropped in simply with a single line and have extremely low overhead. e.g....

```
void SomeFunction(void)
{
  OVR_CAPTURE_CPU_ZONE( SomeFunction );
  DoSomethingExpensive();
}
```

## Remote Variables
Remote Variables allow Oculus Remote Monitor to override the value of "constants" remotely in real-time using buttons, sliders or checkboxes depending on the type. This allows developers to quickly iterate on the look, feel and performance of their application without rebuilding or relaunching. OVRCapture implements this efficiently so that when Oculus Remote Monitor is not connected the function immediately returns the default value.

Once connected though, `GetVariable(...)` will return the latest value set in Oculus Remote Monitor.

Like many other features VrApi already implements several remote variables for a number of internal features and debug modes (for instance TimeWarp texture density visualization).

Below are some simple usage examples...

### Float
```
static const OVR::Capture::Label jumpHeightLabel( "Jump Height" );
const float defaultValue = 0.4f;
const float minValue     = 0.1f;
const float maxValue     = 10.0f;
const float jumpHeight   = OVR::Capture::GetVariable( jumpHeightLabel, defaultValue, minValue, maxValue );
DoPlayerJump( jumpHeight );
```

### Int
```
static const OVR::Capture::Label playerHealthLabel( "Player Health" );
const int minValue = 0;
const int maxValue = 100;
const int health   = OVR::Capture::GetVariable( playerHealthLabel, mCurrentHealth, minValue, maxValue );
DoUpdatePlayerState( health );
```

### Bool
```
static const OVR::Capture::Label enableParticlesLabel( "Enable Particles" );
const bool defaultValue    = true;
const bool enableParticles = OVR::Capture::GetVariable( enableParticlesLabel, defaultValue );
if ( enableParticles == true )
{
	DoParticles();
}
```

### Button
Buttons are a special case of `bool` where have an implicit default value of `false` and return `true` only once for each click. This makes it easy to implement fire-once events...

```
static const OVR::Capture::Label shouldKillPlayer( "Kill Player" );
if ( OVR::Capture::ButtonClicked( shouldKillPlayer ) )
{
	DoKillPlayer();
}
```
