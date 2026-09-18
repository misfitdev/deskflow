#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <stdio.h>
// Dock exposes Mission Control state via CGSGetActiveSpace changes + window list.
// Simpler observable: count on-screen windows owned by Dock (MC overlay creates them).
int main(void) {
  extern CFArrayRef CGWindowListCopyWindowInfo(uint32_t, uint32_t);
  CFArrayRef w = CGWindowListCopyWindowInfo(1 /*onscreenonly*/, 0);
  long dockWindows = 0;
  for (CFIndex i = 0; i < CFArrayGetCount(w); i++) {
    CFDictionaryRef d = CFArrayGetValueAtIndex(w, i);
    CFStringRef owner = CFDictionaryGetValue(d, CFSTR("kCGWindowOwnerName"));
    if (owner && CFStringCompare(owner, CFSTR("Dock"), 0) == kCFCompareEqualTo) dockWindows++;
  }
  printf("%ld\n", dockWindows);
  CFRelease(w);
  return 0;
}
