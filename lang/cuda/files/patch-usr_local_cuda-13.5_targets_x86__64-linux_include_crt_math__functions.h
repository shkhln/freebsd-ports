--- usr/local/cuda-13.5/targets/x86_64-linux/include/crt/math_functions.h.orig	2015-07-08 00:00:00 UTC
+++ usr/local/cuda-13.5/targets/x86_64-linux/include/crt/math_functions.h
@@ -124,7 +124,11 @@
  */
 
 #if defined(__GNUC__) && !defined(__ANDROID__) && !defined(__QNX__) && !defined(__APPLE__) && !defined(__HORIZON__)
+#if !defined(__FreeBSD__)
 #  include <features.h> /* For GLIBC macros */
+#else
+#define __GLIBC_PREREQ(...) 0
+#endif
 #  if __GLIBC_PREREQ(2,41) && (__GLIBC_USE_ISOC23 || defined(__STDC_WANT_IEC_60559_FUNCS_EXT__))
 #    define __NV_GLIBC_PROVIDES_IEC_60559_FUNCS 1
 #  else
