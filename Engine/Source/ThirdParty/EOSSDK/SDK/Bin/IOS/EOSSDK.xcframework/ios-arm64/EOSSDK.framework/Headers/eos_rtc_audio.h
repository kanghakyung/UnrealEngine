// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include <EOSSDK/eos_rtc_audio_types.h>

/**
 * The RTC Audio Interface. This is used to manage Audio specific RTC features
 *
 * @see EOS_RTC_GetAudioInterface
 */

/**
 * Use this function to push a new audio buffer to be sent to the participants of a room.
 *
 * This should only be used if Manual Audio Input was enabled locally for the specified room.
 *
 * @param Options structure containing the parameters for the operation.
 * @return EOS_Success if the buffer was successfully queued for sending
 *         EOS_InvalidParameters if any of the parameters are incorrect
 *         EOS_NotFound if the specified room was not found
 *         EOS_InvalidState if manual recording was not enabled when joining the room.
 * @see EOS_RTC_JoinRoomOptions
 * @see EOS_Lobby_LocalRTCOptions
 */
EOS_DECLARE_FUNC(EOS_EResult) EOS_RTCAudio_SendAudio(EOS_HRTCAudio Handle, const EOS_RTCAudio_SendAudioOptions* Options);

/**
 * Use this function to tweak outgoing audio options for a room.
 *
 * @note Due to internal implementation details, this function requires that you first register to any notification for room
 *
 * @param Options structure containing the parameters for the operation.
 * @param ClientData Arbitrary data that is passed back in the CompletionDelegate
 * @param CompletionDelegate The callback to be fired when the operation completes, either successfully or in error
 * @return EOS_Success if the operation succeeded
 *         EOS_InvalidParameters if any of the parameters are incorrect
 *         EOS_NotFound if the local user is not in the room
 */
EOS_DECLARE_FUNC(void) EOS_RTCAudio_UpdateSending(EOS_HRTCAudio Handle, const EOS_RTCAudio_UpdateSendingOptions* Options, void* ClientData, const EOS_RTCAudio_OnUpdateSendingCallback CompletionDelegate);

/**
 * Use this function to tweak incoming audio options for a room.
 *
 * @note Due to internal implementation details, this function requires that you first register to any notification for room
 *
 * @param Options structure containing the parameters for the operation.
 * @param ClientData Arbitrary data that is passed back in the CompletionDelegate
 * @param CompletionDelegate The callback to be fired when the operation completes, either successfully or in error
 * @return EOS_Success if the operation succeeded
 *         EOS_InvalidParameters if any of the parameters are incorrect
 *         EOS_NotFound if either the local user or specified participant are not in the room
 */
EOS_DECLARE_FUNC(void) EOS_RTCAudio_UpdateReceiving(EOS_HRTCAudio Handle, const EOS_RTCAudio_UpdateReceivingOptions* Options, void* ClientData, const EOS_RTCAudio_OnUpdateReceivingCallback CompletionDelegate);

/**
 * Use this function to change outgoing audio volume for a room.
 *
 * @note Due to internal implementation details, this function requires that you first register to any notification for room
 *
 * @param Options structure containing the parameters for the operation.
 * @param ClientData Arbitrary data that is passed back in the CompletionDelegate
 * @param CompletionDelegate The callback to be fired when the operation completes, either successfully or in error
 * @return EOS_Success if the operation succeeded
 *         EOS_InvalidParameters if any of the parameters are incorrect
 *         EOS_NotFound if the local user is not in the room
 */
EOS_DECLARE_FUNC(void) EOS_RTCAudio_UpdateSendingVolume(EOS_HRTCAudio Handle, const EOS_RTCAudio_UpdateSendingVolumeOptions* Options, void* ClientData, const EOS_RTCAudio_OnUpdateSendingVolumeCallback CompletionDelegate);

/**
 * Use this function to change incoming audio volume for a room.
 *
 * @note Due to internal implementation details, this function requires that you first register to any notification for room
 *
 * @param Options structure containing the parameters for the operation.
 * @param ClientData Arbitrary data that is passed back in the CompletionDelegate
 * @param CompletionDelegate The callback to be fired when the operation completes, either successfully or on error
 * @return EOS_Success if the operation succeeded
 *         EOS_InvalidParameters if any of the parameters are incorrect
 *         EOS_NotFound if the local user is not in the room
 */
EOS_DECLARE_FUNC(void) EOS_RTCAudio_UpdateReceivingVolume(EOS_HRTCAudio Handle, const EOS_RTCAudio_UpdateReceivingVolumeOptions* Options, void* ClientData, const EOS_RTCAudio_OnUpdateReceivingVolumeCallback CompletionDelegate);

/**
 * Use this function to change participant audio volume for a room.
 *
 * @note Due to internal implementation details, this function requires that you first register to any notification for room
 *
 * @param Options structure containing the parameters for the operation.
 * @param ClientData Arbitrary data that is passed back in the CompletionDelegate
 * @param CompletionDelegate The callback to be fired when the operation completes, either successfully or in error
 * @return EOS_Success if the operation succeeded
 *         EOS_InvalidParameters if any of the parameters are incorrect
 *         EOS_NotFound if either the local user or specified participant are not in the room
 */
EOS_DECLARE_FUNC(void) EOS_RTCAudio_UpdateParticipantVolume(EOS_HRTCAudio Handle, const EOS_RTCAudio_UpdateParticipantVolumeOptions* Options, void* ClientData, const EOS_RTCAudio_OnUpdateParticipantVolumeCallback CompletionDelegate);

/**
 * Register to receive notifications when a room participant audio status is updated (f.e when mute state changes or speaking flag changes).
 *
 * The notification is raised when the participant's audio status is updated. In order not to miss any participant status changes, applications need to add the notification before joining a room.
 *
 * If the returned NotificationId is valid, you must call EOS_RTCAudio_RemoveNotifyParticipantUpdated when you no longer wish
 * to have your CompletionDelegate called.
 *
 * @param ClientData Arbitrary data that is passed back in the CompletionDelegate
 * @param CompletionDelegate The callback to be fired when a participant changes audio status
 * @return Notification ID representing the registered callback if successful, an invalid NotificationId if not
 *
 * @see EOS_INVALID_NOTIFICATIONID
 * @see EOS_RTCAudio_RemoveNotifyParticipantUpdated
 * @see EOS_RTCAudio_ParticipantUpdatedCallbackInfo
 * @see EOS_ERTCAudioStatus
 */
EOS_DECLARE_FUNC(EOS_NotificationId) EOS_RTCAudio_AddNotifyParticipantUpdated(EOS_HRTCAudio Handle, const EOS_RTCAudio_AddNotifyParticipantUpdatedOptions* Options, void* ClientData, const EOS_RTCAudio_OnParticipantUpdatedCallback CompletionDelegate);

/**
 * Unregister a previously bound notification handler from receiving participant updated notifications
 *
 * @param NotificationId The Notification ID representing the registered callback
 */
EOS_DECLARE_FUNC(void) EOS_RTCAudio_RemoveNotifyParticipantUpdated(EOS_HRTCAudio Handle, EOS_NotificationId NotificationId);

/**
 * Register to receive notifications when an audio device is added or removed to the system.
 *
 * If the returned NotificationId is valid, you must call EOS_RTCAudio_RemoveNotifyAudioDevicesChanged when you no longer wish
 * to have your CompletionDelegate called.
 *
 * The library will try to use user selected audio device while following these rules:
 * - if none of the audio devices has been available and connected before - the library will try to use it;
 * - if user selected device failed for some reason, default device will be used instead (and user selected device will be memorized);
 * - if user selected a device but it was not used for some reason (and default was used instead), when devices selection is triggered we will try to use user selected device again;
 * - triggers to change a device: when new audio device appears or disappears - library will try to use previously user selected;
 * - if for any reason, a device cannot be used - the library will fallback to using default;
 * - if a configuration of the current audio device has been changed, it will be restarted.
 *
 * @param ClientData Arbitrary data that is passed back in the CompletionDelegate
 * @param CompletionDelegate The callback to be fired when an audio device change occurs
 * @return Notification ID representing the registered callback if successful, an invalid NotificationId if not
 *
 * @see EOS_INVALID_NOTIFICATIONID
 * @see EOS_RTCAudio_RemoveNotifyAudioDevicesChanged
 * @see EOS_RTCAudio_SetAudioInputSettingsOptions
 * @see EOS_RTCAudio_SetAudioOutputSettingsOptions
 */
EOS_DECLARE_FUNC(EOS_NotificationId) EOS_RTCAudio_AddNotifyAudioDevicesChanged(EOS_HRTCAudio Handle, const EOS_RTCAudio_AddNotifyAudioDevicesChangedOptions* Options, void* ClientData, const EOS_RTCAudio_OnAudioDevicesChangedCallback CompletionDelegate);

/**
 * Unregister a previously bound notification handler from receiving audio devices notifications
 *
 * @param NotificationId The Notification ID representing the registered callback
 */
EOS_DECLARE_FUNC(void) EOS_RTCAudio_RemoveNotifyAudioDevicesChanged(EOS_HRTCAudio Handle, EOS_NotificationId NotificationId);

/**
 * Register to receive notifications when audio input state changed.
 *
 * If the returned NotificationId is valid, you must call EOS_RTCAudio_RemoveNotifyAudioInputState when you no longer wish to
 * have your CompletionDelegate called.
 *
 * @param ClientData Arbitrary data that is passed back in the CompletionDelegate
 * @param CompletionDelegate The callback to be fired when audio input state changes
 * @return Notification ID representing the registered callback if successful, an invalid NotificationId if not
 *
 * @see EOS_INVALID_NOTIFICATIONID
 * @see EOS_RTCAudio_RemoveNotifyAudioInputState
 */
EOS_DECLARE_FUNC(EOS_NotificationId) EOS_RTCAudio_AddNotifyAudioInputState(EOS_HRTCAudio Handle, const EOS_RTCAudio_AddNotifyAudioInputStateOptions* Options, void* ClientData, const EOS_RTCAudio_OnAudioInputStateCallback CompletionDelegate);

/**
 * Unregister a previously bound notification handler from receiving notifications on audio input state changed.
 *
 * @param NotificationId The Notification ID representing the registered callback
 */
EOS_DECLARE_FUNC(void) EOS_RTCAudio_RemoveNotifyAudioInputState(EOS_HRTCAudio Handle, EOS_NotificationId NotificationId);

/**
 * Register to receive notifications when audio output state changed.
 *
 * If the returned NotificationId is valid, you must call EOS_RTCAudio_RemoveNotifyAudioOutputState when you no longer wish to
 * have your CompletionDelegate called.
 *
 * @param ClientData Arbitrary data that is passed back in the CompletionDelegate
 * @param CompletionDelegate The callback to be fired when audio output state changes
 * @return Notification ID representing the registered callback if successful, an invalid NotificationId if not
 *
 * @see EOS_INVALID_NOTIFICATIONID
 * @see EOS_RTCAudio_RemoveNotifyAudioOutputState
 */
EOS_DECLARE_FUNC(EOS_NotificationId) EOS_RTCAudio_AddNotifyAudioOutputState(EOS_HRTCAudio Handle, const EOS_RTCAudio_AddNotifyAudioOutputStateOptions* Options, void* ClientData, const EOS_RTCAudio_OnAudioOutputStateCallback CompletionDelegate);

/**
 * Unregister a previously bound notification handler from receiving notifications on audio output state changed.
 *
 * @param NotificationId The Notification ID representing the registered callback
 */
EOS_DECLARE_FUNC(void) EOS_RTCAudio_RemoveNotifyAudioOutputState(EOS_HRTCAudio Handle, EOS_NotificationId NotificationId);

/**
 * Register to receive notifications when local audio buffers are about to be encoded and sent.
 *
 * This gives you access to the audio data about to be sent, allowing for example the implementation of custom filters/effects.
 *
 * If the returned NotificationId is valid, you must call EOS_RTCAudio_RemoveNotifyAudioBeforeSend when you no longer wish to
 * have your CompletionDelegate called.
 *
 * @note The CompletionDelegate may be called from a thread other than the one from which the SDK is ticking.
 *
 * @param ClientData Arbitrary data that is passed back in the CompletionDelegate
 * @param CompletionDelegate The callback to be fired when local audio buffers are about to be encoded and sent
 * @return Notification ID representing the registered callback if successful, an invalid NotificationId if not
 *
 * @see EOS_INVALID_NOTIFICATIONID
 * @see EOS_RTCAudio_RemoveNotifyAudioBeforeSend
 */
EOS_DECLARE_FUNC(EOS_NotificationId) EOS_RTCAudio_AddNotifyAudioBeforeSend(EOS_HRTCAudio Handle, const EOS_RTCAudio_AddNotifyAudioBeforeSendOptions* Options, void* ClientData, const EOS_RTCAudio_OnAudioBeforeSendCallback CompletionDelegate);

/**
 * Unregister a previously bound notification handler from receiving local audio buffers before they are encoded and sent.
 *
 * @param NotificationId The Notification ID representing the registered callback
 */
EOS_DECLARE_FUNC(void) EOS_RTCAudio_RemoveNotifyAudioBeforeSend(EOS_HRTCAudio Handle, EOS_NotificationId NotificationId);

/**
 * Register to receive notifications with remote audio buffers before they are rendered.
 *
 * This gives you access to the audio data received, allowing for example the implementation of custom filters/effects.
 *
 * If the returned NotificationId is valid, you must call EOS_RTCAudio_RemoveNotifyAudioBeforeRender when you no longer wish to
 * have your CompletionDelegate called.
 *
 * @note The CompletionDelegate may be called from a thread other than the one from which the SDK is ticking.
 *
 * @param ClientData Arbitrary data that is passed back in the CompletionDelegate
 * @param CompletionDelegate The callback to be fired when remote audio buffers are about to be rendered
 * @return Notification ID representing the registered callback if successful, an invalid NotificationId if not
 *
 * @see EOS_INVALID_NOTIFICATIONID
 * @see EOS_RTCAudio_RemoveNotifyAudioBeforeRender
 */
EOS_DECLARE_FUNC(EOS_NotificationId) EOS_RTCAudio_AddNotifyAudioBeforeRender(EOS_HRTCAudio Handle, const EOS_RTCAudio_AddNotifyAudioBeforeRenderOptions* Options, void* ClientData, const EOS_RTCAudio_OnAudioBeforeRenderCallback CompletionDelegate);

/**
 * Unregister a previously bound notification handler from receiving remote audio buffers before they are rendered.
 *
 * @param NotificationId The Notification ID representing the registered callback
 */
EOS_DECLARE_FUNC(void) EOS_RTCAudio_RemoveNotifyAudioBeforeRender(EOS_HRTCAudio Handle, EOS_NotificationId NotificationId);

/**
 * Use this function to inform the audio system of a user.
 *
 * This function is only necessary for some platforms.
 *
 * @param Options structure containing the parameters for the operation
 * @param ClientData Arbitrary data that is passed back in the CompletionDelegate
 * @param CompletionDelegate The callback to be fired when the operation completes, either successfully or in error
 * @return EOS_Success if the user was successfully registered
 *         EOS_InvalidParameters if any of the parameters are incorrect
 *         EOS_UnexpectedError otherwise
 *
 * @see EOS_RTCAudio_UnregisterPlatformUser
 */
EOS_DECLARE_FUNC(void) EOS_RTCAudio_RegisterPlatformUser(EOS_HRTCAudio Handle, const EOS_RTCAudio_RegisterPlatformUserOptions* Options, void* ClientData, const EOS_RTCAudio_OnRegisterPlatformUserCallback CompletionDelegate);

/**
 * Use this function to remove a user that was added with EOS_RTCAudio_RegisterPlatformUser.
 *
 * This function is only necessary for some platforms.
 *
 * @param Options structure containing the parameters for the operation
 * @param ClientData Arbitrary data that is passed back in the CompletionDelegate
 * @param CompletionDelegate The callback to be fired when the operation completes, either successfully or in error
 * @return EOS_Success if the user was successfully unregistered
 *         EOS_InvalidParameters if any of the parameters are incorrect
 *         EOS_UnexpectedError otherwise
 */
EOS_DECLARE_FUNC(void) EOS_RTCAudio_UnregisterPlatformUser(EOS_HRTCAudio Handle, const EOS_RTCAudio_UnregisterPlatformUserOptions* Options, void* ClientData, const EOS_RTCAudio_OnUnregisterPlatformUserCallback CompletionDelegate);

/**
 * Query for a list of audio input devices available in the system together with their specifications.
 *
 * @param Options structure containing the parameters for the operation
 * @param ClientData Arbitrary data that is passed back in the CompletionDelegate
 * @param CompletionDelegate The callback to be fired when the operation completes, either successfully or in error
 * @return EOS_Success if the operation succeeded
 *         EOS_InvalidParameters if any of the parameters are incorrect
 */
EOS_DECLARE_FUNC(void) EOS_RTCAudio_QueryInputDevicesInformation(EOS_HRTCAudio Handle, const EOS_RTCAudio_QueryInputDevicesInformationOptions* Options, void* ClientData, const EOS_RTCAudio_OnQueryInputDevicesInformationCallback CompletionDelegate);

/**
 * Fetch the number of audio input devices available in the system that are cached locally.
 *
 * The returned value should not be cached and should instead be used immediately with
 * the EOS_RTCAudio_CopyInputDeviceInformationByIndex function.
 *
 * @param Options structure containing the parameters for the operation
 * @return The number of audio input devices available in the system or 0 if there is an error
 *
 * @see EOS_RTCAudio_CopyInputDeviceInformationByIndex
 * @see EOS_RTCAudio_AddNotifyAudioDevicesChanged
 */
EOS_DECLARE_FUNC(uint32_t) EOS_RTCAudio_GetInputDevicesCount(EOS_HRTCAudio Handle, const EOS_RTCAudio_GetInputDevicesCountOptions* Options);

/**
 * Fetches an audio input device's information from then given index that are cached locally.
 *
 * @param Options structure containing the index being accessed
 * @param OutInputDeviceInformation The audio input device's information for the given index, if it exists and is valid, use EOS_RTCAudio_InputDeviceInformation_Release when finished
 * @return EOS_Success if the information is available and passed out in OutInputDeviceInformation
 *         EOS_InvalidParameters if you pass a null pointer for the out parameter
 *         EOS_NotFound if the audio input device's information is not found
 *
 * @see EOS_RTCAudio_InputDeviceInformation_Release
 * @see EOS_RTCAudio_GetAudioInputDevicesCount
 * @see EOS_RTCAudio_AddNotifyAudioDevicesChanged
 */
EOS_DECLARE_FUNC(EOS_EResult) EOS_RTCAudio_CopyInputDeviceInformationByIndex(EOS_HRTCAudio Handle, const EOS_RTCAudio_CopyInputDeviceInformationByIndexOptions* Options, EOS_RTCAudio_InputDeviceInformation ** OutInputDeviceInformation);

/**
 * Query for a list of audio output devices available in the system together with their specifications.
 *
 * @param Options structure containing the parameters for the operation
 * @param ClientData Arbitrary data that is passed back in the CompletionDelegate
 * @param CompletionDelegate The callback to be fired when the operation completes, either successfully or in error
 * @return EOS_Success if the operation succeeded
 *         EOS_InvalidParameters if any of the parameters are incorrect
 */
EOS_DECLARE_FUNC(void) EOS_RTCAudio_QueryOutputDevicesInformation(EOS_HRTCAudio Handle, const EOS_RTCAudio_QueryOutputDevicesInformationOptions* Options, void* ClientData, const EOS_RTCAudio_OnQueryOutputDevicesInformationCallback CompletionDelegate);

/**
 * Fetch the number of audio output devices available in the system that are cached locally.
 *
 * The returned value should not be cached and should instead be used immediately with
 * the EOS_RTCAudio_CopyOutputDeviceInformationByIndex function.
 *
 * @param Options structure containing the parameters for the operation
 * @return The number of audio output devices available in the system or 0 if there is an error
 *
 * @see EOS_RTCAudio_CopyOutputDeviceInformationByIndex
 * @see EOS_RTCAudio_AddNotifyAudioDevicesChanged
 */
EOS_DECLARE_FUNC(uint32_t) EOS_RTCAudio_GetOutputDevicesCount(EOS_HRTCAudio Handle, const EOS_RTCAudio_GetOutputDevicesCountOptions* Options);

/**
 * Fetches an audio output device's information from then given index that are cached locally.
 *
 * @param Options structure containing the index being accessed
 * @param OutOutputDeviceInformation The audio output device's information for the given index, if it exists and is valid, use EOS_RTCAudio_OutputDeviceInformation_Release when finished
 * @return EOS_Success if the information is available and passed out in OutOutputDeviceInformation
 *         EOS_InvalidParameters if you pass a null pointer for the out parameter
 *         EOS_NotFound if the audio output device's information is not found
 *
 * @see EOS_RTCAudio_OutputDeviceInformation_Release
 * @see EOS_RTCAudio_GetAudioOutputDevicesCount
 * @see EOS_RTCAudio_AddNotifyAudioDevicesChanged
 */
EOS_DECLARE_FUNC(EOS_EResult) EOS_RTCAudio_CopyOutputDeviceInformationByIndex(EOS_HRTCAudio Handle, const EOS_RTCAudio_CopyOutputDeviceInformationByIndexOptions* Options, EOS_RTCAudio_OutputDeviceInformation ** OutOutputDeviceInformation);

/**
 * Use this function to set audio input device settings, such as the active input device, or platform AEC.
 *
 * @param Options structure containing the parameters for the operation
 * @param ClientData Arbitrary data that is passed back in the CompletionDelegate
 * @param CompletionDelegate The callback to be fired when the operation completes, either successfully or in error
 * @return EOS_Success if the setting was successful
 *         EOS_InvalidParameters if any of the parameters are incorrect
 */
EOS_DECLARE_FUNC(void) EOS_RTCAudio_SetInputDeviceSettings(EOS_HRTCAudio Handle, const EOS_RTCAudio_SetInputDeviceSettingsOptions* Options, void* ClientData, const EOS_RTCAudio_OnSetInputDeviceSettingsCallback CompletionDelegate);

/**
 * Use this function to set audio output device settings, such as the active output device.
 *
 * @param Options structure containing the parameters for the operation
 * @param ClientData Arbitrary data that is passed back in the CompletionDelegate
 * @param CompletionDelegate The callback to be fired when the operation completes, either successfully or in error
 * @return EOS_Success if the setting was successful
 *         EOS_InvalidParameters if any of the parameters are incorrect
 */
EOS_DECLARE_FUNC(void) EOS_RTCAudio_SetOutputDeviceSettings(EOS_HRTCAudio Handle, const EOS_RTCAudio_SetOutputDeviceSettingsOptions* Options, void* ClientData, const EOS_RTCAudio_OnSetOutputDeviceSettingsCallback CompletionDelegate);

/**
 * DEPRECATED! Use EOS_RTCAudio_RegisterPlatformUser instead.
 *
 * Use this function to inform the audio system of a user.
 *
 * This function is only necessary for some platforms.
 *
 * @param Options structure containing the parameters for the operation.
 * @return EOS_Success if the user was successfully registered, EOS_UnexpectedError otherwise.
 */
EOS_DECLARE_FUNC(EOS_EResult) EOS_RTCAudio_RegisterPlatformAudioUser(EOS_HRTCAudio Handle, const EOS_RTCAudio_RegisterPlatformAudioUserOptions* Options);

/**
 * DEPRECATED! Use EOS_RTCAudio_UnregisterPlatformUser instead.
 *
 * Use this function to remove a user that was added with EOS_RTCAudio_RegisterPlatformAudioUser.
 *
 * @param Options structure containing the parameters for the operation.
 * @return EOS_Success if the user was successfully unregistered, EOS_UnexpectedError otherwise.
 */
EOS_DECLARE_FUNC(EOS_EResult) EOS_RTCAudio_UnregisterPlatformAudioUser(EOS_HRTCAudio Handle, const EOS_RTCAudio_UnregisterPlatformAudioUserOptions* Options);

/**
 * DEPRECATED! Use EOS_RTCAudio_QueryInputDevicesInformation, EOS_RTCAudio_GetInputDevicesCount instead.
 *
 * Returns the number of audio input devices available in the system.
 *
 * The returned value should not be cached and should instead be used immediately with the EOS_RTCAudio_GetAudioInputDeviceByIndex
 * function.
 *
 * @param Options structure containing the parameters for the operation
 * @return The number of audio input devices
 * @see EOS_RTCAudio_GetAudioInputDeviceByIndex
 * @see EOS_RTCAudio_AddNotifyAudioDevicesChanged
 */
EOS_DECLARE_FUNC(uint32_t) EOS_RTCAudio_GetAudioInputDevicesCount(EOS_HRTCAudio Handle, const EOS_RTCAudio_GetAudioInputDevicesCountOptions* Options);

/**
 * DEPRECATED! Use EOS_RTCAudio_QueryInputDevicesInformation, EOS_RTCAudio_CopyInputDeviceInformationByIndex instead.
 *
 * Fetches an audio input device's info from then given index. The returned value should not be cached and important
 * information should be copied off of the result object immediately.
 *
 * @param Options structure containing the index being accessed
 * @return A pointer to the device information, or NULL on error. You should NOT keep hold of this pointer.
 * @see EOS_RTCAudio_GetAudioInputDevicesCount
 * @see EOS_RTCAudio_AddNotifyAudioDevicesChanged
 */
EOS_DECLARE_FUNC(const EOS_RTCAudio_AudioInputDeviceInfo *) EOS_RTCAudio_GetAudioInputDeviceByIndex(EOS_HRTCAudio Handle, const EOS_RTCAudio_GetAudioInputDeviceByIndexOptions* Options);

/**
 * DEPRECATED! Use EOS_RTCAudio_QueryOutputDevicesInformation, EOS_RTCAudio_GetOutputDevicesCount instead.
 *
 * Returns the number of audio output devices available in the system.
 *
 * The returned value should not be cached and should instead be used immediately with the EOS_RTCAudio_GetAudioOutputDeviceByIndex
 * function.
 *
 * @param Options structure containing the parameters for the operation
 * @return The number of audio output devices
 * @see EOS_RTCAudio_GetAudioOutputDeviceByIndex
 * @see EOS_RTCAudio_AddNotifyAudioDevicesChanged
 */
EOS_DECLARE_FUNC(uint32_t) EOS_RTCAudio_GetAudioOutputDevicesCount(EOS_HRTCAudio Handle, const EOS_RTCAudio_GetAudioOutputDevicesCountOptions* Options);

/**
 * DEPRECATED! Use EOS_RTCAudio_QueryOutputDevicesInformation, EOS_RTCAudio_CopyOutputDeviceInformationByIndex instead.
 *
 * Fetches an audio output device's info from then given index.
 *
 * The returned value should not be cached and important information should be copied off of the result object immediately.
 *
 * @param Options structure containing the index being accessed
 * @return A pointer to the device information, or NULL on error. You should NOT keep hold of this pointer.
 * @see EOS_RTCAudio_GetAudioOutputDevicesCount
 * @see EOS_RTCAudio_AddNotifyAudioDevicesChanged
 */
EOS_DECLARE_FUNC(const EOS_RTCAudio_AudioOutputDeviceInfo *) EOS_RTCAudio_GetAudioOutputDeviceByIndex(EOS_HRTCAudio Handle, const EOS_RTCAudio_GetAudioOutputDeviceByIndexOptions* Options);

/**
 * DEPRECATED! Use EOS_RTCAudio_SetInputDeviceSettings instead.
 *
 * Use this function to set audio input settings, such as the active input device, volume, or platform AEC.
 *
 * @param Options structure containing the parameters for the operation.
 * @return EOS_Success if the setting was successful
 *         EOS_InvalidParameters if any of the parameters are incorrect
 */
EOS_DECLARE_FUNC(EOS_EResult) EOS_RTCAudio_SetAudioInputSettings(EOS_HRTCAudio Handle, const EOS_RTCAudio_SetAudioInputSettingsOptions* Options);

/**
 * DEPRECATED! Use EOS_RTCAudio_SetOutputDeviceSettings instead.
 *
 * Use this function to set audio output settings, such as the active output device or volume.
 *
 * @param Options structure containing the parameters for the operation.
 * @return EOS_Success if the setting was successful
 *         EOS_InvalidParameters if any of the parameters are incorrect
 */
EOS_DECLARE_FUNC(EOS_EResult) EOS_RTCAudio_SetAudioOutputSettings(EOS_HRTCAudio Handle, const EOS_RTCAudio_SetAudioOutputSettingsOptions* Options);
