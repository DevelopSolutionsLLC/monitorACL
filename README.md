# monitorACL
QNAP fix for Active Directory permission issue between shared group domain users.  Uses inotify to create recurisve file monitor then sets specified acl with setfacl.  Run in autostart.sh.  Entropy for compile required.
