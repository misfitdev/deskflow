// Phase 0 spike: can this process synthesize a multi-finger swipe that
// macOS 27 actually acts on, with no real trackpad input?
//
// Constants, struct layouts and the posting sequence are derived from
// joshuarli/iss (0BSD). Everything here is undocumented private API.
//
// Usage: swipe_spike <left|right>

#include <ApplicationServices/ApplicationServices.h>
#include <float.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>

static const CGEventField kCGSEventTypeField = 55;
static const CGEventField kCGEventGestureHIDType = 110;
static const CGEventField kCGEventGestureSwipeMask = 115;
static const CGEventField kCGEventGestureSwipeMotion = 123;
static const CGEventField kCGEventGestureSwipeProgress = 124;
static const CGEventField kCGEventGestureSwipePositionX = 125;
static const CGEventField kCGEventGestureSwipePositionY = 126;
static const CGEventField kCGEventGestureSwipeVelocityX = 129;
static const CGEventField kCGEventGestureSwipeVelocityY = 130;
static const CGEventField kCGEventGesturePhase = 132;
static const CGEventField kCGEventGesturePhaseAlias = 134;
static const CGEventField kCGEventGestureZoomDeltaY = 138;
static const CGEventField kCGEventSourceProcessAlias = 169;
static const CGEventField kCGEventRawIOHIDPayload = 4205;

enum { kCGSEventGesture = 29, kCGSEventDockControl = 30 };
enum { kIOHIDEventTypeDockSwipe = 23 };
enum { kCGGestureMotionHorizontal = 1 };
enum { kGestureBegan = 1, kGestureChanged = 2, kGestureEnded = 4 };

#pragma pack(push, 1)
typedef struct {
  uint32_t size;
  uint32_t type;
  uint32_t options;
  uint8_t depth;
  uint8_t reserved[3];
} IOHIDEventBase;

typedef struct {
  IOHIDEventBase base;
  int32_t position_x;
  int32_t position_y;
  int32_t position_z;
  uint32_t swipe_mask;
  uint16_t gesture_motion;
  uint16_t gesture_flavor;
  int32_t swipe_progress;
} IOHIDFluidTouchGestureData;

typedef struct {
  IOHIDEventBase base;
  int32_t velocity_x;
  int32_t velocity_y;
  int32_t velocity_z;
} IOHIDVelocityEventData;

typedef struct {
  uint64_t timestamp;
  uint64_t sender_id;
  uint32_t options;
  uint32_t attribute_length;
  uint32_t event_count;
} IOHIDSystemQueueElementHeader;
#pragma pack(pop)

_Static_assert(sizeof(IOHIDEventBase) == 16, "base layout");
_Static_assert(sizeof(IOHIDFluidTouchGestureData) == 40, "fluid layout");
_Static_assert(sizeof(IOHIDVelocityEventData) == 28, "velocity layout");
_Static_assert(sizeof(IOHIDSystemQueueElementHeader) == 28, "header layout");

static const uint32_t kIOHIDEventTypeVelocity = 9;
static const uint32_t kIOHIDEventTypeFluidTouchGesture = 23;
static const uint16_t kIOHIDGestureFlavorDockPrimary = 3;

static int32_t double_to_fixed1616(double value)
{
  int32_t fixed = (int32_t)(value * 65536.0);
  if (fixed == 0 && value != 0.0)
    return value > 0.0 ? 1 : -1;
  return fixed;
}

static int macos_major(void)
{
  char version[32];
  size_t size = sizeof(version);
  if (sysctlbyname("kern.osproductversion", version, &size, NULL, 0) != 0)
    return 0;
  int major = 0;
  sscanf(version, "%d", &major);
  return major;
}

static uint8_t *generate_iohid_payload(CGEventRef event, size_t *out_length)
{
  int64_t phase = CGEventGetIntegerValueField(event, kCGEventGesturePhase);
  int64_t motion = CGEventGetIntegerValueField(event, kCGEventGestureSwipeMotion);
  double progress = CGEventGetDoubleValueField(event, kCGEventGestureSwipeProgress);
  double pos_x = CGEventGetDoubleValueField(event, kCGEventGestureSwipePositionX);
  double pos_y = CGEventGetDoubleValueField(event, kCGEventGestureSwipePositionY);
  double vel_x = CGEventGetDoubleValueField(event, kCGEventGestureSwipeVelocityX);
  double vel_y = CGEventGetDoubleValueField(event, kCGEventGestureSwipeVelocityY);
  int64_t swipe_mask = CGEventGetIntegerValueField(event, kCGEventGestureSwipeMask);

  bool include_velocity = (vel_x != 0.0 || vel_y != 0.0 || phase == kGestureEnded);
  uint32_t event_count = include_velocity ? 2 : 1;
  size_t payload_length = sizeof(IOHIDSystemQueueElementHeader) + sizeof(IOHIDFluidTouchGestureData);
  if (include_velocity)
    payload_length += sizeof(IOHIDVelocityEventData);

  uint8_t *payload = malloc(payload_length);
  if (!payload)
    return NULL;
  memset(payload, 0, payload_length);

  IOHIDSystemQueueElementHeader *header = (IOHIDSystemQueueElementHeader *)payload;
  uint64_t timestamp = CGEventGetTimestamp(event);
  header->timestamp = timestamp ? timestamp : mach_absolute_time();
  header->event_count = event_count;

  IOHIDFluidTouchGestureData *fluid =
      (IOHIDFluidTouchGestureData *)(payload + sizeof(IOHIDSystemQueueElementHeader));
  fluid->base.size = sizeof(IOHIDFluidTouchGestureData);
  fluid->base.type = kIOHIDEventTypeFluidTouchGesture;
  fluid->base.options = (uint32_t)((phase & 0xFF) << 24);
  fluid->position_x = double_to_fixed1616(pos_x);
  fluid->position_y = double_to_fixed1616(pos_y);
  fluid->swipe_mask = (uint32_t)swipe_mask;
  fluid->gesture_motion = (uint16_t)motion;
  fluid->gesture_flavor = kIOHIDGestureFlavorDockPrimary;
  fluid->swipe_progress = double_to_fixed1616(progress);

  if (include_velocity) {
    IOHIDVelocityEventData *velocity =
        (IOHIDVelocityEventData *)(payload + sizeof(IOHIDSystemQueueElementHeader) + sizeof(IOHIDFluidTouchGestureData));
    velocity->base.size = sizeof(IOHIDVelocityEventData);
    velocity->base.type = kIOHIDEventTypeVelocity;
    velocity->base.depth = 1;
    velocity->velocity_x = double_to_fixed1616(vel_x);
    velocity->velocity_y = double_to_fixed1616(vel_y);
  }

  *out_length = payload_length;
  return payload;
}

static CGEventRef augment_dock_swipe_event(CGEventRef event)
{
  if (!event)
    return NULL;

  CFDataRef data = CGEventCreateData(kCFAllocatorDefault, event);
  if (!data)
    return NULL;

  const uint8_t *bytes = CFDataGetBytePtr(data);
  CFIndex length = CFDataGetLength(data);
  if (length < 4 || bytes[0] != 0 || bytes[1] != 0 || bytes[2] != 0 || bytes[3] != 2) {
    fprintf(stderr, "  ! serialized event header unexpected (len=%ld)\n", (long)length);
    CFRelease(data);
    return NULL;
  }

  size_t payload_length = 0;
  uint8_t *payload = generate_iohid_payload(event, &payload_length);
  if (!payload) {
    CFRelease(data);
    return NULL;
  }

  size_t new_length = (size_t)length + 4 + payload_length;
  uint8_t *new_bytes = malloc(new_length);
  if (!new_bytes) {
    free(payload);
    CFRelease(data);
    return NULL;
  }

  memcpy(new_bytes, bytes, length);
  new_bytes[length] = (uint8_t)(payload_length >> 8);
  new_bytes[length + 1] = (uint8_t)payload_length;
  new_bytes[length + 2] = (uint8_t)(kCGEventRawIOHIDPayload >> 8);
  new_bytes[length + 3] = (uint8_t)kCGEventRawIOHIDPayload;
  memcpy(new_bytes + length + 4, payload, payload_length);

  free(payload);
  CFRelease(data);

  CFDataRef new_data = CFDataCreate(kCFAllocatorDefault, new_bytes, (CFIndex)new_length);
  free(new_bytes);
  if (!new_data)
    return NULL;

  CGEventRef result = CGEventCreateFromData(kCFAllocatorDefault, new_data);
  CFRelease(new_data);
  return result;
}

static CGEventRef make_augmented_dock_event(int phase, bool right)
{
  CGEventRef ev = CGEventCreate(NULL);
  if (!ev)
    return NULL;

  CGEventSetIntegerValueField(ev, kCGSEventTypeField, kCGSEventDockControl);
  CGEventSetIntegerValueField(ev, kCGEventGestureHIDType, kIOHIDEventTypeDockSwipe);
  CGEventSetIntegerValueField(ev, kCGEventGesturePhase, phase);
  CGEventSetDoubleValueField(ev, kCGEventGestureSwipeProgress, right ? -1.0 : 1.0);
  CGEventSetIntegerValueField(ev, kCGEventGestureSwipeMotion, kCGGestureMotionHorizontal);
  CGEventSetIntegerValueField(ev, kCGEventGesturePhaseAlias, phase);
  CGEventSetDoubleValueField(ev, kCGEventGestureZoomDeltaY, 3.0);
  CGEventSetDoubleValueField(ev, kCGEventSourceProcessAlias, (double)mach_absolute_time());
  CGEventSetDoubleValueField(ev, kCGEventGestureSwipePositionX, 0.1);
  if (phase == kGestureEnded) {
    CGEventSetDoubleValueField(ev, kCGEventGestureSwipeVelocityX, right ? -9999.0 : 9999.0);
  }
  return ev;
}

static bool post_pair(CGEventRef dock)
{
  CGEventRef companion = CGEventCreate(NULL);
  if (!companion) {
    CFRelease(dock);
    return false;
  }
  CGEventSetIntegerValueField(companion, kCGSEventTypeField, kCGSEventGesture);
  CGEventPost(kCGSessionEventTap, dock);
  CGEventPost(kCGSessionEventTap, companion);
  CFRelease(dock);
  CFRelease(companion);
  return true;
}

int main(int argc, char **argv)
{
  bool right = true;
  if (argc > 1 && strcmp(argv[1], "left") == 0)
    right = false;

  printf("macOS major version: %d\n", macos_major());
  printf("Accessibility trusted: %s\n", AXIsProcessTrusted() ? "YES" : "NO");
  if (!AXIsProcessTrusted()) {
    fprintf(stderr, "\nFAIL: process is not Accessibility-trusted; grant it and re-run.\n");
    return 2;
  }

  printf("Posting synthetic 3-finger swipe %s in 3 seconds...\n", right ? "RIGHT" : "LEFT");
  fflush(stdout);
  sleep(3);

  const int phases[3] = {kGestureBegan, kGestureChanged, kGestureEnded};
  const char *names[3] = {"began", "changed", "ended"};

  for (int i = 0; i < 3; i++) {
    CGEventRef raw = make_augmented_dock_event(phases[i], right);
    if (!raw) {
      fprintf(stderr, "FAIL: could not create event for phase %s\n", names[i]);
      return 1;
    }
    CGEventRef augmented = augment_dock_swipe_event(raw);
    CFRelease(raw);
    if (!augmented) {
      fprintf(stderr, "FAIL: could not augment event for phase %s\n", names[i]);
      return 1;
    }
    if (!post_pair(augmented)) {
      fprintf(stderr, "FAIL: could not post phase %s\n", names[i]);
      return 1;
    }
    printf("  posted phase %s\n", names[i]);
    usleep(16000);
  }

  printf("\nPosted. Did the Space switch? (visual check)\n");
  return 0;
}
