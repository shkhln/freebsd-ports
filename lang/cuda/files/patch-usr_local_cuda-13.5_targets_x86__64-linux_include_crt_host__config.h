--- usr/local/cuda-13.5/targets/x86_64-linux/include/crt/host_config.h.orig	2015-07-08 00:00:00 UTC
+++ usr/local/cuda-13.5/targets/x86_64-linux/include/crt/host_config.h
@@ -205,7 +205,11 @@
 #define _CRTIMP
 #define _ACRTIMP
 
+#if !defined(__FreeBSD__)
 #include <features.h> /* for __THROW */
+#else
+#define __THROW
+#endif
 
 #elif defined(_WIN32)
 
