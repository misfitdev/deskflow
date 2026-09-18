#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <stdio.h>
int main(void) {
  void *h = dlopen("/System/Library/PrivateFrameworks/SkyLight.framework/SkyLight", RTLD_LAZY);
  int (*mainCID)(void) = dlsym(h, "CGSMainConnectionID");
  CFArrayRef (*copySpaces)(int) = dlsym(h, "CGSCopyManagedDisplaySpaces");
  if (!mainCID || !copySpaces) { printf("symbols missing\n"); return 1; }
  CFArrayRef arr = copySpaces(mainCID());
  if (!arr) { printf("null result\n"); return 1; }
  long total = 0;
  for (CFIndex i = 0; i < CFArrayGetCount(arr); i++) {
    CFDictionaryRef d = CFArrayGetValueAtIndex(arr, i);
    CFArrayRef spaces = CFDictionaryGetValue(d, CFSTR("Spaces"));
    long n = spaces ? (long)CFArrayGetCount(spaces) : 0L;
    total += n;
    printf("display %ld: %ld space(s)\n", (long)i, n);
  }
  printf("TOTAL SPACES: %ld\n", total);
  CFRelease(arr);
  return 0;
}
