#pragma once

#include <thread>
#include <atomic>

// WASAPI loopback beat detection.
//
// A background thread captures the system audio mix (loopback of the default
// render endpoint), runs a real FFT over short Hann windows, tracks a bass-onset
// envelope with an adaptive threshold, and exposes a smoothed 0..1 "beat" level.
// The render thread reads level() each frame to modulate the TV bias light.
// No device selection, no UI — just the current default output. COM/WASAPI live
// entirely on the worker thread.
struct AudioBeat {
    bool  start();
    void  stop();
    float level()  const { return beat.load(); }       // 0..1 smoothed envelope
    bool  active() const { return capturing.load(); }   // capture running
    float sensitivity = 1.0f;                           // scales detected onset

private:
    void threadMain();   // runs capture(); contains any error so it can't end the process
    void capture();

    std::thread        worker;
    std::atomic<bool>  running{false};
    std::atomic<bool>  capturing{false};
    std::atomic<float> beat{0.0f};
};
