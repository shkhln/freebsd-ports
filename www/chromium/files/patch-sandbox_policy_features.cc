--- sandbox/policy/features.cc.orig	2026-02-17 23:34:34 UTC
+++ sandbox/policy/features.cc
@@ -99,7 +99,7 @@ BASE_FEATURE(kSpectreVariant2Mitigation, base::FEATURE
 BASE_FEATURE(kSpectreVariant2Mitigation, base::FEATURE_ENABLED_BY_DEFAULT);
 #endif  // BUILDFLAG(IS_CHROMEOS)
 
-#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
+#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_BSD)
 // Increase the renderer sandbox memory limit. As of 2023, there are no limits
 // on macOS, and a 1TiB limit on Windows. There are reports of users bumping
 // into the limit. This increases the limit by 2x compared to the default
@@ -154,7 +154,7 @@ bool IsNetworkSandboxEnabled() {
 #endif  // BUILDFLAG(IS_WIN)
 
 bool IsNetworkSandboxEnabled() {
-#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_FUCHSIA)
+#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(IS_FREEBSD)
   return true;
 #else
 #if BUILDFLAG(IS_WIN)
