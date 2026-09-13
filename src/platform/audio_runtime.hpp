#pragma once

namespace squarestar::platform {

double AppSoundDurationSeconds(const char* filename);
void StopAllAppAudio();
void TrimIdleAppAudioMemory();
void ShutdownAppAudio();
bool PlayAppSoundRuntime(const char* filename);

} // namespace squarestar::platform
