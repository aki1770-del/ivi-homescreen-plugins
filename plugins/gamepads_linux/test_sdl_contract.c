// Exercises exactly the SDL3 calls plugins/gamepads_linux makes, against a
// virtual pad, so the plugin's contract can be checked without hardware.
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

int main(void) {
  if (!SDL_Init(SDL_INIT_GAMEPAD)) { printf("SDL_Init: %s\n", SDL_GetError()); return 77; }

  SDL_VirtualJoystickDesc desc;
  SDL_INIT_INTERFACE(&desc);
  desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
  desc.vendor_id = 0x045e;   // Microsoft
  desc.product_id = 0x028e;  // X360 pad
  desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
  desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
  desc.name = "Test Virtual Pad";
  for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++) desc.button_mask |= (1u << b);
  for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; a++) desc.axis_mask |= (1u << a);

  SDL_JoystickID vid = SDL_AttachVirtualJoystick(&desc);
  if (vid == 0) { printf("SDL_AttachVirtualJoystick: %s\n", SDL_GetError()); return 77; }

  // 1. The plugin enumerates with SDL_GetGamepads + SDL_GetGamepadName.
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  CHECK(ids != NULL && count >= 1, "SDL_GetGamepads found %d pads, expected >= 1", count);
  int found = 0;
  for (int i = 0; i < count; i++) if (ids[i] == vid) found = 1;
  CHECK(found, "the virtual pad (id %u) is not in the enumeration", (unsigned)vid);
  SDL_free(ids);

  SDL_Gamepad* pad = SDL_OpenGamepad(vid);
  CHECK(pad != NULL, "SDL_OpenGamepad: %s", SDL_GetError());
  if (!pad) return 1;
  const char* name = SDL_GetGamepadName(pad);
  CHECK(name && strcmp(name, "Test Virtual Pad") == 0, "name was '%s'", name ? name : "(null)");

  // 2. Buttons read back through SDL_UpdateGamepads + SDL_GetGamepadButton,
  //    which is the plugin's whole button path.
  for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++) {
    SDL_SetJoystickVirtualButton(SDL_GetGamepadJoystick(pad), b, true);
    SDL_UpdateGamepads();
    CHECK(SDL_GetGamepadButton(pad, (SDL_GamepadButton)b), "button %d did not read back as pressed", b);
    SDL_SetJoystickVirtualButton(SDL_GetGamepadJoystick(pad), b, false);
    SDL_UpdateGamepads();
    CHECK(!SDL_GetGamepadButton(pad, (SDL_GamepadButton)b), "button %d did not read back as released", b);
  }

  // 3. Axes, and the range the plugin forwards verbatim. Sticks come back
  //    signed, but SDL rescales the two TRIGGER axes to 0..32767 -- a gamepad
  //    trigger is unipolar even though the underlying joystick axis is not.
  //    The port must divide both by 32767 and will land on -1..1 for sticks
  //    and 0..1 for triggers, which is also what Godot's JoyAxis reports.
  const Sint16 probes[] = {-32768, -16384, 0, 16384, 32767};
  for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; a++) {
    const bool trigger = (a == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ||
                          a == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    for (size_t p = 0; p < sizeof(probes)/sizeof(probes[0]); p++) {
      SDL_SetJoystickVirtualAxis(SDL_GetGamepadJoystick(pad), a, probes[p]);
      SDL_UpdateGamepads();
      Sint16 got = SDL_GetGamepadAxis(pad, (SDL_GamepadAxis)a);
      // Rescale of [-32768,32767] onto [0,32767], rounding down.
      Sint16 want = trigger
          ? (Sint16)(((int)probes[p] + 32768) * 32767 / 65535)
          : probes[p];
      CHECK(got == want, "axis %d set to %d read back as %d, expected %d",
            a, probes[p], got, want);
    }
    SDL_SetJoystickVirtualAxis(SDL_GetGamepadJoystick(pad), a, trigger ? -32768 : 0);
  }
  SDL_UpdateGamepads();
  CHECK(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) == 0,
        "a released trigger does not rest at 0");
  CHECK(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) == 0,
        "a centred stick does not rest at 0");

  // 4. The enum values the port will map to Godot's JoyButton/JoyAxis.
  CHECK(SDL_GAMEPAD_BUTTON_SOUTH == 0 && SDL_GAMEPAD_BUTTON_EAST == 1 &&
        SDL_GAMEPAD_BUTTON_WEST == 2 && SDL_GAMEPAD_BUTTON_NORTH == 3,
        "face button enum values moved");
  CHECK(SDL_GAMEPAD_BUTTON_DPAD_UP == 11 && SDL_GAMEPAD_BUTTON_DPAD_RIGHT == 14,
        "d-pad enum values moved");
  CHECK(SDL_GAMEPAD_AXIS_LEFTX == 0 && SDL_GAMEPAD_AXIS_LEFTY == 1 &&
        SDL_GAMEPAD_AXIS_RIGHTX == 2 && SDL_GAMEPAD_AXIS_RIGHTY == 3 &&
        SDL_GAMEPAD_AXIS_LEFT_TRIGGER == 4 && SDL_GAMEPAD_AXIS_RIGHT_TRIGGER == 5,
        "axis enum values moved");

  // 5. Hot-unplug: the plugin's rescan drops pads that leave the enumeration.
  SDL_CloseGamepad(pad);
  SDL_DetachVirtualJoystick(vid);
  SDL_UpdateGamepads();
  ids = SDL_GetGamepads(&count);
  found = 0;
  for (int i = 0; i < count; i++) if (ids[i] == vid) found = 1;
  CHECK(!found, "the detached pad is still enumerated");
  SDL_free(ids);

  SDL_Quit();
  printf(failures ? "%d check(s) FAILED\n" : "all checks passed (%d failures)\n", failures);
  return failures ? 1 : 0;
}
