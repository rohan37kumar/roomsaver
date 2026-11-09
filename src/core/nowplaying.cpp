#include "core/nowplaying.h"
#include "core/log.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

#include <stb_image.h>

#include <chrono>

using namespace winrt;
using namespace winrt::Windows::Media::Control;
using namespace winrt::Windows::Storage::Streams;

bool NowPlaying::start() {
    if (running.load()) return true;
    running.store(true);
    worker = std::thread(&NowPlaying::threadMain, this);
    return true;
}

void NowPlaying::stop() {
    running.store(false);
    if (worker.joinable()) worker.join();
}

std::string NowPlaying::title()  { std::lock_guard<std::mutex> l(mtx); return curTitle;  }
std::string NowPlaying::artist() { std::lock_guard<std::mutex> l(mtx); return curArtist; }

bool NowPlaying::takeArtwork(std::vector<unsigned char>& rgba, int& w, int& h) {
    std::lock_guard<std::mutex> l(mtx);
    if (!pendingReady) return false;
    rgba = std::move(pendingRGBA);
    w = pendingW; h = pendingH;
    pendingReady = false;
    pendingRGBA.clear();
    return true;
}

void NowPlaying::threadMain() {
    init_apartment(apartment_type::multi_threaded);

    GlobalSystemMediaTransportControlsSessionManager mgr{ nullptr };
    try {
        mgr = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
    } catch (hresult_error const& e) {
        LOGI("NowPlaying: session manager request failed (0x%08X)", (unsigned)e.code());
        uninit_apartment();
        return;
    }
    LOGI("NowPlaying: SMTC session manager ready");

    // Decode the current track's thumbnail into the pending buffer. Returns true
    // only on success (art actually decoded & stored).
    auto tryFetchArt = [&](GlobalSystemMediaTransportControlsSessionMediaProperties const& props) -> bool {
        auto thumbRef = props.Thumbnail();
        if (!thumbRef) return false;
        try {
            IRandomAccessStreamWithContentType stream = thumbRef.OpenReadAsync().get();
            uint32_t size = (uint32_t)stream.Size();
            if (size == 0) return false;
            Buffer buffer(size);
            IBuffer filled = stream.ReadAsync(buffer, size, InputStreamOptions::None).get();
            DataReader reader = DataReader::FromBuffer(filled);
            std::vector<uint8_t> bytes(filled.Length());
            reader.ReadBytes(bytes);

            int w = 0, h = 0, comp = 0;
            unsigned char* px = stbi_load_from_memory(
                bytes.data(), (int)bytes.size(), &w, &h, &comp, 4);
            if (!px) { LOGI("NowPlaying: art decode failed"); return false; }
            {
                std::lock_guard<std::mutex> l(mtx);
                pendingRGBA.assign(px, px + (size_t)w * h * 4);
                pendingW = w; pendingH = h; pendingReady = true;
            }
            stbi_image_free(px);
            LOGI("NowPlaying: decoded album art %dx%d", w, h);
            return true;
        } catch (hresult_error const& e) {
            LOGI("NowPlaying: thumbnail read failed (0x%08X)", (unsigned)e.code());
            return false;
        }
    };

    // Album art is often not ready on the first poll after a track change / app
    // launch, so we retry across polls until it lands (then poll lazily). Without
    // this the TV stays on the idle screen until you skip tracks.
    bool artDelivered = false;
    int  artTries = 0;
    const int kMaxArtTries = 25;   // ~7.5s of fast retries, then accept "no art"

    while (running.load()) {
        bool waitingForArt = false;
        try {
            GlobalSystemMediaTransportControlsSession session = mgr.GetCurrentSession();
            if (!session) {
                sessionActive.store(false);
                curTrackHasArt.store(false);
                lastTrackId.clear();            // reopening a player re-fetches art
                artDelivered = false; artTries = 0;
            } else {
                sessionActive.store(true);
                auto props = session.TryGetMediaPropertiesAsync().get();

                std::string title  = to_string(props.Title());
                std::string artist = to_string(props.Artist());
                std::string trackId = title + "\x1f" + artist;

                {
                    std::lock_guard<std::mutex> l(mtx);
                    curTitle = title; curArtist = artist;
                }

                if (trackId != lastTrackId) {
                    lastTrackId = trackId;
                    artDelivered = false; artTries = 0;
                    LOGI("NowPlaying: track -> '%s' / '%s'", title.c_str(), artist.c_str());
                }

                if (!artDelivered && artTries < kMaxArtTries) {
                    ++artTries;
                    if (tryFetchArt(props)) {
                        artDelivered = true;
                        curTrackHasArt.store(true);
                    } else {
                        waitingForArt = true;
                        if (artTries >= kMaxArtTries) curTrackHasArt.store(false);
                    }
                }
            }
        } catch (hresult_error const& e) {
            LOGI("NowPlaying: poll error (0x%08X)", (unsigned)e.code());
        }

        // Poll fast while chasing the artwork so it appears promptly on open;
        // otherwise idle at ~1s. Interruptible so stop() is responsive.
        const int pollMs = waitingForArt ? 300 : 1000;
        for (int i = 0; i < pollMs / 50 && running.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    uninit_apartment();
}
