// Copyright 2019 The Bitcoin SV Developers
// Based on vmtouch - https://github.com/hoytech/vmtouch
//
/***********************************************************************
vmtouch - the Virtual Memory Toucher
Portable file system cache diagnostics and control
by Doug Hoyte (doug@hcsw.org)
************************************************************************

Copyright (c) 2009-2017 Doug Hoyte and contributors. All rights reserved.
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the author may not be used to endorse or promote products
   derived from this software without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
***********************************************************************/

#ifndef WIN32

#include "vmtouch.h"

#include <unistd.h>
#include <stdio.h>

#include <exception>

#include <sys/time.h>
#include <sys/resource.h>
#include <sys/mman.h>

#include <signal.h>

#include "logging.h"

#if defined(__linux__)
    #include <sys/ioctl.h>
    #include <sys/mount.h>
#endif

#include <libgen.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>

#include "fs.h"

#include <stdarg.h>

#include <boost/noncopyable.hpp>
#include <functional>
#include <boost/thread/thread.hpp> // boost::thread::interrupt

// Takes ownership of file descriptor and closes it in destructor.
class CAutoCloseFile : public boost::noncopyable {
  int fd =-1;
public:

  CAutoCloseFile(int fd) : fd(fd)
  {
  }

  ~CAutoCloseFile()
  {
    if (fd != -1)
    {
      close(fd);
    }
  }
};

// Takes ownership of result of opendir() and closes it in destructor.
class CAutoCloseDir : public boost::noncopyable {
  DIR * dir;
  std::function<void(const std::string& message)> warning;
public:

  CAutoCloseDir(DIR* dir, std::function<void(const std::string& message)> warning) : dir(dir), warning(warning)
  {
  }

  ~CAutoCloseDir()
  {
    if (closedir(dir))
    {
      char* msg = strerror(errno);
      warning(std::string("unable to closedir. ") +  std::string(msg));
    }
  }
};

// Takes ownership of memory mapped region and unmaps it in destructor unless Release() is called
class CAutoMunmap : public boost::noncopyable {
  void* mem = nullptr;
  uint64_t len_of_range = 0;
  std::function<void(const std::string& message)> warning;
public:
  CAutoMunmap(void *mem,uint64_t len_of_range, std::function<void(const std::string& message)> warning)
   :  mem(mem), len_of_range(len_of_range), warning(warning)
  {  }

  void Release()
  {
    mem = nullptr;
  }

  ~CAutoMunmap()
  {
    if (mem && munmap(mem, len_of_range) != 0) {
      char* msg = strerror(errno);
      warning(std::string("unable to munmap file. ") +  std::string(msg));
    }
  }

};


VMTouch::VMTouch() : pagesize(sysconf(_SC_PAGESIZE))
{
    curr_crawl_depth = 0;
}

VMTouch::~VMTouch()
{   }

static void fatal(const char *fmt, ...) {
    va_list ap;
    char buf[4096];

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    throw std::runtime_error(buf);
}
void VMTouch::warning(const char *fmt, ...) {
    va_list ap;
    char buf[4096];

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    warnings.emplace_back(buf);
    
}
int VMTouch::aligned_p(void* p)
{
    return 0 == ((long)p & (pagesize - 1));
}

int64_t VMTouch::bytes2pages(uint64_t bytes)
{
    return (bytes + pagesize - 1) / pagesize;
}

int VMTouch::is_mincore_page_resident(char p)
{
    return p & 0x1;
}

void VMTouch::increment_nofile_rlimit()
{
    struct rlimit r;

    if (getrlimit(RLIMIT_NOFILE, &r))
      fatal("increment_nofile_rlimit: getrlimit (%s)", strerror(errno));

    r.rlim_cur = r.rlim_max + 1;
    r.rlim_max = r.rlim_max + 1;

    if (setrlimit(RLIMIT_NOFILE, &r)) {
        if (errno == EPERM) {
            if (getuid() == 0 || geteuid() == 0) { 
              fatal("system open file limit reached");
            } 
            fatal("open file limit reached and unable to increase limit. retry as root");
        }
        fatal("increment_nofile_rlimit: setrlimit (%s)", strerror(errno));

    }
}
void VMTouch::vmtouch_file(const std::string& strPath)
{
    int fd = -1;
    void* mem = nullptr;
    struct stat sb;
    int64_t len_of_file=0;
    uint64_t len_of_range=0;
    int64_t pages_in_range;
    int i;
    int res;
    int open_flags;
    int max_len = 0;
    int offset = 0;

    retry_open:

    open_flags = O_RDONLY;

  #if defined(O_NOATIME)
    open_flags |= O_NOATIME;
  #endif

    fd = open(strPath.c_str(), open_flags, 0);

  #if defined(O_NOATIME)
    if (fd == -1 && errno == EPERM) {
      open_flags &= ~O_NOATIME;
      fd = open(strPath.c_str(), open_flags, 0);
    }
  #endif

    if (fd == -1) {
      if (errno == ENFILE || errno == EMFILE) {
        increment_nofile_rlimit();
        goto retry_open;
      }

      warning("unable to open %s (%s), skipping", strPath.c_str(), strerror(errno));
      return;
    }

    // File was sucesfully open. Make sure that ti is closed.
    CAutoCloseFile file(fd);
    res = fstat(fd, &sb);

    if (res) {
      warning("unable to fstat %s (%s), skipping", strPath.c_str(), strerror(errno));
      return;
    }

    if (S_ISBLK(sb.st_mode)) {
  #if defined(__linux__)
      if (ioctl(fd, BLKGETSIZE64, &len_of_file)) {
        warning("unable to ioctl %s (%s), skipping", strPath.c_str(), strerror(errno));
        return;
      }
  #else
        fatal("discovering size of block devices not (yet?) supported on this platform");
  #endif
    } else {
      len_of_file = sb.st_size;
    }

    if (len_of_file == 0) {
        return;
    }

    if (len_of_file > o_max_file_size) {
      warning("file %s too large, skipping", strPath.c_str());
      return;
    }

    if (max_len > 0 && (offset + max_len) < len_of_file) {
      len_of_range = max_len;
    } else if (offset >= len_of_file) {
      warning("file %s smaller than offset, skipping", strPath.c_str());
      return;
    } else {
      len_of_range = len_of_file - offset;
    }

    auto warning_callback = [this, &strPath](const std::string& message) {
        this->warning("%s %s", message.c_str(), strPath.c_str());
    };

    mem = mmap(nullptr, len_of_range, PROT_READ, MAP_SHARED, fd, offset);

    if (mem == MAP_FAILED) {
        warning("unable to mmap file %s (%s), skipping", strPath.c_str(), strerror(errno));    
        return;
    }

    // Memory was mapped. Unmap it automatically unless lock was sucesful:
    CAutoMunmap mapped_memory(mem, len_of_range, warning_callback);

    if (aligned_p(mem) == 0) {
        fatal("mmap(%s) wasn't page aligned", strPath.c_str());
    }

    pages_in_range = bytes2pages(len_of_range);

    total_pages += pages_in_range;

    if (o_evict) {
      //if (o_verbose) printf("Evicting %s\n", path);

  #if defined(__linux__) || defined(__hpux)
      if (posix_fadvise(fd, offset, len_of_range, POSIX_FADV_DONTNEED)) {
          warning("unable to posix_fadvise file %s (%s)", strPath.c_str(), strerror(errno));
      }
      
  #elif defined(__FreeBSD__) || defined(__sun__) || defined(__APPLE__)
      if (msync(mem, len_of_range, MS_INVALIDATE)) {
          warning("unable to msync invalidate file %s (%s)", strPath.c_str(), strerror(errno));
      }
        
  #else
      fatal("cache eviction not (yet?) supported on this platform");      
  #endif
    } else {
      //double last_chart_print_time=0.0, temp_time;
      #ifdef __APPLE__
          std::vector<char> mincore_array(pages_in_range);
      #else
          std::vector<unsigned char> mincore_array(pages_in_range);
      #endif
      
      // 3rd arg to mincore is char* on BSD and unsigned char* on linux      
      if (mincore(mem, len_of_range, mincore_array.data()) != 0) {
          fatal("mincore %s (%s)", strPath.c_str(), strerror(errno));
      }
      for (i=0; i<pages_in_range; i++) {
        if (is_mincore_page_resident(mincore_array[i])) {
          total_pages_in_core++;
        }
      }

      /*if (o_verbose) {
        printf("%s\n", path);
        last_chart_print_time = gettimeofday_as_double();
        print_page_residency_chart(stdout, mincore_array, pages_in_range);
      }*/

      if (o_touch) {
        for (i=0; i<pages_in_range; i++) {
          junk_counter += ((char*)mem)[i*pagesize];
          mincore_array[i] = 1;

          /*if (o_verbose) {
            temp_time = gettimeofday_as_double();

            if (temp_time > (last_chart_print_time+CHART_UPDATE_INTERVAL)) {
              last_chart_print_time = temp_time;
              print_page_residency_chart(stdout, mincore_array, pages_in_range);
            }
          }*/
        }
      }

      /*if (o_verbose) {
        print_page_residency_chart(stdout, mincore_array, pages_in_range);
        printf("\n");
      }*/

    }

    if (o_lock) {
      if (mlock(mem, len_of_range) != 0) {            
          fatal("mlock: %s (%s)", strPath.c_str(), strerror(errno));
      }
      // Release ownership, to keep apges locked
      mapped_memory.Release();
    }

}

void VMTouch::add_object(const struct stat& st)
{
    seen_inodes.emplace( dev_and_inode { st.st_dev, st.st_ino});
}

bool VMTouch::is_ignored(const std::string& path)
{
    if (ignoreList.empty()) {
        return false;
    }

    // basename() modifies input buffer so make a copy.
    // It might return pointer into path_copy or to some static allocated moemory
    std::string path_copy(path.c_str());

    char *filename = basename(path_copy.data());

    for(auto& ignoreItem : ignoreList) {
        if (fnmatch(ignoreItem.c_str(), filename, 0) == 0) {
            return true;
        }
    }

    return false;
}

bool VMTouch::is_filename_filtered(const std::string& path)
{
    if (filenameFilterList.empty()) {
        return true;
    } 

    // basename() modifies input buffer so make a copy.
    // It might return pointer into path_copy or to some static allocated moemory
    std::string path_copy(path.c_str());

    char *filename = basename(path_copy.data());

    for(auto& filterItem : filenameFilterList) {
        if (fnmatch(filterItem.c_str(), filename, 0) == 0) {
            return true;
        }
    }
    return false;
}



// returns true if st HAS been added before
bool VMTouch::find_object(const struct stat& st)
{
    struct dev_and_inode obj {st.st_dev, st.st_ino};
    return seen_inodes.find(obj) != seen_inodes.end();     
}

void VMTouch::vmtouch_crawl(std::string strPath)
{
    struct stat sb;
    DIR *dirp;
    struct dirent *de;
    std::string npath;
    int res;
    int tp_path_len = strPath.length();
    int i;

    if (strPath.empty())
    {
        throw std::invalid_argument("Invalid argument - path must not be empty");
    }

    if (strPath[tp_path_len-1] == '/' && tp_path_len > 1)
        strPath[tp_path_len-1] = '\0'; // prevent ugly double slashes when printing path names

    if (is_ignored(strPath)) {
      return;
    }

    res = o_followsymlinks ? stat(strPath.c_str(), &sb) : lstat(strPath.c_str(), &sb);   

    if (res) {
      warning("unable to stat %s (%s)", strPath.c_str(), strerror(errno));
      return;
    } else {
      if (S_ISLNK(sb.st_mode)) {
        warning("not following symbolic link %s", strPath.c_str());        
        return;
      }

      if (o_singlefilesystem) {
        if (!orig_device_inited) {
          orig_device = sb.st_dev;
          orig_device_inited = true;
        } else {
          if (sb.st_dev != orig_device) {
            warning("not recursing into separate filesystem %s", strPath.c_str());
            return;
          }
        }
      }

      if (!o_ignorehardlinkeduplictes && sb.st_nlink > 1) {
        /*
         * For files with more than one link to it, ignore it if we already know
         * inode.  Without this check files copied as hardlinks (cp -al) are
         * counted twice (which may lead to a cache usage of more than 100% of
         * RAM).
         */
        if (find_object(sb)) {
          // we already saw the device and inode referenced by this file
          return;
        } else {
          add_object(sb);
        }
      }

      if (S_ISDIR(sb.st_mode)) {
        for (i=0; i<curr_crawl_depth; i++) {
          if (crawl_inodes[i] == sb.st_ino) {
            warning("symbolic link loop detected: %s", strPath.c_str());
            return;
          }
        }

        if (curr_crawl_depth == MAX_CRAWL_DEPTH) {
          fatal("maximum directory crawl depth reached: %s", strPath.c_str());          
        }

        total_dirs++;

        crawl_inodes[curr_crawl_depth] = sb.st_ino;

        retry_opendir:

        dirp = opendir(strPath.c_str()); 

        if (dirp == NULL) {
          if (errno == ENFILE || errno == EMFILE) {
            increment_nofile_rlimit();
            goto retry_opendir;
          }

          warning("unable to opendir %s (%s), skipping", strPath.c_str(), strerror(errno));
          return;
        }

        auto warning_callback = [this, &strPath](const std::string& message) {
            this->warning("%s %s", message.c_str(), strPath.c_str());
        };        
        
        CAutoCloseDir dir(dirp, warning_callback);
        while((de = readdir(dirp)) != NULL) {

          boost::this_thread::interruption_point();

          if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;

          npath = strPath + "/" + de->d_name;

          curr_crawl_depth++;
          vmtouch_crawl(npath);
          curr_crawl_depth--;
        }

      } else if (S_ISLNK(sb.st_mode)) {        
        warning("not following symbolic link %s", strPath.c_str());
        return;
      } else if (S_ISREG(sb.st_mode) || S_ISBLK(sb.st_mode)) {
        if (is_filename_filtered(strPath.c_str())) {
          total_files++;
          vmtouch_file(strPath);
        }
      } else {
        warning("skipping non-regular file: %s\n", strPath.c_str());
      }
    }
}

void VMTouch::vmtouch_touch(const std::string& strPath)
{
    o_touch = true;
    o_evict = false;
    vmtouch_crawl(strPath);
}

double VMTouch::vmtouch_check(const std::string& strPath)
{
    o_touch = false;
    o_evict = false;
    vmtouch_crawl(strPath);
    return getPagesInCorePercent();
}

double VMTouch::getPagesInCorePercent()
{
  if (total_pages == 0)
  {
    // Avoid division by zero. If there are no pages to load, we assume, that everything is loaded
    return 100;
  } 

  return 100.0*total_pages_in_core/total_pages;
}

#endif

