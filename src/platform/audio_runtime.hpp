#pragma once

namespace squarestar::platform {

double AppSoundDurationSeconds(const char* filename);
void StopAllAppAudio();
void ShutdownAppAudio();
bool PlayAppSoundRuntime(const char* filename);

}
