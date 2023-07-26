//============ Copyright (c) Valve Corporation, All rights reserved. ============
#include "controller_device_driver.h"

#include "driverlog.h"
#include "vrmath.h"
#include <algorithm>
#include <cstdint>

// Let's create some variables for strings used in getting settings.
// This is the section where all of the settings we want are stored. A section name can be anything,
// but if you want to store driver specific settings, it's best to namespace the section with the driver identifier
// ie "<my_driver>_<section>" to avoid collisions
static const char *my_controller_main_settings_section = "driver_derivativeloco";

// These are the keys we want to retrieve the values for in the settings
static const char *my_controller_settings_key_model_number = "mycontroller_model_number";
static const char *my_controller_settings_key_serial_number = "mycontroller_serial_number";
static const char* my_controller_settings_key_offset_x = "mycontroller_offset_x";
static const char* my_controller_settings_key_offset_z = "mycontroller_offset_z";
static const char* my_controller_settings_key_radius = "mycontroller_radius";
static const char* my_controller_settings_key_hmd_pivot_offset = "mycontroller_hmd_pivot_offset";

MyControllerDeviceDriver::MyControllerDeviceDriver()
{
	// Set a member to keep track of whether we've activated yet or not
	is_active_ = false;

	// The constructor takes a role argument, that gives us information about if our controller is a left or right hand.
	// Let's store it for later use. We'll need it.
	my_controller_role_ = vr::TrackedControllerRole_Treadmill;

	// We have our model number and serial number stored in SteamVR settings. We need to get them and do so here.
	// Other IVRSettings methods (to get int32, floats, bools) return the data, instead of modifying, but strings are
	// different.
	char model_number[ 1024 ];
	vr::VRSettings()->GetString( my_controller_main_settings_section, my_controller_settings_key_model_number, model_number, sizeof( model_number ) );
	my_controller_model_number_ = model_number;

	// Get our serial number depending on our "handedness"
	char serial_number[ 1024 ];
	vr::VRSettings()->GetString( my_controller_main_settings_section, my_controller_settings_key_serial_number, serial_number, sizeof( serial_number ) );
	my_controller_serial_number_ = serial_number;

	my_controller_offset_x_ = vr::VRSettings()->GetFloat(my_controller_main_settings_section, my_controller_settings_key_offset_x);
	my_controller_offset_z_ = vr::VRSettings()->GetFloat(my_controller_main_settings_section, my_controller_settings_key_offset_z);
	my_controller_radius_ = vr::VRSettings()->GetFloat(my_controller_main_settings_section, my_controller_settings_key_radius);
	my_controller_hmd_pivot_offset_ = vr::VRSettings()->GetFloat(my_controller_main_settings_section, my_controller_settings_key_hmd_pivot_offset);

	// Here's an example of how to use our logging wrapper around IVRDriverLog
	// In SteamVR logs (SteamVR Hamburger Menu > Developer Settings > Web console) drivers have a prefix of
	// "<driver_name>:". You can search this in the top search bar to find the info that you've logged.
	DriverLog( "My Controller Model Number: %s", my_controller_model_number_.c_str() );
	DriverLog( "My Controller Serial Number: %s", my_controller_serial_number_.c_str() );
}

//-----------------------------------------------------------------------------
// Purpose: This is called by vrserver after our
//  IServerTrackedDeviceProvider calls IVRServerDriverHost::TrackedDeviceAdded.
//-----------------------------------------------------------------------------
vr::EVRInitError MyControllerDeviceDriver::Activate( uint32_t unObjectId )
{
	// Set an member to keep track of whether we've activated yet or not
	is_active_ = true;

	// Let's keep track of our device index. It'll be useful later.
	my_controller_index_ = unObjectId;

	// Properties are stored in containers, usually one container per device index. We need to get this container to set
	// The properties we want, so we call this to retrieve a handle to it.
	vr::PropertyContainerHandle_t container = vr::VRProperties()->TrackedDeviceToPropertyContainer( my_controller_index_ );

	// Let's begin setting up the properties now we've got our container.
	// A list of properties available is contained in vr::ETrackedDeviceProperty.

	// First, let's set the model number.
	vr::VRProperties()->SetStringProperty( container, vr::Prop_ModelNumber_String, my_controller_model_number_.c_str() );

	// Let's tell SteamVR our role which we received from the constructor earlier.
	vr::VRProperties()->SetInt32Property( container, vr::Prop_ControllerRoleHint_Int32, my_controller_role_ );


	// Now let's set up our inputs

	// This tells the UI what to show the user for bindings for this controller,
	// As well as what default bindings should be for legacy apps.
	// Note, we can use the wildcard {<driver_name>} to match the root folder location
	// of our driver.
	vr::VRProperties()->SetStringProperty( container, vr::Prop_InputProfilePath_String, "{derivativeloco}/input/mycontroller_profile.json" );

	// Let's set up handles for all of our components.
	// Even though these are also defined in our input profile,
	// We need to get handles to them to update the inputs.

	// CreateScalarComponent requires:
	// EVRScalarType - whether the device can give an absolute position, or just one relative to where it was last. We
	// can do it absolute.
	// EVRScalarUnits - whether the devices has two "sides", like a joystick. This makes the range of valid inputs -1
	// to 1. Otherwise, it's 0 to 1. We only have one "side", so ours is onesided.
	vr::VRDriverInput()->CreateScalarComponent(container, "/input/joystick/x", &input_handles_[MyComponent_joystick_x], vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedTwoSided);
	vr::VRDriverInput()->CreateScalarComponent(container, "/input/joystick/y", &input_handles_[MyComponent_joystick_y], vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedTwoSided);

	my_pose_update_thread_ = std::thread( &MyControllerDeviceDriver::MyPoseUpdateThread, this );

	// We've activated everything successfully!
	// Let's tell SteamVR that by saying we don't have any errors.
	return vr::VRInitError_None;
}

//-----------------------------------------------------------------------------
// Purpose: If you're an HMD, this is where you would return an implementation
// of vr::IVRDisplayComponent, vr::IVRVirtualDisplay or vr::IVRDirectModeComponent.
//
// But this a simple example to demo for a controller, so we'll just return nullptr here.
//-----------------------------------------------------------------------------
void *MyControllerDeviceDriver::GetComponent( const char *pchComponentNameAndVersion )
{
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: This is called by vrserver when a debug request has been made from an application to the driver.
// What is in the response and request is up to the application and driver to figure out themselves.
//-----------------------------------------------------------------------------
void MyControllerDeviceDriver::DebugRequest( const char *pchRequest, char *pchResponseBuffer, uint32_t unResponseBufferSize )
{
	if ( unResponseBufferSize >= 1 )
		pchResponseBuffer[ 0 ] = 0;
}

//-----------------------------------------------------------------------------
// Purpose: This is never called by vrserver in recent OpenVR versions,
// but is useful for giving data to vr::VRServerDriverHost::TrackedDevicePoseUpdated.
//-----------------------------------------------------------------------------
vr::DriverPose_t MyControllerDeviceDriver::GetPose()
{
	static int logTick = 0;

	// Let's retrieve the Hmd pose to base our controller pose off.

	// First, initialize the struct that we'll be submitting to the runtime to tell it we've updated our pose.
	vr::DriverPose_t pose = { 0 };

	// These need to be set to be valid quaternions. The device won't appear otherwise.
	pose.qWorldFromDriverRotation.w = 1.f;
	pose.qDriverFromHeadRotation.w = 1.f;

	vr::TrackedDevicePose_t hmd_pose{};

	// GetRawTrackedDevicePoses expects an array.
	// We only want the hmd pose, which is at index 0 of the array so we can just pass the struct in directly, instead of in an array
	vr::VRServerDriverHost()->GetRawTrackedDevicePoses(0.f, &hmd_pose, 1);

	// Get the position of the hmd from the 3x4 matrix GetRawTrackedDevicePoses returns
	const vr::HmdVector3_t hmd_position = HmdVector3_From34Matrix(hmd_pose.mDeviceToAbsoluteTracking);
	// Get the orientation of the hmd from the 3x4 matrix GetRawTrackedDevicePoses returns
	const vr::HmdQuaternion_t hmd_orientation = HmdQuaternion_FromMatrix(hmd_pose.mDeviceToAbsoluteTracking);

	// Calculate the hmd pivot point offset
	vr::HmdVector3_t hmd_pivot_offset = HmdVector3_Backward;
	hmd_pivot_offset.v[2] *= my_controller_hmd_pivot_offset_;
	hmd_pivot_offset = hmd_pivot_offset * hmd_orientation;

	const vr::HmdVector3_t head_position = hmd_position + hmd_pivot_offset;
	
	// Get the relative offset from the center of the play space
	vr::HmdVector3_t hmd_center_offset_position;
	hmd_center_offset_position.v[0] = head_position.v[0] - my_controller_offset_x_;
	hmd_center_offset_position.v[2] = head_position.v[2] - my_controller_offset_z_;

	// Get angle of HMD orientation projected onto the X/Z plane
	vr::HmdVector3_t hmd_projected_position = HmdVector3_Forward * hmd_orientation;
	float dot_prod = hmd_projected_position.v[2];
	float projected_angle = acos(dot_prod / sqrtf(hmd_projected_position.v[0] * hmd_projected_position.v[0] + hmd_projected_position.v[2] * hmd_projected_position.v[2]));

	if (hmd_projected_position.v[0] > 0) {
		projected_angle *= -1;
	}

	vr::HmdVector3_t joystick_vector = hmd_center_offset_position * HmdQuaternion_FromEulerAngles(0, 0, projected_angle);

	float joyX = std::clamp(joystick_vector.v[0] / my_controller_radius_, -1.0f, 1.0f) * -1;
	float joyY = std::clamp(joystick_vector.v[2] / my_controller_radius_, -1.0f, 1.0f);

	if (logTick % 100 == 0) {
		DriverLog("HMD: x=%f, z=%f, a=%f   JOY: x=%f, y=%f", head_position.v[0], head_position.v[2], projected_angle, joyX, joyY);
	}
	logTick++;

	vr::VRDriverInput()->UpdateScalarComponent(input_handles_[MyComponent_joystick_x], joyX, 0);
	vr::VRDriverInput()->UpdateScalarComponent(input_handles_[MyComponent_joystick_y], joyY, 0);

	// copy our position to our pose
	pose.vecPosition[ 0 ] = head_position.v[0];
	pose.vecPosition[ 1 ] = 10.0f;
	pose.vecPosition[ 2 ] = head_position.v[2];

	// The pose we provided is valid.
	pose.poseIsValid = true;

	// Our device is always connected.
	// In reality with physical devices, when they get disconnected,
	// set this to false and icons in SteamVR will be updated to show the device is disconnected
	pose.deviceIsConnected = true;

	// The state of our tracking. For our virtual device, it's always going to be ok,
	// but this can get set differently to inform the runtime about the state of the device's tracking
	// and update the icons to inform the user accordingly.
	pose.result = vr::TrackingResult_Running_OK;

	return pose;
}

void MyControllerDeviceDriver::MyPoseUpdateThread()
{
	while ( is_active_ )
	{
		// Inform the vrserver that our tracked device's pose has updated, giving it the pose returned by our GetPose().
		vr::VRServerDriverHost()->TrackedDevicePoseUpdated( my_controller_index_, GetPose(), sizeof( vr::DriverPose_t ) );

		// Delay
		std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: This is called by vrserver when the device should enter standby mode.
// The device should be put into whatever low power mode it has.
// We don't really have anything to do here, so let's just log something.
//-----------------------------------------------------------------------------
void MyControllerDeviceDriver::EnterStandby()
{
	DriverLog( "controller has been put on standby" );
}

//-----------------------------------------------------------------------------
// Purpose: This is called by vrserver when the device should deactivate.
// This is typically at the end of a session
// The device should free any resources it has allocated here.
//-----------------------------------------------------------------------------
void MyControllerDeviceDriver::Deactivate()
{
	// Let's join our pose thread that's running
	// by first checking then setting is_active_ to false to break out
	// of the while loop, if it's running, then call .join() on the thread
	if ( is_active_.exchange( false ) )
	{
		my_pose_update_thread_.join();
	}

	// unassign our controller index (we don't want to be calling vrserver anymore after Deactivate() has been called
	my_controller_index_ = vr::k_unTrackedDeviceIndexInvalid;
}


//-----------------------------------------------------------------------------
// Purpose: This is called by our IServerTrackedDeviceProvider when its RunFrame() method gets called.
// It's not part of the ITrackedDeviceServerDriver interface, we created it ourselves.
//-----------------------------------------------------------------------------
void MyControllerDeviceDriver::MyRunFrame()
{
	// update our inputs here
}


//-----------------------------------------------------------------------------
// Purpose: This is called by our IServerTrackedDeviceProvider when it pops an event off the event queue.
// It's not part of the ITrackedDeviceServerDriver interface, we created it ourselves.
//-----------------------------------------------------------------------------
void MyControllerDeviceDriver::MyProcessEvent( const vr::VREvent_t &vrevent )
{
	switch ( vrevent.eventType )
	{
		// Listen for haptic events
		case vr::VREvent_Input_HapticVibration:
		{
			// We now need to make sure that the event was intended for this device.
			// So let's compare handles of the event and our haptic component

			/*
			if ( vrevent.data.hapticVibration.componentHandle == input_handles_[ MyComponent_haptic ] )
			{
				// The event was intended for us!
				// To convert the data to a pulse, see the docs.
				// For this driver, we'll just print the values.

				float duration = vrevent.data.hapticVibration.fDurationSeconds;
				float frequency = vrevent.data.hapticVibration.fFrequency;
				float amplitude = vrevent.data.hapticVibration.fAmplitude;

				DriverLog( "Haptic event triggered for %s hand. Duration: %.2f, Frequency: %.2f, Amplitude: %.2f", my_controller_role_ == vr::TrackedControllerRole_LeftHand ? "left" : "right",
					duration, frequency, amplitude );
			}
			*/
			break;
		}
		default:
			break;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Our IServerTrackedDeviceProvider needs our serial number to add us to vrserver.
// It's not part of the ITrackedDeviceServerDriver interface, we created it ourselves.
//-----------------------------------------------------------------------------
const std::string &MyControllerDeviceDriver::MyGetSerialNumber()
{
	return my_controller_serial_number_;
}