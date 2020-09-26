#include <errno.h>                                        
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <unistd.h>                                                            
#include <sys/types.h>                                    
#include <sys/inotify.h>  
#include <iostream>
#include <vector>

#include <stdlib.h>                                       
using namespace std;

#define EVENT_SIZE  ( sizeof (struct inotify_event) )                                         
#define BUF_LEN     ( 1024 * ( EVENT_SIZE + 16 ) ) 

struct folderItem {
    int wd;
    string folder;
};


void setFileACL(const string &path) {
  string command = "setfacl -R -m u:AD\\\\administrator:rwx,g:AD\\\\domain\\ users:rwx,g:AD\\\\domain\\ admins:rwx \"" + path + "\"";

cout << command << endl;
  system(command.c_str());
}

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
          subDirs = getDirList(subPath); // recursively call "listDir" with the new path
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
  vector<folderItem> folderTracker(2);

  if (argc > 1) {                                    
    monitorPath = argv[1];                        
  }                        
  else {                                   
    cerr << "Specify a path." << endl;                               
    exit(1);                  
  }                                               

 // cout << "You have entered " << argc  << " arguments:" << "\n";        
 // for (int i = 0; i < argc; ++i) {                       
 //   cout << argv[i] << "\n";   
 // }                             

  folderList = getDirList(monitorPath);
  folderList.push_back(monitorPath);

  fd = inotify_init();                                                         

  for(vector<string>::const_iterator ifl = folderList.begin(); ifl != folderList.end(); ++ifl) {
    string folder = *ifl;
    
    folderItem newFolder = {inotify_add_watch(fd, folder.c_str(), IN_CREATE), folder};
    folderTracker.push_back(newFolder);
  }

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
        setFileACL(folderTracker[event->wd+1].folder);
        cout << folderTracker[event->wd+1].folder << "created." <<endl;
        string folder = folderTracker[event->wd+1].folder + event->name;
        if (event->mask & IN_ISDIR) { // add to folder watch list
          folderItem newFolder = {inotify_add_watch(fd, folder.c_str(), IN_CREATE), folder};
          folderTracker.push_back(newFolder);
        }                                                                                     
      }                                                                                       
    }                                                                                         
  }                                                                                           

  for(vector<folderItem>::const_iterator ifl = folderTracker.begin(); ifl != folderTracker.end(); ++ifl) {
    inotify_rm_watch(fd, (*ifl).wd);
  }

  close(fd);
}
