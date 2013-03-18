/**
 * This file is part of the CernVM File System.
 */

#define __STDC_FORMAT_MACROS

#include "cvmfs_config.h"
#include "glue_buffer.h"

#include <inttypes.h>
#include <dirent.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>

#include <cstdlib>
#include <cstring>
#include <cassert>

#include "platform.h"
#include "smalloc.h"
#include "logging.h"

using namespace std;  // NOLINT


void GlueBuffer::InitLock() {
  rwlock_ =
    reinterpret_cast<pthread_rwlock_t *>(smalloc(sizeof(pthread_rwlock_t)));
  int retval = pthread_rwlock_init(rwlock_, NULL);
  assert(retval == 0);
}


GlueBuffer::GlueBuffer(const unsigned size) {
  assert(size >= 2);
  version_ = kVersion;
  size_ = size;
  buffer_ = new BufferEntry[size_];
  atomic_init64(&buffer_pos_);
  
  InitLock();
}


void GlueBuffer::CopyFrom(const GlueBuffer &other) {
  if (other.version_ > kVersion)
    abort();
  
  version_ = kVersion;
  size_ = other.size_;
  buffer_ = new BufferEntry[size_];
  for (unsigned i = 0; i < size_; ++i)
    buffer_[i] = other.buffer_[i];
  buffer_pos_ = other.buffer_pos_;
  statistics_ = other.statistics_;
}


GlueBuffer::GlueBuffer(const GlueBuffer &other) {
  CopyFrom(other);
  InitLock();
}


GlueBuffer &GlueBuffer::operator= (const GlueBuffer &other) {
  if (&other == this)
    return *this;

  delete[] buffer_;
  CopyFrom(other);
  return *this;
}


GlueBuffer::~GlueBuffer() {
  delete[] buffer_;
  pthread_rwlock_destroy(rwlock_);
  free(rwlock_);
}


void GlueBuffer::Resize(const unsigned new_size) {
  if (size_ == new_size)
    return;
  
  assert(new_size >= 2);
  BufferEntry *new_buffer = new BufferEntry[new_size];

  WriteLock();
  unsigned num_entries = size_ > new_size ? new_size : size_;
  for (unsigned i = 0; i < num_entries; ++i) {
    int64_t from_pos = 
      ((int64_t)(buffer_pos_) - (int64_t)(num_entries-i)) % size_;
    if (from_pos < 0)
      from_pos = size_ - (-from_pos);
    new_buffer[i] = buffer_[from_pos];
  }
  delete[] buffer_;
  buffer_ = new_buffer;
  if (buffer_pos_ >= new_size)
    buffer_pos_ = num_entries;
  size_ = new_size;
  Unlock();
  
  // TODO
}


bool GlueBuffer::ConstructPath(const unsigned buffer_idx, PathString *path) {
  // Root inode found?
  if (buffer_[buffer_idx].name.IsEmpty())
    return true;
  
  // Construct path until buffer_idx
  LogCvmfs(kLogGlueBuffer, kLogDebug, "construct inode %u, parent %u, name %s", 
           buffer_[buffer_idx].inode, buffer_[buffer_idx].parent_inode, 
           buffer_[buffer_idx].name.c_str());
  uint32_t needle_generation = buffer_[buffer_idx].generation;
  uint64_t needle_inode = buffer_[buffer_idx].parent_inode;
  int parent_idx = -1;
  for (unsigned i = 0; i < size_; ++i) {
    if ((buffer_[i].inode == needle_inode) && 
        (buffer_[i].generation == needle_generation))
    {
      parent_idx = i;
      break;
    }
  }
  if (parent_idx < 0)
    return false;
  
  bool retval = ConstructPath(parent_idx, path);
  path->Append("/", 1);
  path->Append(buffer_[buffer_idx].name.GetChars(), 
               buffer_[buffer_idx].name.GetLength());  
  return retval;
}


bool GlueBuffer::AncientInode2Path(const uint64_t inode, 
                                   const uint32_t current_generation,
                                   PathString *path)
{
  assert(path->IsEmpty());
  WriteLock();
  
  // Find inode with highest revision < new_revision
  unsigned max_generation = 0;
  int index = -1;
  for (unsigned i = 0; i < size_; ++i) {
    //LogCvmfs(kLogGlueBuffer, kLogDebug, "GLUE: idx %d, inode %u, parent %u, "
    //         "revision %u, name %s",
    //         i, buffer_[i].inode, buffer_[i].parent_inode, buffer_[i].revision, 
    //         buffer_[i].name.c_str());
    if ((buffer_[i].inode == inode) && 
        ((buffer_[i].generation < current_generation)) &&
        (buffer_[i].generation >= max_generation))
    {
      max_generation = buffer_[i].generation;
      index = i;
    }
  }
  if (index < 0) {
    LogCvmfs(kLogGlueBuffer, kLogDebug, "failed to find initial needle for "
             "ancient inode %"PRIu64, inode);
    Unlock();
    atomic_inc64(&statistics_.num_ancient_misses);
    return false;
  }
  
  // Recursively build path
  bool retval = ConstructPath(index, path);
  Unlock();
  
  if (retval) {
    atomic_inc64(&statistics_.num_ancient_hits);
    return true;
  }
  atomic_inc64(&statistics_.num_ancient_misses);
  return false;
}


//------------------------------------------------------------------------------


void CwdBuffer::InitLock() {
  lock_ =
    reinterpret_cast<pthread_mutex_t *>(smalloc(sizeof(pthread_mutex_t)));
  int retval = pthread_mutex_init(lock_, NULL);
  assert(retval == 0);
}


void CwdBuffer::CopyFrom(const CwdBuffer &other) {
  inode2cwd_ = other.inode2cwd_;
  mountpoint_ = other.mountpoint_;
  statistics_ = other.statistics_;
}


CwdBuffer::CwdBuffer(const std::string mountpoint) { 
  version_ = kVersion;
  mountpoint_ = mountpoint;
  InitLock();
}


CwdBuffer::CwdBuffer(const CwdBuffer &other) {
  assert(other.version_ == kVersion);
  version_ = kVersion;
  CopyFrom(other);
  InitLock();
}


CwdBuffer &CwdBuffer::operator= (const CwdBuffer &other) {
  assert(other.version_ == kVersion);
  CopyFrom(other);
  return *this;
}


CwdBuffer::~CwdBuffer() {
  pthread_mutex_destroy(lock_);
  free(lock_);
}


vector<PathString> CwdBuffer::GatherCwds() {
  pid_t save_uid = geteuid();
  gid_t save_gid = getegid();
  
  Lock();
  bool retval = SwitchCredentials(0, 0, true);
  if (!retval) {
    LogCvmfs(kLogGlueBuffer, kLogDebug, 
             "failed to switch to root for gathering cwds");
  }
  
  vector<PathString> result;
  
  DIR *dirp = opendir("/proc");
  if (!dirp) {
    LogCvmfs(kLogGlueBuffer, kLogDebug | kLogSyslog, "failed to open /proc");
    Unlock();
    retval = SwitchCredentials(save_uid, save_gid, true);
    assert(retval);
    return result;
  }
  platform_dirent64 *d;
  while ((d = platform_readdir(dirp)) != NULL) {
    const string pid = d->d_name;
    if (!IsNumeric(pid))
      continue;
    
    const string path = string("/proc/") + pid + string("/cwd");
    char symlink_buf[PATH_MAX];
    ssize_t retval = readlink(path.c_str(), symlink_buf, PATH_MAX);
    if (retval >= 0) {
      const string cwd(symlink_buf, retval);
      LogCvmfs(kLogGlueBuffer, kLogDebug, "cwd of pid %s is %s",
               pid.c_str(), cwd.c_str());
      if (HasPrefix(cwd, mountpoint_ + "/", false)) {
        string relative_cwd = cwd.substr(mountpoint_.length());
        result.push_back(PathString(relative_cwd));
        while ((relative_cwd = GetParentPath(relative_cwd)) != "") {
          result.push_back(PathString(relative_cwd));
        }
      }
    } else {
      LogCvmfs(kLogGlueBuffer, kLogDebug, "failed to read cwd of pid %s", 
               pid.c_str());
    }
  }
  closedir(dirp);
  if (!result.empty())
    result.push_back(PathString());
  
  retval = SwitchCredentials(save_uid, save_gid, true);
  assert(retval);
  
  // Clean garbage
  for (std::map<uint64_t, PathString>::iterator i = inode2cwd_.begin(),
       iEnd = inode2cwd_.end(); i != iEnd; )
  {
    bool found = false;
    for (unsigned j = 0; j < result.size(); ++j) {
      if (i->second == result[j]) {
        found = true;
        break;
      }
    }
    if (!found) {
      std::map<uint64_t, PathString>::iterator delete_me = i;
      ++i;
      inode2cwd_.erase(delete_me);
      atomic_inc64(&statistics_.num_removes);
    } else {
      ++i;
    }
  }
  Unlock();
  
  return result;
}


void CwdBuffer::Add(const uint64_t inode, const PathString &path) {
  Lock();
  inode2cwd_[inode] = path;
  Unlock();
  atomic_inc64(&statistics_.num_inserts);
}


void CwdBuffer::Remove(const uint64_t inode) {
  Lock();
  unsigned num_erased = inode2cwd_.erase(inode);
  Unlock();
  if (num_erased)
    atomic_inc64(&statistics_.num_removes);
}


bool CwdBuffer::Find(const uint64_t inode, PathString *path) {
  bool result = false;
  Lock();
  map<uint64_t, PathString>::const_iterator needle = inode2cwd_.find(inode);
  if (needle != inode2cwd_.end()) {
    result = true;
    path->Assign(needle->second);
  }
  Unlock();
  
  if (result) {
    atomic_inc64(&statistics_.num_ancient_hits);
    return true;
  }
  atomic_inc64(&statistics_.num_ancient_misses);
  return false;
}

void CwdBuffer::BeforeRemount(catalog::AbstractCatalogManager *source) {
  vector<PathString> open_cwds = GatherCwds();
  for (unsigned i = 0; i < open_cwds.size(); ++i) {
    catalog::DirectoryEntry dirent;
    bool retval = source->LookupPath(open_cwds[i], catalog::kLookupSole,
                                     &dirent);
    if (retval)
      Add(dirent.inode(), open_cwds[i]);
  }
}
