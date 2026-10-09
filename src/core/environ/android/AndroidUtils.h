#pragma once
#include <jni.h>
#include <string>

std::string TVPGetDeviceID();

// Called on the rendering thread after the director and scene are initialized.
void Android_InitializeEventQueue();
