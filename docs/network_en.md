## [English]

This document describes three (Mirror, Bridge, NAT with Fixed IP) network operating modes for WSL(Windows Subsystem for Linux) and their configuration methods.

### Important Notes
* Before changing settings, you must shut down all WSL instances by running `wsl --shutdown` in PowerShell with administrator privileges.
* The main configuration file is the `.wslconfig` file located in your user home directory (e.g., `C:\Users\username\`).
* Network mode changes in `.wslconfig` apply to all installed distributions.

---

### Summary and Recommendations

| Mode | Supported OS |
| :--- | :--- |
| **Mirrored** | Win 11 only |
| **Bridge** | Win 10/11 |
| **NAT Port Forwarding** | Win 10/11 |

**For Windows 11 users, `Mirrored Mode` is recommended.**

### 1. Port Forwarding in NAT Mode (NAT Mode)

The default network mode for WSL2 is NAT.
To use internal ports, follow the steps below. Open PowerShell with administrator privileges and proceed in order.
(Note: The IP address of a WSL2 instance may change with each reboot. If you need consistent access, automation through scripts or using mirrored/bridged mode is recommended.)

1.  Check the current internal IP of WSL (result: xxx.xxx.xxx.xxx)
```command
wsl -e hostname -I
```

2.  Port connection (e.g., broker port 30000)
```command
netsh interface portproxy add v4tov4 listenport=30000 listenaddress=0.0.0.0 connectport=30000 connectaddress=xxx.xxx.xxx.xxx
```
3.  Allow through firewall
```command
New-NetFirewallRule -DisplayName "WSL Broker Port 30000" -Direction Inbound -Action Allow -Protocol TCP -LocalPort 30000
```

4.  View currently configured list
```command
netsh interface portproxy show v4tov4
```

**Reference**
- Delete port connection configuration
```command
netsh interface portproxy delete v4tov4 listenport=30000 listenaddress=0.0.0.0
```

---

### 2. Mirrored Networking Mode

Shares the same network interface with the Windows host and uses the same IP address range. This is the simplest and most recommended approach.

**Compatibility:** Supported only on Windows 11 22H2 or later. Not available on Windows 10.

**Configuration Steps:**

1.  Shut down WSL:
    ```command
    wsl --shutdown
    ```
2.  Open or create the `.wslconfig` file in your user home directory.
3.  Add the following content to the file and save:
    ```ini
    [wsl2]
    networkingMode = mirrored
    ```
4.  Restart WSL and verify the IP address (should be the same as Windows).
    ```command
    wsl --shutdown
    wsl -u cubrid
    ip addr show
    ```

---

### 3. Bridged Networking Mode

WSL2 connects directly to the physical network through a virtual switch and receives a separate fixed IP address. External access is easier as it bypasses NAT.

**Compatibility: Available on both Windows 10/11, but HOME editions require additional installation and manual activation.**

* Activation method (Open PowerShell with administrator privileges, copy and execute the commands below, and reboot the system after installation completes.)
  ```command
  # Manual activation
  Get-ChildItem -Path "$env:SystemRoot\servicing\Packages\*Hyper-V*.mum" | ForEach-Object { Dism /online /norestart /add-package:"$($_.FullName)" }
  # Reboot
  Restart-Computer
  ```

**Configuration Steps:**

1.  **Create Virtual Switch**: In Hyper-V Manager (Windows feature activation required), create an `External` virtual switch and connect it to the physical network adapter.
    * Check for physical virtual switch
    ```command
    Get-VMSwitch | Where-Object { $_.SwitchType -eq "External" }
    ```
       * If no virtual switch exists or you need to create a new one
          1. Query network adapters
          ```command
          Get-NetAdapter | Where-Object { $_.Status -eq "Up" -and $_.HardwareInterface -eq $True }
          ```
          2. Create switch (`wsl_switch` - name for the switch to create, `Ethernet 3` - Name of the queried physical network adapter)
          ```command
          New-VMSwitch -Name `wsl_switch` -NetAdapterName `Ethernet 3` -AllowManagement $true -ErrorAction Stop
          ```

2.  **Configure `.wslconfig`**:
    ```ini
    # Change to the name of the virtual switch you want to use (e.g., wsl_switch)
    [wsl2]
    vmSwitch = wsl_switch
    networkingMode = bridged
    ```
3.  Restart WSL and verify the IP address (same as Windows).
    ```command
    wsl --shutdown
    wsl -u cubrid
    ip addr
    ```
