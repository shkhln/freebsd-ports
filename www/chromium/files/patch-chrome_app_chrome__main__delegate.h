--- chrome/app/chrome_main_delegate.h.orig	2026-04-15 07:53:33 UTC
+++ chrome/app/chrome_main_delegate.h
@@ -60,7 +60,7 @@ class ChromeMainDelegate : public content::ContentMain
       const std::string& process_type,
       content::MainFunctionParams main_function_params) override;
   void ProcessExiting(const std::string& process_type) override;
-#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
+#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_FREEBSD)
   void ZygoteStarting(std::vector<std::unique_ptr<content::ZygoteForkDelegate>>*
                           delegates) override;
   void ZygoteForked() override;
