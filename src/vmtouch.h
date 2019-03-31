// Copyright 2019 The Bitcoin SV Developers

#ifndef VMTOUCH_H
#define VMTOUCH_H

#ifndef WIN32

#include <stdint.h>
#include <assert.h>
#include <fnmatch.h>
#include <limits.h>

#include <string>
#include <set>
#include <tuple>
#include <vector>

#include <search.h>
#include <sys/stat.h>
#include <fcntl.h>

#define MAX_CRAWL_DEPTH 1024
#define MAX_NUMBER_OF_FILENAME_FILTERS 1024

struct dev_and_inode
{
    dev_t dev;
    ino_t ino;

    // required for std::Set
    bool operator<(const dev_and_inode& other) const
    {
        return std::tie (dev, ino) < std::tie (other.dev, other.ino);        
    } 
};


/**
 * @brief The VMTouch class. This class implements the VM touch functionality into
 * bitcoind. It used to preload chainstate leveldb databse into memory.
 */
class VMTouch
{
public:
    VMTouch();
    virtual ~VMTouch();

    // Tries to bring given folder into disk cache, so that future access will be faster
    void  vmtouch_touch(const std::string& strPath);

    // TiresCheck how mouch of given folder content is present in disk cache (100 meand all of it)
    double vmtouch_check(const std::string& strPath);

    int64_t total_pages = 0;
    int64_t total_pages_in_core = 0;
    int64_t total_files=0;
    int64_t total_dirs=0;
    const long pagesize;

    const std::vector<std::string>& get_warnings() const
    {   return warnings; }

    unsigned int get_junk_counter() const
    {   return junk_counter; }
    
    // returns number between 0 and 100 that indicates how many pages 
    // were resident in memory before we tried to preload them
    // 100 means  that preload had no effect, since everything was
    // already in memory.
    double getPagesInCorePercent();

private:
    void vmtouch_crawl(std::string strPath);

    void vmtouch_file(const std::string& strPath);

    int aligned_p(void* p);

    int64_t bytes2pages(uint64_t bytes);

    int is_mincore_page_resident(char p);

    bool find_object(const struct stat& st);

    // add device and inode information to the tree of known inodes
    void add_object (const struct stat& st);

    void increment_nofile_rlimit();

    bool is_filename_filtered(const std::string& path);

    bool is_ignored(const std::string& path);

    
    std::set<dev_and_inode> seen_inodes; 
    std::vector<std::string> ignoreList;
    int curr_crawl_depth;
    ino_t crawl_inodes[MAX_CRAWL_DEPTH];
    std::vector<std::string> filenameFilterList;

    // eviction is asnyc and might not take effect immedialtey (or al all)
    // it's up to OS to decide when and if to remove pacges from cache
    bool o_evict = false; 
    bool o_touch = true;
    bool o_lock = false;
    int64_t o_max_file_size = LLONG_MAX;
    bool o_singlefilesystem = false;
    bool o_followsymlinks = true; 
    bool o_ignorehardlinkeduplictes = true;
    void warning(const char *fmt, ...);
    std::vector<std::string> warnings;

    dev_t orig_device;
    bool orig_device_inited = false;

    // just to prevent any compiler optimizations when touching memory
    unsigned int junk_counter = 0;
};

#endif

#endif // VMTOUCH_H
