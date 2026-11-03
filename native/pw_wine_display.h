/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef PW_WINE_DISPLAY_H
#define PW_WINE_DISPLAY_H
/*
 * The title's side of Wine's PS5 user driver (wine/ps5/pw_wine_sink.h).
 *
 * win32u hands each frame to the present sink on one of Wine's threads. The
 * sink only copies it into a PwWineFrameBox; the title's own thread takes the
 * newest frame and shows it, so the display backend never runs on a Wine
 * thread and a slow flip never stalls Wine. The same thread reads the pad and
 * turns it into Wine input with the game profile's bindings: buttons send
 * keys or mouse buttons, and a stick can move the pointer. In xinput mode it
 * also becomes the game's XInput controller (the sink's gamepad slot).
 */
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include "../src/pw_present.h"
#include "../src/pw_game_profile.h"
#include "../src/pw_script_input.h"
#include "../wine/ps5/pw_wine_sink.h"
#include "pw_pad_ps5.h"

/* Each independent input source owns keys/buttons until it releases them. */
enum { PW_INPUT_HID, PW_INPUT_PAD, PW_INPUT_PAD2, PW_INPUT_OSK, PW_INPUT_SCRIPT, PW_INPUT_STICK,
       PW_INPUT_SOURCES = 8 };
typedef struct PwWineInputOwners { uint8_t keys[256], mouse[3]; } PwWineInputOwners;
int pw_wine_input_owned_post(PwWineInputOwners *owners, unsigned owner,
                             int (*sink)(const PwWineInput *), const PwWineInput *input);
int pw_wine_input_release_source(PwWineInputOwners *owners, unsigned source,
                                  int (*sink)(const PwWineInput *));
uint32_t pw_wine_capture_buttons(uint32_t held, uint32_t *withheld, int captured);
/* Delivery keeps a complete input tick in order while the Wine sink is full.
 * Callers must drain it before producing the next tick. Overflow is fatal to
 * the session rather than silently forgetting a release. */
enum { PW_INPUT_DELIVERY_CAPACITY = 1024 };
typedef struct PwWineInputDelivery {
    PwWineInputOwners owners;
    struct { PwWineInput event; unsigned source; } pending[PW_INPUT_DELIVERY_CAPACITY];
    size_t first, count;
    int overflow;
} PwWineInputDelivery;
int pw_wine_input_enqueue(PwWineInputDelivery *, unsigned, const PwWineInput *);
int pw_wine_input_drain(PwWineInputDelivery *, int (*)(const PwWineInput *));
int pw_wine_input_enqueue_release(PwWineInputDelivery *, unsigned);
/* One capture decision per tick, retained through OSK completion. All
 * secondary controller routes share its withholding state. */
typedef struct PwWineInputSession { int captured; uint32_t withheld[3]; } PwWineInputSession;
void pw_wine_input_session_begin(PwWineInputSession *, int menu, int dialog);
uint32_t pw_wine_input_session_buttons(PwWineInputSession *, unsigned slot, uint32_t held);
/* Button edges aggregate duplicate bindings before ownership routing. */
typedef struct PwWineButtonState { uint32_t held; } PwWineButtonState;

typedef struct PwWineFrameBox {
    pthread_mutex_t lock;
    uint8_t *pixels;              /* the caller's storage; rows packed, width * 4 bytes */
    size_t capacity;
    uint32_t width, height;
    uint64_t sequence;            /* frames put */
    uint64_t rejected;            /* too large, or invalid */
} PwWineFrameBox;

/* A box keeping frames of at most bytes (width * height * 4) in storage,
 * which the caller owns: a title's libc heap is too small for frames, so it
 * lends static memory. 0, or -1. */
int pw_wine_frame_box_init(PwWineFrameBox *box, uint8_t *storage, size_t bytes);
void pw_wine_frame_box_destroy(PwWineFrameBox *box);
/* Keep a copy of a BGRA frame, top-down, stride in bytes. 0, or -1 when it
 * is invalid or larger than the box. */
int pw_wine_frame_box_put(PwWineFrameBox *box, const void *bgra, uint32_t width,
                          uint32_t height, uint32_t stride);
/* When a frame newer than *seen was put, copy it into out (at most
 * capacity bytes), describe it in view, update *seen and return 1; 0 when
 * there is nothing new, -1 when out is too small. */
int pw_wine_frame_box_take(PwWineFrameBox *box, uint64_t *seen, uint8_t *out,
                           size_t capacity, PwPresentView *view);

/* Translate a batch with its final held mask and accumulated edges. Bindings
 * that share a key/button are aggregated; a complete tap between samples
 * still emits down/up. A full output buffer leaves the state unchanged. */
size_t pw_wine_game_inputs(PwWineButtonState *state, const PwGameInput *input, uint32_t held, uint32_t pressed, uint32_t released,
                           PwWineInput *out, size_t max);

/* Digital key emulation for one analog stick. Direction bits are retained
 * across frames so key-down is sent only on a threshold crossing, and every
 * held direction is released when the controller disconnects.
 *
 * For titles that read the keyboard for movement and cannot be configured
 * to use a stick axis: one stick drives the four direction keys. */
typedef struct PwWineKeyboardStick { uint8_t held; } PwWineKeyboardStick;
size_t pw_wine_keyboard_stick_step(const PwGameInput *input, uint8_t stick_x, uint8_t stick_y,
                                  int connected, PwWineKeyboardStick *state,
                                  PwWineInput *out, size_t max);

/* The DualSense as an XInput gamepad: its held buttons (Cross A, Circle B,
 * Square X, Triangle Y, L1/R1 shoulders, L3/R3 thumbs, Options Start,
 * Create Back, a touchpad click Back on its left half and Guide on its
 * right, by where it began: touch_left), the analog triggers, and the sticks
 * scaled to -32768..32767 with y up. 1 and the state in out while the pad
 * is connected, 1 and a neutral state while the system has it
 * (intercepted); 0 and a neutral state otherwise. The sink numbers the
 * packets. */
int pw_wine_game_pad(const PwPadPs5 *pad, PwWinePad *out);

/* A script build's scripted controller (src/pw_script_input.h) added to
 * the real one in pad, which pw_wine_game_pad filled (connected says
 * whether it is): the scripted buttons join the held ones, and each stick
 * the script holds off centre takes the real stick's place. With scripted
 * set (the macro drives a controller), controller 0 stays connected
 * without a DualSense. Returns whether it is connected; a neutral pad
 * when not. */
int pw_wine_script_pad(PwWinePad *pad, int connected, const PwScriptPad *script, int scripted);

/* A stick's pointer motion not yet sent, in 1/65536 pixels. Wine keeps
 * the pointer's position: the title sends only motion, as a mouse does
 * (patch 0670), so a game can move or clip the pointer itself and motion
 * against an edge still reaches it. */
typedef struct PwWinePointer {
    int64_t x, y;
} PwWinePointer;

enum { PW_WINE_POINTER_DEADZONE = 20 };   /* of 128, stick noise at rest */

/* Move by a stick position (0..255 each axis, 0x80 centred) held for
 * elapsed_us, at up to speed pixels per second at full tilt, on a squared
 * curve past the dead zone. 1 and a MOUSE_MOVE of the whole pixels moved
 * in out when there are any (the fraction is kept for the next step),
 * else 0. */
int pw_wine_pointer_step(PwWinePointer *pointer, uint8_t stick_x, uint8_t stick_y,
                         uint32_t speed, uint32_t elapsed_us, PwWineInput *out);
#endif
