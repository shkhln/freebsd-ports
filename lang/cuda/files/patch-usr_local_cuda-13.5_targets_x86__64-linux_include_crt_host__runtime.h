--- usr/local/cuda-13.5/targets/x86_64-linux/include/crt/host_runtime.h.orig	2015-07-08 00:00:00 UTC
+++ usr/local/cuda-13.5/targets/x86_64-linux/include/crt/host_runtime.h
@@ -256,7 +256,7 @@ extern void CUDARTAPI __cudaRegisterFunction(
         int     *wSize
 );
 
-#if defined(__APPLE__)
+#if defined(__APPLE__) || defined(__FreeBSD__)
 extern "C" int atexit(void (*)(void));
 
 #elif  defined(__GNUC__) && !defined(__ANDROID__) && !defined(__HORIZON__)
