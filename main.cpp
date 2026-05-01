#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/inotify.h>
#include <iostream>
#include <vector>

using namespace std;

#define EVENT_SIZE  ( sizeof (struct inotify_event) )
// Each event can carry a filename up to 16 bytes; size the buffer for bulk reads
#define BUF_LEN     ( 1024 * ( EVENT_SIZE + 16 ) )

// Maps an inotify watch descriptor to the directory path it monitors
struct folderItem {
    int wd;
    string folder;
};


// Applies AD ACLs to the given path using setfacl.
// Uses fork/execlp instead of system() to avoid shell injection via path names.
void setFileACL(const string &path) {
  pid_t pid = fork();
  if (pid == 0) {
    execlp("setfacl", "setfacl", "-R", "-m",
      "u:AD\\administrator:rwx,g:AD\\domain users:rwx,g:AD\\domain admins:rwx",
      path.c_str(), (char *)NULL);
    perror("execlp setfacl");
    _exit(1);
  } else if (pid > 0) {
    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
      cerr << "setfacl failed for: " << path << endl;
    }
  } else {
    perror("fork");
  }
}

// Recursively enumerates all subdirectories under path.
// Returns a flat list of every nested directory for inotify registration.
vector<string> getDirList(const string &path) {
  DIR *dir;
  struct dirent *entry;
  vector<string> dirList;

  if ((dir = opendir(path.c_str())) != NULL) {

    while ((entry = readdir (dir)) != NULL) {
      string subDirName = entry->d_name;

      if (entry->d_type & DT_DIR) {
        if (subDirName != ".." && subDirName != ".") {
          string subPath = path + "/" + subDirName;
          dirList.push_back(subPath);

          vector<string> subDirs;
          subDirs = getDirList(subPath);
          dirList.insert(dirList.end(), subDirs.begin(), subDirs.end());
        }
      }
    }
  }
  closedir(dir);

  return dirList;
}

int main(int argc, char **argv)
{
  int fd, length, i = 0;
  string monitorPath = "";
  vector<string> folderList;
  // Initialized with size 2 so watch descriptor indices (1-based) align with vector positions.
  // inotify WDs start at 1, so folderTracker[wd+1] maps correctly after the initial padding.
  vector<folderItem> folderTracker(2);

  if (argc > 1) {
    monitorPath = argv[1];
  }
  else {
    cerr << "Specify a path." << endl;
    exit(1);
  }

  // Build initial directory tree and register watches for all existing subdirectories
  folderList = getDirList(monitorPath);
  folderList.push_back(monitorPath);

  fd = inotify_init();

  for(vector<string>::const_iterator ifl = folderList.begin(); ifl != folderList.end(); ++ifl) {
    string folder = *ifl;

    folderItem newFolder = {inotify_add_watch(fd, folder.c_str(), IN_CREATE), folder};
    folderTracker.push_back(newFolder);
  }

  // Main event loop — blocks on read() until inotify fires
  while(1) {
    struct inotify_event *event;
    char buffer[BUF_LEN];

    length = read(fd, buffer, BUF_LEN);

    if (length < 0) {
      cerr << "Error, read." << endl;
    }

    event = (struct inotify_event *) &buffer[i];

    if (event->len) {
      if (event->mask & IN_CREATE) {
        // Apply ACLs to the parent directory (recursive via setfacl -R)
        setFileACL(folderTracker[event->wd+1].folder);
        cout << folderTracker[event->wd+1].folder << "created." <<endl;
        string folder = folderTracker[event->wd+1].folder + event->name;
        // New subdirectories need their own watch to catch future creates
        if (event->mask & IN_ISDIR) {
          folderItem newFolder = {inotify_add_watch(fd, folder.c_str(), IN_CREATE), folder};
          folderTracker.push_back(newFolder);
        }
      }
    }
  }

  // Cleanup — unreachable in current loop but correct for future signal handling
  for(vector<folderItem>::const_iterator ifl = folderTracker.begin(); ifl != folderTracker.end(); ++ifl) {
    inotify_rm_watch(fd, (*ifl).wd);
  }

  close(fd);
}
