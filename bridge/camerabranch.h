// SPDX-License-Identifier: AGPL-3.0-only
#ifndef CAMERABRANCH_H
#define CAMERABRANCH_H

#include <gst/gst.h>
#include <gst/video/video.h>

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

// What the 1:1 and the group call engine share about their camera branch.
namespace CameraBranch {

// How long the GUI waits for GStreamer before going on without it.
const int WaitMs = 3000;

// `work` on a thread of its own, waited for at most WaitMs: false if it didn't finish, and it may
// still be running then, so it must own what it uses and never touch the engine.
inline bool finishesInTime(std::function<void()> work)
{
    struct State {
        std::mutex lock;
        std::condition_variable finished;
        bool done = false;
    };
    auto state = std::make_shared<State>();
    std::thread([state, work] {
        work();
        std::lock_guard<std::mutex> guard(state->lock);
        state->done = true;
        state->finished.notify_all();
    }).detach();
    std::unique_lock<std::mutex> guard(state->lock);
    return state->finished.wait_for(guard, std::chrono::milliseconds(WaitMs), [&state] { return state->done; });
}

inline GstPadProbeReturn dropReconfigure(GstPad *, GstPadProbeInfo *info, gpointer)
{
    return GST_EVENT_TYPE(gst_pad_probe_info_get_event(info)) == GST_EVENT_RECONFIGURE ? GST_PAD_PROBE_DROP
                                                                                       : GST_PAD_PROBE_OK;
}

// droidcamsrc answers an upstream RECONFIGURE by stopping the camera and pushing caps downstream
// from the thread that sent the event, while its own thread can be waiting for that one in a query:
// a deadlock (issue 7). Nothing needs it to: its first negotiation is forced when its mode starts,
// and the elements after `first` convert whatever it delivers to what is asked of them.
inline void keepCameraNegotiated(GstElement *bin, const char *first)
{
    GstElement *element = gst_bin_get_by_name(GST_BIN(bin), first);
    GstPad *pad = gst_element_get_static_pad(element, "sink");
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_EVENT_UPSTREAM, &dropReconfigure, nullptr, nullptr);
    gst_object_unref(pad);
    gst_object_unref(element);
}

// The camera delivers what its sensor sees, and the sensor is mounted across the phone. The call
// pages are portrait only, so `camflip` stands the picture upright for a phone held upright, for
// our preview and the other side alike: no video orientation extension tells them (sdp.rs). True
// if that swapped its width and height. (The emulator's test pattern has no `camsrc`.)
//
// droidcamsrc knows the angle only once the camera is open: before, it says 90 for every camera,
// while a Jolla Phone's front camera is at 270 (issue 7). So this opens it, which can hang, and
// belongs on start()'s thread.
inline bool standUpright(GstElement *bin)
{
    GstElement *camera = gst_bin_get_by_name(GST_BIN(bin), "camsrc");
    if (!camera) {
        return false;
    }
    int angle = 0;
    if (gst_element_set_state(camera, GST_STATE_READY) != GST_STATE_CHANGE_FAILURE
        && g_object_class_find_property(G_OBJECT_GET_CLASS(camera), "sensor-orientation")) {
        g_object_get(camera, "sensor-orientation", &angle, nullptr);
    }
    gst_object_unref(camera);
    // Degrees clockwise; videoflip's directions go identity, 90R, 180, 90L.
    const int quarters = (angle % 360 + 360 + 45) / 90 % 4;
    GstElement *flip = gst_bin_get_by_name(GST_BIN(bin), "camflip");
    g_object_set(flip, "video-direction", static_cast<GstVideoOrientationMethod>(quarters), nullptr);
    gst_object_unref(flip);
    return quarters % 2 != 0;
}

// The camera branch into its state, standing upright. `turned` hears on that thread, before the
// first frame, whether width and height swapped; it must own what it uses, like the thread.
inline bool start(GstElement *bin, std::function<void(bool)> turned = nullptr)
{
    gst_object_ref(bin);
    return finishesInTime([bin, turned] {
        const bool swapped = standUpright(bin);
        if (turned) {
            turned(swapped);
        }
        gst_element_sync_state_with_parent(bin);
        gst_object_unref(bin);
    });
}

// The camera branch out of the pipeline, without an EOS (it would end the encoder's stream for
// good): unlinked, then stopped. Takes over the reference to `inputPad`.
inline bool remove(GstElement *pipeline, GstElement *input, GstElement *bin, GstPad *inputPad)
{
    gst_object_ref(pipeline);
    gst_object_ref(input);
    gst_object_ref(bin);
    return finishesInTime([pipeline, input, bin, inputPad] {
        GstPad *binPad = gst_element_get_static_pad(bin, "src");
        gst_pad_unlink(binPad, inputPad);
        gst_object_unref(binPad);
        gst_element_release_request_pad(input, inputPad);
        gst_object_unref(inputPad);
        gst_element_set_state(bin, GST_STATE_NULL);
        gst_bin_remove(GST_BIN(pipeline), bin);
        gst_object_unref(bin);
        gst_object_unref(input);
        gst_object_unref(pipeline);
    });
}

// A whole pipeline to NULL. False: it hangs, and the caller has to leave it alone.
inline bool stop(GstElement *pipeline)
{
    gst_object_ref(pipeline);
    return finishesInTime([pipeline] {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
    });
}

} // namespace CameraBranch

#endif // CAMERABRANCH_H
