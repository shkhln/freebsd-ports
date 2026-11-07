--- usr/local/cuda-13.5/targets/x86_64-linux/include/crt/math_functions.hpp.orig	2015-07-08 00:00:00 UTC
+++ usr/local/cuda-13.5/targets/x86_64-linux/include/crt/math_functions.hpp
@@ -366,7 +366,7 @@ __forceinline__ __host__ __device__ __cudart_builtin__
   return __isfinitef(x);
 #endif /* __CUDA_ARCH__ */
 }
-#else /* !__ANDROID__ */
+#elif !defined(__FreeBSD__)
 __forceinline__ __host__ __device__ __cudart_builtin__ NV_INT_OR_BOOL isfinite(const float x) { return __finitef(x); }
 #endif  /* __ANDROID__ */
 
@@ -381,7 +381,7 @@ __forceinline__ __host__ __device__ __cudart_builtin__
 }
 #elif defined(__ICC)
 __forceinline__ __host__ __device__ __cudart_builtin__ NV_INT_OR_BOOL isfinite(const double x) throw() { return __finite(x); }
-#else
+#elif !defined(__FreeBSD__)
 __forceinline__ __host__ __device__ __cudart_builtin__ NV_INT_OR_BOOL isfinite(const double x) { return __finite(x); }
 #endif /* __ANDROID__ */
 
@@ -394,25 +394,35 @@ __forceinline__ __host__ __device__ __cudart_builtin__
    return __isfinitel(x);
 #endif  /* __CUDA_ARCH__ */
 }
-#else /* !__ANDROID__ */
+#elif !defined(__FreeBSD__)
 __forceinline__ __host__ __device__ __cudart_builtin__ NV_INT_OR_BOOL isfinite(const long double x) { return __finitel(x); }
 #endif  /* __ANDROID__ */
 
+#if !defined(__FreeBSD__)
 __forceinline__ __host__ __device__ __cudart_builtin__ NV_INT_OR_BOOL isnan(const float x) { return __isnanf(x); }
+#endif
 #if defined(__ANDROID__)
 __forceinline__ __host__ __device__ __cudart_builtin__ NV_INT_OR_BOOL isnan(const double x) { return __isnan(x); }
 #else /* !__ANDROID__ */
+#if !defined(__FreeBSD__)
 __forceinline__ __host__ __device__ __cudart_builtin__ NV_INT_OR_BOOL isnan(const double x) throw()  { return __isnan(x); }
+#endif
 #endif /* __ANDROID__ */
+#if !defined(__FreeBSD__)
 __forceinline__ __host__ __device__ __cudart_builtin__ NV_INT_OR_BOOL isnan(const long double x) { return __isnanl(x); }
+#endif
 
+#if !defined(__FreeBSD__)
 __forceinline__ __host__ __device__ __cudart_builtin__ NV_INT_OR_BOOL isinf(const float x) { return __isinff(x); }
+#endif
 #if defined(__ANDROID__)
 __forceinline__ __host__ __device__ __cudart_builtin__ NV_INT_OR_BOOL isinf(const double x) { return __isinf(x); }
 #else /* !__ANDROID__ */
 __forceinline__ __host__ __device__ __cudart_builtin__ NV_INT_OR_BOOL isinf(const double x) throw()  { return __isinf(x); }
 #endif /* __ANDROID__ */
+#if !defined(__FreeBSD__)
 __forceinline__ __host__ __device__ __cudart_builtin__ NV_INT_OR_BOOL isinf(const long double x) { return __isinfl(x); }
+#endif
 #endif /* __QNX__ || __HORIZON__ */
 
 #endif /* ((defined _GLIBCXX_MATH_H) && _GLIBCXX_MATH_H) && (__cplusplus >= 201103L) */
