--- sandbox/policy/freebsd/interceptors.cc.orig	2026-04-15 07:33:25 UTC
+++ sandbox/policy/freebsd/interceptors.cc
@@ -0,0 +1,1116 @@
+#include "sandbox/policy/freebsd/interceptors.h"
+
+#include <sys/param.h>
+#include <sys/types.h>
+
+#include <sys/mount.h>
+#include <sys/select.h>
+#include <sys/socket.h>
+#include <sys/stat.h>
+#include <sys/syscall.h>
+#include <sys/sysctl.h>
+#include <sys/un.h>
+
+#include <dirent.h>
+#include <dlfcn.h>
+#include <errno.h>
+#include <fcntl.h>
+#include <pthread_np.h>
+#include <pthread.h>
+#include <stdarg.h>
+#include <stdio.h>
+#include <stdlib.h>
+#include <string.h>
+#include <unistd.h>
+
+extern "C" {
+#include <casper/cap_net.h>
+#include <casper/cap_sysctl.h>
+}
+
+#include <map>
+#include <tuple>
+
+#include "base/check_op.h"
+#include "base/files/file_util.h"
+#include "base/lazy_instance.h"
+#include "base/logging.h"
+#include "base/no_destructor.h"
+#include "base/strings/stringprintf.h"
+#include "base/synchronization/lock.h"
+
+using FilePath = base::FilePath;
+
+static pthread_once_t g_init_guard = PTHREAD_ONCE_INIT;
+
+typedef int   connectat_t   (int, int, const struct sockaddr*, socklen_t);
+typedef void* dlopen_t      (const char*, int);
+typedef int   faccessat_t   (int, const char*, int, int);
+typedef int   fstatat_t     (int, const char*, struct stat*, int);
+typedef int   mkdirat_t     (int, const char*, mode_t);
+typedef int   mkostempsat_t (int, char*, int, int);
+typedef int   openat_t      (int, const char*, int, ...);
+typedef int   scandirat_t   (int, const char*, struct dirent***,
+  int (*)(const struct dirent*),
+  int (*)(const struct dirent**, const struct dirent**));
+typedef int   select_t      (int, fd_set*, fd_set*, fd_set*, struct timeval*);
+typedef int   sysctl_t(const int*, u_int, void*, size_t*, const void*, size_t);
+typedef int   sysctlbyname_t(const char*, void*, size_t*, const void*, size_t);
+typedef int   unlinkat_t    (int, const char*, int);
+
+static connectat_t*    g_libc_connectat    = nullptr;
+static dlopen_t*       g_libc_dlopen       = nullptr;
+static faccessat_t*    g_libc_faccessat    = nullptr;
+static fstatat_t*      g_libc_fstatat      = nullptr;
+static mkdirat_t*      g_libc_mkdirat      = nullptr;
+static mkostempsat_t*  g_libc_mkostempsat  = nullptr;
+static openat_t*       g_libc_openat       = nullptr;
+static scandirat_t*    g_libc_scandirat    = nullptr;
+static select_t*       g_libc_select       = nullptr;
+static sysctl_t*       g_libc_sysctl       = nullptr;
+static sysctlbyname_t* g_libc_sysctlbyname = nullptr;
+static unlinkat_t*     g_libc_unlinkat     = nullptr;
+
+static cap_channel_t* g_cap_net_chan    = nullptr;
+static cap_channel_t* g_cap_sysctl_chan = nullptr;
+
+// We don't want to use the Chromium's logger before it's initialized.
+static bool g_use_vlog = false;
+
+static int g_vlog_level = 0;
+const  int XLogLevel    = 5;
+
+#define XLOG(fmt, ...) ({ \
+  if (g_vlog_level >= XLogLevel) { \
+    base::AutoLock lock_(g_log_lock.Get()); \
+    if (g_use_vlog) { \
+      VLOG(XLogLevel) << base::StringPrintf(fmt, ## __VA_ARGS__); \
+    } else { \
+      int errno_ = errno; \
+      fprintf(stderr, "[%d:%d] " fmt "\n", \
+        getpid(), pthread_getthreadid_np(), ## __VA_ARGS__); \
+      errno = errno_; \
+    } \
+  } \
+})
+
+#define LOG_ENTRY(fmt, ...) __builtin_choose_expr( \
+  __builtin_strcmp("" fmt, "") == 0, \
+    XLOG("%s()",        __func__), \
+    XLOG("%s(" fmt ")", __func__, ## __VA_ARGS__))
+
+#define LOG_EXIT(fmt, ...) __builtin_choose_expr( \
+  __builtin_strcmp("" fmt, "") == 0, \
+    XLOG("%s -> void", __func__), \
+    XLOG("%s -> " fmt, __func__, ## __VA_ARGS__))
+
+namespace {
+
+  void* xdlsym(void* handle, const char* symbol) {
+    void* p = dlsym(handle, symbol);
+    if (p == nullptr) {
+      fprintf(stderr, "Can't find symbol: %s\n", symbol);
+      abort();
+    }
+    return p;
+  }
+
+  void Init() {
+    g_libc_connectat    = reinterpret_cast<connectat_t*>   (xdlsym(RTLD_NEXT, "connectat"));
+    g_libc_dlopen       = reinterpret_cast<dlopen_t*>      (xdlsym(RTLD_NEXT, "dlopen"));
+    g_libc_faccessat    = reinterpret_cast<faccessat_t*>   (xdlsym(RTLD_NEXT, "faccessat"));
+    g_libc_fstatat      = reinterpret_cast<fstatat_t*>     (xdlsym(RTLD_NEXT, "fstatat"));
+    g_libc_mkdirat      = reinterpret_cast<mkdirat_t*>     (xdlsym(RTLD_NEXT, "mkdirat"));
+    g_libc_mkostempsat  = reinterpret_cast<mkostempsat_t*> (xdlsym(RTLD_NEXT, "mkostempsat"));
+    g_libc_openat       = reinterpret_cast<openat_t*>      (xdlsym(RTLD_NEXT, "openat"));
+    g_libc_scandirat    = reinterpret_cast<scandirat_t*>   (xdlsym(RTLD_NEXT, "scandirat"));
+    g_libc_select       = reinterpret_cast<select_t*>      (xdlsym(RTLD_NEXT, "select"));
+    g_libc_sysctl       = reinterpret_cast<sysctl_t*>      (xdlsym(RTLD_NEXT, "sysctl"));
+    g_libc_sysctlbyname = reinterpret_cast<sysctlbyname_t*>(xdlsym(RTLD_NEXT, "sysctlbyname"));
+    g_libc_unlinkat     = reinterpret_cast<unlinkat_t*>    (xdlsym(RTLD_NEXT, "unlinkat"));
+
+    char* log_level = getenv("__INTERCEPTORS_INITIAL_VLOG_LEVEL");
+    if (log_level != nullptr) {
+      std::string str(log_level);
+      if (str.find_first_not_of("0123456789") == std::string::npos) {
+        g_vlog_level = std::stoi(str);
+      }
+    }
+  }
+
+  class LinkTracker {
+    private:
+      std::map<FilePath,FilePath> links = {};
+      std::map<FilePath,FilePath> partially_resolved = {};
+      bool dirty = false;
+      FilePath ResolveInternal(const FilePath& path) const;
+    public:
+      void AddLink(const FilePath& path, const FilePath& target);
+      FilePath Resolve(const FilePath& path);
+  };
+
+  // Note that the target is expected to be fully resolved.
+  void LinkTracker::AddLink(const FilePath& path, const FilePath& target) {
+
+    CHECK(path  .IsAbsolute());
+    CHECK(target.IsAbsolute());
+    CHECK(path != target);
+
+    if (!links.contains(path) || links[path] != target) {
+      links[path] = target;
+      dirty = true;
+    }
+  }
+
+  FilePath LinkTracker::ResolveInternal(const FilePath& path) const {
+    FilePath p;
+    for (const auto& component : path.GetComponents()) {
+      p = p.Append(component);
+      if (auto e = partially_resolved.find(p); e != partially_resolved.end()) {
+        p = e->second;
+      }
+    }
+    return p;
+  }
+
+  // This also happens to remove duplicate separators.
+  FilePath LinkTracker::Resolve(const FilePath& path) {
+
+    CHECK(path.IsAbsolute());
+
+    if (dirty) {
+      partially_resolved.clear();
+      for (const auto& [source, target] : links) {
+        auto p = ResolveInternal(source);
+        CHECK(p != target);
+        partially_resolved.insert(std::pair{p, target});
+      }
+      dirty = false;
+    }
+
+    return ResolveInternal(path);
+  }
+
+  //TODO: LazyInstance is deprecated
+  base::LazyInstance<base::Lock>::Leaky g_files_lock  = LAZY_INSTANCE_INITIALIZER;
+  base::LazyInstance<base::Lock>::Leaky g_log_lock    = LAZY_INSTANCE_INITIALIZER;
+  base::LazyInstance<base::Lock>::Leaky g_net_lock    = LAZY_INSTANCE_INITIALIZER;
+  base::LazyInstance<base::Lock>::Leaky g_sysctl_lock = LAZY_INSTANCE_INITIALIZER;
+
+  std::optional<FilePath>& preset_cwd() {
+    static base::NoDestructor<std::optional<FilePath>> cwd;
+    return *cwd;
+  }
+
+  std::map<FilePath,int>& preopened_files() {
+    static base::NoDestructor<std::map<FilePath,int>> files;
+    return *files;
+  }
+
+  std::map<FilePath,int>& preopened_dirs() {
+    static base::NoDestructor<std::map<FilePath,int>> dirs;
+    return *dirs;
+  }
+
+  LinkTracker& links() {
+    static base::NoDestructor<LinkTracker> links;
+    return *links;
+  }
+
+  void ScanForLinks(LinkTracker& tracker, const FilePath& path) {
+    FilePath abs_path =
+      base::MakeAbsoluteFilePathNoResolveSymbolicLinks(path).value();
+    FilePath p;
+    for (const auto& component : abs_path.GetComponents()) {
+      p = p.Append(component);
+      if (base::IsLink(p)) {
+        auto resolved = base::MakeAbsoluteFilePath(p);
+        VLOG(XLogLevel) << "adding symlink " << p << " -> " << resolved;
+        base::AutoLock lock(g_files_lock.Get());
+        tracker.AddLink(p, resolved);
+      }
+    }
+  }
+
+  std::optional<FilePath> NormalizePath(const FilePath& path) {
+    base::AutoLock lock(g_files_lock.Get());
+    if (path.IsAbsolute()) {
+      return links().Resolve(path);
+    } else if (auto cwd = preset_cwd()) {
+      return links().Resolve(cwd->Append(path));
+    } else {
+      return {};
+    }
+  }
+
+  std::optional<int> GetFile(const char* path) {
+
+    if (auto p = NormalizePath(FilePath(path))) {
+      base::AutoLock lock(g_files_lock.Get());
+      if (auto e = preopened_files().find(*p); e != preopened_files().end()) {
+        VLOG(XLogLevel) << "returning file fd " << e->second
+          << " for path " << path;
+        return e->second;
+      }
+    }
+
+    return {};
+  }
+
+  // Returns fd for the ancestor dir and a relative path from it.
+  std::optional<std::tuple<int,FilePath>> GetDirWithRelativePath(
+    const char* path)
+  {
+    if (auto p = NormalizePath(FilePath(path))) {
+      base::AutoLock lock(g_files_lock.Get());
+      for (const auto& [dir_path, dir_fd] :
+          preopened_dirs() | std::views::reverse)
+      {
+        if (dir_path.IsParent(*p)) {
+          auto right = p->BaseName();
+          auto left  = p->DirName();
+          while (dir_path.IsParent(left)) {
+            right = left.BaseName().Append(right);
+            left  = left.DirName();
+          }
+          auto rel_path = right;
+          VLOG(XLogLevel) << "returning dir fd " << dir_fd
+            << " with rel path " << rel_path << " for path " << path;
+          return std::tuple{dir_fd, rel_path};
+        } else if (dir_path == *p) {
+          VLOG(XLogLevel) << "returning dir fd " << dir_fd
+            << " with rel path \"\" for path " << path;
+          return std::tuple{dir_fd, FilePath()};
+        }
+      }
+    }
+
+    return {};
+  }
+
+} // namespace
+
+int sandbox_open_dir(const FilePath& path, const cap_rights_t* rights) {
+
+  CHECK_EQ(0, pthread_once(&g_init_guard, Init));
+
+  int fd = g_libc_openat(AT_FDCWD, path.value().c_str(),
+    O_PATH | O_DIRECTORY | O_CLOEXEC);
+  if (fd != -1) {
+    VLOG(XLogLevel) << "preopening dir " << path << ": fd -> " << fd;
+
+    ScanForLinks(links(), path);
+
+    if (rights != nullptr && cap_rights_limit(fd, rights) == -1) {
+      PLOG(FATAL) << "cap_rights_limit failed for "
+        << fd << " @ " << path;
+    }
+
+    auto p = base::MakeAbsoluteFilePath(path);
+    CHECK(!p.empty());
+
+    base::AutoLock lock(g_files_lock.Get());
+    if (!preopened_dirs().insert(std::pair{p, fd}).second) {
+      LOG(ERROR) << "duplicate key: " << p;
+    }
+  } else {
+    PLOG_IF(ERROR, VLOG_IS_ON(XLogLevel)) << "could not preopen dir " << path;
+  }
+
+  return fd;
+}
+
+int sandbox_open_file(const FilePath& path, const cap_rights_t* rights) {
+
+  CHECK_EQ(0, pthread_once(&g_init_guard, Init));
+
+  int fd = g_libc_openat(AT_FDCWD, path.value().c_str(), O_PATH | O_CLOEXEC);
+  if (fd != -1) {
+    VLOG(XLogLevel) << "preopening file " << path << ": fd -> " << fd;
+
+    ScanForLinks(links(), path);
+
+    if (rights != nullptr && cap_rights_limit(fd, rights) == -1) {
+      PLOG(FATAL) << "cap_rights_limit failed for "
+        << fd << " @ " << path;
+    }
+
+    auto p = base::MakeAbsoluteFilePath(path);
+    CHECK(!p.empty());
+
+    base::AutoLock lock(g_files_lock.Get());
+    if (!preopened_files().insert(std::pair{p, fd}).second) {
+      LOG(ERROR) << "duplicate key: " << p;
+    }
+  } else {
+    PLOG_IF(ERROR, VLOG_IS_ON(XLogLevel)) << "could not preopen file " << path;
+  }
+
+  return fd;
+}
+
+void sandbox_scan_for_symlinks(const FilePath& path) {
+  ScanForLinks(links(), path);
+}
+
+void sandbox_set_cap_net_channel(cap_channel_t* chan) {
+  base::AutoLock lock(g_net_lock.Get());
+  g_cap_net_chan = chan;
+}
+
+void sandbox_set_cap_sysctl_channel(cap_channel_t* chan) {
+  base::AutoLock lock(g_sysctl_lock.Get());
+  g_cap_sysctl_chan = chan;
+}
+
+void sandbox_set_cwd(const FilePath& path) {
+  base::AutoLock lock(g_files_lock.Get());
+  preset_cwd().emplace(path);
+}
+
+void sandbox_set_cwd() {
+  base::FilePath cwd;
+  if (base::GetCurrentDirectory(&cwd)) {
+    sandbox_set_cwd(cwd);
+  } else {
+    LOG(ERROR) << "unable to get current dir";
+  }
+}
+
+void sandbox_use_vlog() {
+  g_use_vlog   = true;
+  g_vlog_level = logging::GetVlogLevel(__FILE__);
+}
+
+void sandbox_with_cap_sysctl_channel(
+  std::function<void(cap_channel_t*)>& action)
+{
+  base::AutoLock lock(g_sysctl_lock.Get());
+  if (g_cap_sysctl_chan != nullptr) {
+    action(g_cap_sysctl_chan);
+  }
+}
+
+static int connectat_impl(
+  int fd, int s, const struct sockaddr* name, socklen_t namelen)
+{
+  CHECK_EQ(0, pthread_once(&g_init_guard, Init));
+
+  if (name->sa_family == PF_LOCAL) {
+
+    auto addr = reinterpret_cast<const struct sockaddr_un*>(name);
+    auto path = addr->sun_path;
+
+    if (fd == AT_FDCWD || path[0] == '/') {
+      if (auto dir_with_path = GetDirWithRelativePath(path)) {
+        auto dir_fd   = std::get<int>(*dir_with_path);
+        auto rel_path = std::get<FilePath>(*dir_with_path);
+        if (!rel_path.empty()) {
+
+          struct sockaddr_un new_name;
+          new_name.sun_len    = SUN_LEN(&new_name);
+          new_name.sun_family = PF_LOCAL;
+
+          size_t n = strlcpy(new_name.sun_path, rel_path.value().c_str(),
+            sizeof(new_name.sun_path));
+          CHECK(n < sizeof(new_name.sun_path));
+
+          return g_libc_connectat(dir_fd, s,
+            reinterpret_cast<const struct sockaddr*>(&new_name),
+            sizeof(new_name));
+        } else {
+          errno = ENOTSOCK;
+          return -1;
+        }
+      }
+    }
+  }
+
+  return g_libc_connectat(fd, s, name, namelen);
+}
+
+static int connect_impl(int s, const struct sockaddr* name, socklen_t namelen) {
+
+  if (name->sa_family != PF_LOCAL) {
+    base::AutoLock lock(g_net_lock.Get());
+    if (g_cap_net_chan != nullptr) {
+      return cap_connect(g_cap_net_chan, s, name, namelen);
+    }
+  }
+
+  return connectat_impl(AT_FDCWD, s, name, namelen);
+}
+
+static void* dlopen_impl(const char* path, int mode) {
+
+  CHECK_EQ(0, pthread_once(&g_init_guard, Init));
+
+  // This doesn't take LD_LIBMAP into consideration, but meh.
+  if (path != nullptr && path[0] == '/') {
+
+    if (auto file_fd = GetFile(path)) {
+      return fdlopen(*file_fd, mode);
+    }
+
+    if (auto dir_with_path = GetDirWithRelativePath(path)) {
+      auto dir_fd   = std::get<int>(*dir_with_path);
+      auto rel_path = std::get<FilePath>(*dir_with_path);
+      int fd = g_libc_openat(
+        dir_fd, rel_path.value().c_str(), O_RDONLY | O_CLOEXEC | O_VERIFY);
+      if (fd != -1) {
+        void* handle = fdlopen(fd, mode);
+        close(fd);
+        return handle;
+      }
+    }
+  }
+
+  return g_libc_dlopen(path, mode);
+}
+
+static int faccessat_impl(int fd, const char* path, int mode, int flag) {
+
+  CHECK_EQ(0, pthread_once(&g_init_guard, Init));
+
+  if (fd == AT_FDCWD || path[0] == '/') {
+
+    if (auto file_fd = GetFile(path)) {
+      return g_libc_faccessat(*file_fd, "", mode, flag | AT_EMPTY_PATH);
+    }
+
+    if (auto dir_with_path = GetDirWithRelativePath(path)) {
+      auto dir_fd   = std::get<int>(*dir_with_path);
+      auto rel_path = std::get<FilePath>(*dir_with_path);
+      return g_libc_faccessat(
+        dir_fd, rel_path.value().c_str(), mode, flag | AT_EMPTY_PATH);
+    }
+  }
+
+  return g_libc_faccessat(fd, path, mode, flag);
+}
+
+static int fstatat_impl(int fd, const char* path, struct stat* sb, int flag) {
+
+  CHECK_EQ(0, pthread_once(&g_init_guard, Init));
+
+  if (fd == AT_FDCWD || path[0] == '/') {
+
+    if (auto file_fd = GetFile(path)) {
+      return g_libc_fstatat(*file_fd, "", sb, flag | AT_EMPTY_PATH);
+    }
+
+    if (auto dir_with_path = GetDirWithRelativePath(path)) {
+      auto dir_fd   = std::get<int>(*dir_with_path);
+      auto rel_path = std::get<FilePath>(*dir_with_path);
+      return g_libc_fstatat(
+        dir_fd, rel_path.value().c_str(), sb, flag | AT_EMPTY_PATH);
+    }
+  }
+
+  return g_libc_fstatat(fd, path, sb, flag);
+}
+
+static int mkdirat_impl(int fd, const char* path, mode_t mode) {
+
+  CHECK_EQ(0, pthread_once(&g_init_guard, Init));
+
+  if (fd == AT_FDCWD || path[0] == '/') {
+
+    if (auto file_fd = GetFile(path)) {
+      errno = EEXIST;
+      return -1;
+    }
+
+    if (auto dir_with_path = GetDirWithRelativePath(path)) {
+      auto dir_fd   = std::get<int>(*dir_with_path);
+      auto rel_path = std::get<FilePath>(*dir_with_path);
+      if (!rel_path.empty()) {
+        return g_libc_mkdirat(dir_fd, rel_path.value().c_str(), mode);
+      } else {
+        errno = EEXIST;
+        return -1;
+      }
+    }
+  }
+
+  return g_libc_mkdirat(fd, path, mode);
+}
+
+static const char* find_mktemp_pattern(
+  const char* str, int suffixlen, int* count)
+{
+  int c = 0;
+  int i = strlen(str) - suffixlen - 1;
+  while (i >= 0 && str[i] == 'X') {
+    c++;
+    i--;
+  }
+  i++;
+
+  if (count != nullptr) {
+    *count = c;
+  }
+
+  return c > 0 ? &str[i] : nullptr;
+}
+
+static int mkostempsat_impl(int dfd, char* tpl, int suffixlen, int oflags) {
+
+  CHECK_EQ(0, pthread_once(&g_init_guard, Init));
+
+  if (dfd == AT_FDCWD || tpl[0] == '/') {
+    if (auto dir_with_path = GetDirWithRelativePath(tpl)) {
+      auto dir_fd   = std::get<int>(*dir_with_path);
+      auto rel_path = std::get<FilePath>(*dir_with_path);
+
+      char* rel_path_mut = strdup(rel_path.value().c_str());
+
+      int pattern_len;
+      auto pattern = find_mktemp_pattern(rel_path_mut, suffixlen, &pattern_len);
+
+      int err = g_libc_mkostempsat(dir_fd, rel_path_mut, suffixlen, oflags);
+      if (err != -1) {
+        auto out_pattern = const_cast<char*>(
+          find_mktemp_pattern(tpl, suffixlen, nullptr));
+        strncpy(out_pattern, pattern, pattern_len);
+      }
+
+      free(rel_path_mut);
+
+      return err;
+    }
+  }
+
+  return g_libc_mkostempsat(dfd, tpl, suffixlen, oflags);
+}
+
+static int openat_impl(int fd, const char* path, int flags, int mode) {
+
+  CHECK_EQ(0, pthread_once(&g_init_guard, Init));
+
+  if (fd == AT_FDCWD || path[0] == '/') {
+
+    if (auto file_fd = GetFile(path)) {
+      return g_libc_openat(*file_fd, "", flags | O_EMPTY_PATH, mode);
+    }
+
+    if (auto dir_with_path = GetDirWithRelativePath(path)) {
+      auto dir_fd   = std::get<int>(*dir_with_path);
+      auto rel_path = std::get<FilePath>(*dir_with_path);
+      if (flags & O_CREAT) {
+        return g_libc_openat(dir_fd, rel_path.value().c_str(), flags, mode);
+      } else {
+        return g_libc_openat(
+          dir_fd, rel_path.value().c_str(), flags | O_EMPTY_PATH, mode);
+      }
+    }
+  }
+
+  return g_libc_openat(fd, path, flags, mode);
+}
+
+static char* realpath_impl(const char* pathname, char* resolved_path) {
+
+  char buf[PATH_MAX];
+  int err;
+
+#if 0 // Not marked as CAPENABLED, doesn't support AT_EMPTY_PATH.
+  if (auto file_fd = GetFile(pathname)) {
+    err = syscall(SYS___realpathat, file_fd, "",
+      buf, sizeof(buf), AT_EMPTY_PATH);
+    goto out;
+  }
+
+  if (auto dir_with_path = GetDirWithRelativePath(pathname)) {
+    auto dir_fd   = std::get<int>(*dir_with_path);
+    auto rel_path = std::get<FilePath>(*dir_with_path);
+    err = syscall(SYS___realpathat, dir_fd, rel_path.value().c_str(),
+      buf, sizeof(buf), AT_EMPTY_PATH);
+    goto out;
+  }
+#endif
+
+  err = syscall(SYS___realpathat, AT_FDCWD, pathname, buf, sizeof(buf), 0);
+
+#if 0
+out:
+#endif
+  if (err != -1) {
+    if (resolved_path == nullptr) {
+      resolved_path = strdup(buf);
+    } else {
+      strlcpy(resolved_path, buf, PATH_MAX);
+    }
+    return resolved_path;
+  } else {
+    return nullptr;
+  }
+}
+
+static int rename_impl(const char* from, const char* to) {
+
+  if (auto from_dir_with_path = GetDirWithRelativePath(from)) {
+    if (auto to_dir_with_path = GetDirWithRelativePath(to)) {
+
+      auto from_dir_fd   = std::get<int>(*from_dir_with_path);
+      auto from_rel_path = std::get<FilePath>(*from_dir_with_path);
+      auto to_dir_fd     = std::get<int>(*to_dir_with_path);
+      auto to_rel_path   = std::get<FilePath>(*to_dir_with_path);
+
+      if (!from_rel_path.empty() && !to_rel_path.empty()) {
+        return renameat(from_dir_fd, from_rel_path.value().c_str(),
+          to_dir_fd, to_rel_path.value().c_str());
+      } else {
+        errno = ENOENT;
+        return -1;
+      }
+    }
+  }
+
+  return renameat(AT_FDCWD, from, AT_FDCWD, to);
+}
+
+static int scandirat_impl(
+  int dirfd,
+  const char* dirname,
+  struct dirent*** namelist,
+  int (*select)(const struct dirent*),
+  int (*compar)(const struct dirent**, const struct dirent**))
+{
+  CHECK_EQ(0, pthread_once(&g_init_guard, Init));
+
+  if (dirfd == AT_FDCWD || dirname[0] == '/') {
+    if (auto dir_with_path = GetDirWithRelativePath(dirname)) {
+      auto dir_fd   = std::get<int>(*dir_with_path);
+      auto rel_path = std::get<FilePath>(*dir_with_path);
+      // No AT_EMPTY_PATH apparently.
+      return g_libc_scandirat(
+        dir_fd, rel_path.value().c_str(), namelist, select, compar);
+    }
+  }
+
+  return g_libc_scandirat(dirfd, dirname, namelist, select, compar);
+}
+
+static int sysctl_impl(const int* name, u_int namelen,
+  void* oldp, size_t* oldlenp, const void* newp, size_t newlen)
+{
+  {
+    base::AutoLock lock(g_sysctl_lock.Get());
+    if (g_cap_sysctl_chan != nullptr) {
+      return cap_sysctl(
+        g_cap_sysctl_chan, name, namelen, oldp, oldlenp, newp, newlen);
+    }
+  }
+
+  CHECK_EQ(0, pthread_once(&g_init_guard, Init));
+
+  return g_libc_sysctl(name, namelen, oldp, oldlenp, newp, newlen);
+}
+
+static int sysctlbyname_impl(const char* name,
+  void* oldp, size_t* oldlenp, const void* newp, size_t newlen)
+{
+  {
+    base::AutoLock lock(g_sysctl_lock.Get());
+    if (g_cap_sysctl_chan != nullptr) {
+      return cap_sysctlbyname(
+        g_cap_sysctl_chan, name, oldp, oldlenp, newp, newlen);
+    }
+  }
+
+  CHECK_EQ(0, pthread_once(&g_init_guard, Init));
+
+  return g_libc_sysctlbyname(name, oldp, oldlenp, newp, newlen);
+}
+
+static int unlinkat_impl(int dfd, const char* path, int flag) {
+
+  CHECK_EQ(0, pthread_once(&g_init_guard, Init));
+
+  if (dfd == AT_FDCWD || path[0] == '/') {
+    if (auto dir_with_path = GetDirWithRelativePath(path)) {
+      auto dir_fd   = std::get<int>(*dir_with_path);
+      auto rel_path = std::get<FilePath>(*dir_with_path);
+      if (!rel_path.empty()) {
+        return g_libc_unlinkat(dir_fd, rel_path.value().c_str(), flag);
+      } else {
+        errno = EACCES;
+        return -1;
+      }
+    }
+  }
+
+  return g_libc_unlinkat(dfd, path, flag);
+}
+
+extern "C" {
+
+__attribute__((visibility("default"), noinline))
+int access(const char* path, int mode) {
+  LOG_ENTRY("%s, %#o", path, mode);
+  int err = faccessat_impl(AT_FDCWD, path, mode, 0);
+  LOG_EXIT("%d", err);
+  return err;
+}
+
+__attribute__((visibility("default"), noinline))
+int connect(int s, const struct sockaddr* name, socklen_t namelen) {
+  if (g_vlog_level >= XLogLevel) {
+    if (name->sa_family == PF_LOCAL) {
+      auto addr = reinterpret_cast<const struct sockaddr_un*>(name);
+      LOG_ENTRY("%d, %p [%s], %d", s, name, addr->sun_path, namelen);
+    } else {
+      LOG_ENTRY("%d, %p, %d", s, name, namelen);
+    }
+  }
+  int err = connect_impl(s, name, namelen);
+  LOG_EXIT("%d", err);
+  return err;
+}
+
+__attribute__((visibility("default"), noinline))
+int connectat(int fd, int s, const struct sockaddr* name, socklen_t namelen) {
+  LOG_ENTRY("%d, %d, %p, %d", fd, s, name, namelen);
+  int err = connectat_impl(fd, s, name, namelen);
+  LOG_EXIT("%d", err);
+  return err;
+}
+
+__attribute__((visibility("default"), noinline))
+int creat(const char* path, mode_t mode) {
+  LOG_ENTRY("%s, %#o", path, mode);
+  int err = openat_impl(AT_FDCWD, path, O_CREAT | O_TRUNC | O_WRONLY, mode);
+  LOG_EXIT("%d", err);
+  return err;
+}
+
+__attribute__((visibility("default"), noinline))
+void* dlopen(const char* path, int mode) {
+  LOG_ENTRY("%s, %#x", path, mode);
+  void* handle = dlopen_impl(path, mode);
+  LOG_EXIT("%p", handle);
+  return handle;
+}
+
+__attribute__((visibility("default"), noinline))
+int faccessat(int fd, const char* path, int mode, int flag) {
+  LOG_ENTRY("%d, %s, %#o, %#x", fd, path, mode, flag);
+  int err = faccessat_impl(fd, path, mode, flag);
+  LOG_EXIT("%d", err);
+  return err;
+}
+
+__attribute__((visibility("default"), noinline))
+FILE* fopen(const char* path, const char* mode) {
+
+  LOG_ENTRY("%s, %s", path, mode);
+
+  int flags = 0;
+  switch (mode[0]) {
+    case 'r':
+      flags = O_RDONLY;
+      break;
+    case 'w':
+      flags = O_WRONLY | O_CREAT | O_TRUNC;
+      break;
+    case 'a':
+      flags = O_WRONLY | O_CREAT | O_APPEND;
+      break;
+    default:
+      errno = EINVAL;
+      return nullptr;
+  }
+
+  for (int i = 1; mode[i] != '\0'; i++) {
+    switch (mode[i]) {
+      case '+': {
+        flags &= ~O_RDONLY;
+        flags &= ~O_WRONLY;
+        flags |= O_RDWR;
+        break;
+      }
+      case 'x': flags |= O_EXCL;    break;
+      case 'b': /* do nothing */;   break;
+      case 'e': flags |= O_CLOEXEC; break;
+      default:
+        errno = EINVAL;
+        return nullptr;
+    }
+  }
+
+  int fd = openat_impl(AT_FDCWD, path, flags, DEFFILEMODE);
+
+  FILE* f;
+  if (fd != -1) {
+    f = fdopen(fd, mode);
+  } else {
+    f = nullptr;
+  }
+
+  LOG_EXIT("%p", f);
+  return f;
+}
+
+__attribute__((visibility("default"), noinline))
+int fstatat(int fd, const char* path, struct stat* sb, int flag) {
+  LOG_ENTRY("%d, %s, %p, %#x", fd, path, sb, flag);
+  int err = fstatat_impl(fd, path, sb, flag);
+  LOG_EXIT("%d", err);
+  return err;
+}
+
+__attribute__((visibility("default"), noinline))
+int lstat(const char* path, struct stat* sb) {
+  LOG_ENTRY("%s, %p", path, sb);
+  int err = fstatat_impl(AT_FDCWD, path, sb, AT_SYMLINK_NOFOLLOW);
+  LOG_EXIT("%d", err);
+  return err;
+}
+
+__attribute__((visibility("default"), noinline))
+int mkdir(const char* path, mode_t mode) {
+  LOG_ENTRY("%s, %#o", path, mode);
+  int err = mkdirat_impl(AT_FDCWD, path, mode);
+  LOG_EXIT("%d", err);
+  return err;
+}
+
+__attribute__((visibility("default"), noinline))
+int mkdirat(int fd, const char* path, mode_t mode) {
+  LOG_ENTRY("%d, %s, %#o", fd, path, mode);
+  int err = mkdirat_impl(fd, path, mode);
+  LOG_EXIT("%d", err);
+  return err;
+}
+
+__attribute__((visibility("default"), noinline))
+int mkostempsat(int dfd, char* tpl, int suffixlen, int oflags) {
+  LOG_ENTRY("%d, %s, %d, %#x", dfd, tpl, suffixlen, oflags);
+  int err = mkostempsat_impl(dfd, tpl, suffixlen, oflags);
+  LOG_EXIT("%d", err);
+  return err;
+}
+
+__attribute__((visibility("default"), noinline))
+int mkstemp(char* tpl) {
+  LOG_ENTRY("%s", tpl);
+  int err = mkostempsat_impl(AT_FDCWD, tpl, 0, 0);
+  LOG_EXIT("%d", err);
+  return err;
+}
+
+__attribute__((visibility("default"), noinline))
+int open(const char* path, int flags, ...) {
+
+  mode_t mode = 0;
+  if (flags & O_CREAT) {
+    va_list args;
+    va_start(args, flags);
+    mode = va_arg(args, int);
+    va_end(args);
+    LOG_ENTRY("%s, %#x, %#o", path, flags, mode);
+  } else {
+    LOG_ENTRY("%s, %#x", path, flags);
+  }
+
+  int err = openat_impl(AT_FDCWD, path, flags, mode);
+  LOG_EXIT("%d", err);
+  return err;
+}
+
+__attribute__((visibility("default"), noinline))
+int openat(int fd, const char* path, int flags, ...) {
+
+  mode_t mode = 0;
+  if (flags & O_CREAT) {
+    va_list args;
+    va_start(args, flags);
+    mode = va_arg(args, int);
+    va_end(args);
+    LOG_ENTRY("%d, %s, %#x, %#o", fd, path, flags, mode);
+  } else {
+    LOG_ENTRY("%d, %s, %#x", fd, path, flags);
+  }
+
+  int err = openat_impl(fd, path, flags, mode);
+  LOG_EXIT("%d", err);
+  return err;
+}
+
+__attribute__((visibility("default"), noinline))
+DIR* opendir(const char* filename) {
+  LOG_ENTRY("%s", filename);
+  int fd = openat_impl(
+    AT_FDCWD, filename, O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0);
+  DIR* d;
+  if (fd != -1) {
+    d = fdopendir(fd);
+  } else {
+    d = nullptr;
+  }
+  LOG_EXIT("%p", d);
+  return d;
+}
+
+__attribute__((visibility("default"), noinline))
+long pathconf(const char* path, int name) {
+  LOG_ENTRY("%s, %d", path, name);
+  int fd = openat_impl(AT_FDCWD, path, O_RDONLY | O_CLOEXEC, 0);
+  int err;
+  if (fd != -1) {
+    err = fpathconf(fd, name);
+    close(fd);
+  } else {
+    err = -1;
+  }
+  LOG_EXIT("%d", err);
+  return err;
+}
+
+__attribute__((visibility("default"), noinline))
+char* realpath(const char* pathname, char* resolved_path) {
+  LOG_ENTRY("%s, %p", pathname, resolved_path);
+  char* out = realpath_impl(pathname, resolved_path);
+  LOG_EXIT("%p %s", out, out != nullptr ? out : "");
+  return out;
+}
+
+__attribute__((visibility("default"), noinline))
+int rename(const char* from, const char* to) {
+  LOG_ENTRY("%s, %s", from, to);
+  int err = rename_impl(from, to);
+  LOG_EXIT("%d", err);
+  return err;
+}
+
+__attribute__((visibility("default"), noinline))
+int rmdir(const char* path) {
+  LOG_ENTRY("%s", path);
+  int err = unlinkat_impl(AT_FDCWD, path, AT_REMOVEDIR);
+  LOG_EXIT("%d", err);
+  return err;
+}
+
+__attribute__((visibility("default"), noinline))
+int scandir(const char* dirname, struct dirent*** namelist,
+  int (*select)(const struct dirent*),
+  int (*compar)(const struct dirent**, const struct dirent**))
+{
+  LOG_ENTRY("%s, %p, %p, %p", dirname, namelist, select, compar);
+  int err = scandirat_impl(AT_FDCWD, dirname, namelist, select, compar);
+  LOG_EXIT("%d", err);
+  return err;
+}
+
+__attribute__((visibility("default"), noinline))
+int scandirat(int dirfd, const char* dirname, struct dirent*** namelist,
+  int (*select)(const struct dirent*),
+  int (*compar)(const struct dirent**, const struct dirent**))
+{
+  LOG_ENTRY("%d, %s, %p, %p, %p", dirfd, dirname, namelist, select, compar);
+  int err = scandirat_impl(dirfd, dirname, namelist, select, compar);
+  LOG_EXIT("%d", err);
+  return err;
+}
+
+// libnv doesn't bother checking for EINTR in select.
+__attribute__((visibility("default"), noinline))
+int select(int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds,
+  struct timeval* timeout)
+{
+  CHECK_EQ(0, pthread_once(&g_init_guard, Init));
+  int err;
+  do {
+    err = g_libc_select(nfds, readfds, writefds, exceptfds, timeout);
+  } while (err == -1 && errno == EINTR);
+  return err;
+}
+
+__attribute__((visibility("default"), noinline))
+int stat(const char* path, struct stat* sb) {
+  LOG_ENTRY("%s, %p", path, sb);
+  int err = fstatat_impl(AT_FDCWD, path, sb, 0);
+  LOG_EXIT("%d", err);
+  return err;
+}
+
+__attribute__((visibility("default"), noinline))
+int statfs(const char* path, struct statfs* buf) {
+  LOG_ENTRY("%s, %p", path, buf);
+  int fd = openat_impl(AT_FDCWD, path, O_RDONLY | O_CLOEXEC, 0);
+  int err;
+  if (fd != -1) {
+    err = fstatfs(fd, buf);
+    close(fd);
+  } else {
+    err = -1;
+  }
+  LOG_EXIT("%d", err);
+  return err;
+}
+
+static bool mib2name(const int* mib, u_int miblen, char* buf, size_t* buflen) {
+  CHECK_EQ(0, pthread_once(&g_init_guard, Init));
+  int q[CTL_MAXNAME + 2];
+  q[0] = CTL_SYSCTL;
+  q[1] = CTL_SYSCTL_NAME;
+  memcpy(q + 2, mib, miblen * sizeof(int));
+  return g_libc_sysctl(q, miblen + 2, buf, buflen, 0, 0) != -1;
+}
+
+__attribute__((visibility("default"), noinline))
+int sysctl(const int* name, u_int namelen, void* oldp, size_t* oldlenp,
+  const void* newp, size_t newlen)
+{
+  if (g_vlog_level >= XLogLevel) {
+    char buf[BUFSIZ];
+    size_t buflen = sizeof(buf);
+    if (mib2name(name, namelen, buf, &buflen)) {
+      LOG_ENTRY("%p [%s], %d, %p, %p, %p, %zu",
+        name, buf, namelen, oldp, oldlenp, newp, newlen);
+    } else {
+      LOG_ENTRY("%p, %d, %p, %p, %p, %zu",
+        name, namelen, oldp, oldlenp, newp, newlen);
+    }
+  }
+  int err = sysctl_impl(name, namelen, oldp, oldlenp, newp, newlen);
+  if (err == -1) {
+    LOG_EXIT("%d (errno = %d)", err, errno);
+  } else {
+    LOG_EXIT("%d", err);
+  }
+  return err;
+}
+
+__attribute__((visibility("default"), noinline))
+int sysctlbyname(const char* name, void* oldp, size_t* oldlenp,
+  const void* newp, size_t newlen)
+{
+  LOG_ENTRY("%s, %p, %p, %p, %zu", name, oldp, oldlenp, newp, newlen);
+  int err = sysctlbyname_impl(name, oldp, oldlenp, newp, newlen);
+  if (err == -1) {
+    LOG_EXIT("%d (errno = %d)", err, errno);
+  } else {
+    LOG_EXIT("%d", err);
+  }
+  return err;
+}
+
+__attribute__((visibility("default"), noinline))
+int unlink(const char* path) {
+  LOG_ENTRY("%s", path);
+  int err = unlinkat_impl(AT_FDCWD, path, 0);
+  LOG_EXIT("%d", err);
+  return err;
+}
+
+__attribute__((visibility("default"), noinline))
+int unlinkat(int dfd, const char* path, int flag) {
+  LOG_ENTRY("%d, %s, %#x", dfd, path, flag);
+  int err = unlinkat_impl(dfd, path, flag);
+  LOG_EXIT("%d", err);
+  return err;
+}
+
+} // extern "C"
