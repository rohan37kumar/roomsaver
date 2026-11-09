#pragma once

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>

// Windows System Media Transport Controls (SMTC) now-playing.
//
// Runs a background thread that polls the current media session (any player:
// Spotify, browser, etc.), and when the track changes, decodes its album-art
// thumbnail to a CPU RGBA buffer. The render thread pulls the latest artwork via
// takeArtwork() and uploads the GL texture itself (GL stays single-threaded).
// No auth, no network — everything comes from the OS.
struct NowPlaying {
    bool start();
    void stop();

    // Main thread: if new artwork was decoded since the last call, moves it out
    // (row 0 = top, 4 bytes/px) and returns true. Otherwise returns false.
    bool takeArtwork(std::vector<unsigned char>& rgba, int& w, int& h);

    // Latest metadata snapshot (thread-safe copies).
    std::string title();
    std::string artist();
    bool hasSession() const { return sessionActive.load(); }
    bool trackHasArt() const { return curTrackHasArt.load(); }

private:
    void threadMain();

    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<bool> sessionActive{false};
    std::atomic<bool> curTrackHasArt{false};

    std::mutex mtx;
    std::string curTitle, curArtist;
    std::string lastTrackId;                 // guards against redundant decodes
    std::vector<unsigned char> pendingRGBA;  // newly decoded artwork
    int pendingW = 0, pendingH = 0;
    bool pendingReady = false;
};
