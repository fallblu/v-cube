#pragma once

namespace vcube {

// The inspection calls as sounds: one short beep at 8 s, two at 12 s. Uses
// Qt Multimedia when the build has it, else the system beep.
void playInspectionCall(int seconds);
// Loads the sound ahead of the first call.
void prepareSounds();

} // namespace vcube
