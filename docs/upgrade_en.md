## [English]

This document describes **how to upgrade a CUBRID server installed in WSL2 to a newer version**. CUBRID running inside WSL is upgraded in exactly the same way as CUBRID on a regular Linux host.

### **Important Notes**

* Always stop the CUBRID service with `cubrid service stop` before upgrading.
* Always back up the configuration files (`cubrid.conf`, `cubrid_broker.conf`, `cm.conf`) and the database location file (`databases.txt`) before upgrading.
* Database volumes are not guaranteed to be compatible across different **major versions**, so a migration with `unloaddb` / `loaddb` may be required.
* All upgrade commands must be executed as the `cubrid` user inside WSL. From PowerShell, you can enter the WSL distro with:
```command
wsl -d CUBRID_For_WSL -u cubrid
```

---

### Upgrade Scenarios at a Glance

| Scenario | Example | Recommended approach |
| :--- | :--- | :--- |
| **Patch-level change only**  | x.x.0 → x.x.1   | Back up configuration files and overwrite with the new package |
| **major / Minor version change** | x.3 → x.4, x.0 → y.0 | Migrate data with `unloaddb` / `loaddb` |

> **TIP**: You can check the exact CUBRID version currently installed by running `cubrid_rel` inside WSL.

---

### 1. Patch Version Upgrade (Same Major Version)

Patch-level upgrades within the same major version (for example x.x.0 → x.x.1) keep the database volume format compatible, so you only need to **preserve the configuration files**.

#### 1-1. Stop the service and back up configuration files

Run the following commands in order, inside WSL.

```command
cubrid service stop

mkdir -p ~/cubrid_backup
rm -rf ~/cubrid_backup/conf
cp -a $CUBRID/conf                  ~/cubrid_backup/
cp    $CUBRID_DATABASES/databases.txt ~/cubrid_backup/
```

> **NOTE**: Always back up and restore the `conf` directory as a whole.
> This preserves auxiliary files (TDE key files, locale/timezone data, etc.) alongside `cubrid.conf`. `cp -a` keeps permissions, ownership, and timestamps intact.

#### 1-2. Download the new version

Download the new patch release from the official site.

* Download page: https://www.cubrid.org/downloads
* To download directly from inside WSL (example):
```command
wget https://ftp.cubrid.org/CUBRID_Engine/<version>/Linux/CUBRID-<version>-linux.x86_64.sh
chmod +x CUBRID-<version>-linux.x86_64.sh
```

#### 1-3. Install the patch

Run the installer to overwrite the existing installation at `$CUBRID`.

```command
./CUBRID-<version>-linux.x86_64.sh
```

When the installation finishes, verify that the new version has been applied with `cubrid_rel`.

```command
cubrid_rel
```

#### 1-4. Restore the backed-up configuration files

```command
cp -a ~/cubrid_backup/conf/.       $CUBRID/conf/
cp    ~/cubrid_backup/databases.txt $CUBRID_DATABASES/
```

> **NOTE**: Any default configuration files shipped with the new version will be overwritten by their counterparts in the backup. To check whether the new version introduced any new parameters, run `diff -r $CUBRID/conf ~/cubrid_backup/conf` before restoring.

#### 1-5. Start the service and verify

```command
cubrid service start
cubrid service status
```

If the service starts normally, the patch upgrade is complete.

---

### 2. Minor / Major Version Migration (Incompatible DB Volumes)

Across different major versions, the database volume format may not be compatible. In this case, you must export the data into text files with `cubrid unloaddb` and reload them into the new CUBRID with `cubrid loaddb`.

For full details, refer to the **Database Migration → Recommended Scenario and Procedure** section in the official CUBRID manual.
https://www.cubrid.org/manuals

> All commands must be run as the `cubrid` user inside WSL.
> ```command
> wsl -d CUBRID_For_WSL -u cubrid
> ```

### Recommended Scenario and Procedure

The following describes a migration scenario that can be applied while the existing CUBRID is still in service. The migration relies on the `cubrid unloaddb` and `cubrid loaddb` utilities; see the **unloaddb** and **loaddb** chapters of the manual for full option references.

#### 2-1 Stop the existing CUBRID service

Run `cubrid service stop` to terminate all service processes of the existing CUBRID, then verify that all CUBRID-related processes have ended properly.

To check on Linux, run `ps -ef | grep cub_`; if no process starting with `cub_` remains, the shutdown completed normally. On Windows, press <Ctrl + Alt + Delete>, select [Start Task Manager], and verify that no process starting with `cub_` is listed under the [Processes] tab. If any CUBRID-related process still remains after the service stop, force-kill it (`kill` on Linux, or right-click the image name in Task Manager`s [Processes] tab and choose [End Process] on Windows), then check and remove any shared memory the CUBRID broker was using with `ipcs -m` on Linux.

#### 2-2 Back up the existing database

Use the `cubrid backupdb` utility to back up the existing database. This step is to guard against any failure that may occur during the subsequent unload / load operations. For details on database backup, refer to the **backupdb** section of the manual.

#### 2-3 Unload the existing database

Use the `cubrid unloaddb` utility to unload the database created by the existing CUBRID version. For details on database unload, refer to the **unloaddb** section of the manual.

#### 2-4 Preserve the existing CUBRID configuration files

Save the configuration files such as `cubrid.conf`, `cubrid_broker.conf`, and `cm.conf` under the `CUBRID/conf` directory. This allows you to reapply the parameter settings used in the existing CUBRID environment to the new CUBRID environment with ease.

#### 2-5 Install the new CUBRID version

Now that the backup and unload of data from the existing CUBRID are complete, remove the existing CUBRID and its databases, and install the new CUBRID version. For details on installing CUBRID, refer to the **Getting Started** section of the manual.

#### 2-6 Configure the new CUBRID environment

Using the configuration files preserved in **2-4** as a reference, set up the environment for the new CUBRID version. For details on environment configuration, refer to **Installation and Execution** under "CUBRID Getting Started" in the manual.

#### 2-7 Load the new database

Use the `cubrid createdb` utility to create the database, and then use the `cubrid loaddb` utility to load the previously unloaded data into that database. For details on database creation, see **createdb** in the "Administrator`s Guide"; for details on database loading, see the **loaddb** section of the manual.

#### 2-8 Back up the new database

Once the data load into the new database is complete, use the `cubrid backupdb` utility to back up the database created in the new CUBRID environment. This is because a backup taken in the old CUBRID environment cannot be restored in the new CUBRID environment. For details on database backup, refer to the **backupdb** section of the manual.

### **Warnings**

* Even within the same version, database volume backups and restores are **not guaranteed** to be compatible between 32-bit and 64-bit. Therefore, restoring a 32-bit backup on 64-bit CUBRID (or vice versa) is not recommended.
* If you use the **TDE** feature in CUBRID 11, backward compatibility is **not** provided, so files unloaded from CUBRID 11 cannot be loaded into an older version.

#### Further Reading

* Official manual: https://www.cubrid.org/manuals
* Full option reference for `cubrid unloaddb`: see the **unloaddb** entry in the manual above
* Full option reference for `cubrid loaddb`: see the **loaddb** entry in the manual above
