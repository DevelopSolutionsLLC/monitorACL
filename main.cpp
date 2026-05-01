#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/inotify.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string.h>

using namespace std;

const char *PID_FILE = "/var/run/monitorACL.pid";

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

// Writes the current PID to the pidfile so the daemon can be stopped later.
void writePidFile() {
  ofstream pidFile(PID_FILE);
  if (pidFile.is_open()) {
    pidFile << getpid() << endl;
    pidFile.close();
  } else {
    cerr << "WARNING: Could not write PID file: " << PID_FILE << endl;
  }
}

void removePidFile() {
  unlink(PID_FILE);
}

// Fork to background, detach from terminal, write PID file.
void daemonize() {
  pid_t pid = fork();
  if (pid < 0) {
    perror("fork");
    exit(1);
  }
  // Parent exits, child continues as daemon
  if (pid > 0) {
    exit(0);
  }

  // New session so the daemon isn't tied to the calling terminal
  if (setsid() < 0) {
    perror("setsid");
    exit(1);
  }

  // Redirect stdio to /dev/null — daemon has no terminal
  freopen("/dev/null", "r", stdin);
  freopen("/dev/null", "w", stdout);
  freopen("/dev/null", "w", stderr);

  writePidFile();
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

// Global so signal handler can clean up
volatile sig_atomic_t running = 1;
int global_fd = -1;

void signalHandler(int sig) {
  running = 0;
}

void usage(const char *prog) {
  cerr << "Usage: " << prog << " [-d] <path>" << endl;
  cerr << "  -d    Daemonize (fork to background, write PID to " << PID_FILE << ")" << endl;
  exit(1);
}

int main(int argc, char **argv)
{
  int fd, length, i = 0;
  string monitorPath = "";
  bool daemon_mode = false;
  vector<string> folderList;
  // Initialized with size 2 so watch descriptor indices (1-based) align with vector positions.
  // inotify WDs start at 1, so folderTracker[wd+1] maps correctly after the initial padding.
  vector<folderItem> folderTracker(2);

  int opt;
  while ((opt = getopt(argc, argv, "dh")) != -1) {
    switch (opt) {
      case 'd': daemon_mode = true; break;
      case 'h': default: usage(argv[0]);
    }
  }

  if (optind < argc) {
    monitorPath = argv[optind];
  } else {
    usage(argv[0]);
  }

  if (daemon_mode) {
    daemonize();
  }

  // Clean shutdown on SIGTERM/SIGINT — removes PID file and inotify watches
  signal(SIGTERM, signalHandler);
  signal(SIGINT, signalHandler);

  // Build initial directory tree and register watches for all existing subdirectories
  folderList = getDirList(monitorPath);
  folderList.push_back(monitorPath);

  fd = inotify_init();

  for(vector<string>::const_iterator ifl = folderList.begin(); ifl != folderList.end(); ++ifl) {
    string folder = *ifl;

    folderItem newFolder = {inotify_add_watch(fd, folder.c_str(), IN_CREATE), folder};
    folderTracker.push_back(newFolder);
  }

  global_fd = fd;

  // Main event loop — blocks on read() until inotify fires or signal interrupts
  while(running) {
    struct inotify_event *event;
    char buffer[BUF_LEN];

    length = read(fd, buffer, BUF_LEN);

    if (length < 0) {
      if (errno == EINTR) break; // interrupted by signal, exit cleanly
      cerr << "Error, read." << endl;
      break;
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

  // Cleanup watches and PID file on shutdown
  for(vector<folderItem>::const_iterator ifl = folderTracker.begin(); ifl != folderTracker.end(); ++ifl) {
    inotify_rm_watch(fd, (*ifl).wd);
  }

  close(fd);
  removePidFile();
  return 0;
}
