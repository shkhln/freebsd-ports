--- content/public/app/content_main_delegate.h.orig	2026-04-15 07:54:34 UTC
+++ content/public/app/content_main_delegate.h
@@ -81,7 +81,7 @@ class CONTENT_EXPORT ContentMainDelegate {
   // path.
   virtual void ProcessExiting(const std::string& process_type) {}
 
-#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
+#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_FREEBSD)
   // Tells the embedder that the zygote process is starting, and allows it to
   // specify one or more zygote delegates if it wishes by storing them in
   // |*delegates|.
