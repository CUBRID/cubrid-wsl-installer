# 네트워크 설정 (Network Setup)

## [Korean]

본 문서는 WSL2의 네트워크 작동 모드 세 가지(NAT, Mirror, Bridge)의 설정 방법을 설명합니다.

### **주의사항**
* 설정을 변경하기 전, 관리자 권한 PowerShell에서 `wsl --shutdown`을 실행하여 WSL을 모두 종료해야 합니다.
* 주요 설정 파일은 사용자 홈 디렉터리(예: `C:\Users\사용자이름\`)의 `.wslconfig` 파일입니다.
* `.wslconfig`에서 변경된 네트워크 모드는 설치된 모든 배포판에 적용됩니다.

---

### 요약 및 권장사항

 모드 | 지원 OS|
 :--- | :--- |
 **Mirrored** | Win 11만 |
 **Bridge** | Win 10/11 |
 **NAT 포트 포워딩** | Win 10/11 |

**Windows 11 사용자는 `미러 모드`를 권장합니다.**

### 1. NAT 모드에서 포트 포워딩 (NAT Mode)

WSL2의 기본 네트워크 모드는 NAT입니다.
내부 포트를 사용하기 위해 아래와 같은 과정이 필요합니다. 관리자 권한으로 PowerShell을 열고 순서대로 진행하세요.
(주의: WSL2 인스턴스의 IP 주소는 재부팅 시마다 변경될 수 있습니다. 고정적인 접근이 필요하다면 스크립트를 통한 자동화나 미러/브리지 모드를 권장합니다.)

1.  WSL의 현재 내부 IP 확인 (결과 : xxx.xxx.xxx.xxx)
```command
wsl -e hostname -I
```

2.  포트 연결 (예: broker 30000번 포트)
```command
netsh interface portproxy add v4tov4 listenport=30000 listenaddress=0.0.0.0 connectport=30000 connectaddress=xxx.xxx.xxx.xxx
```
3.  방화벽 허용
```command
New-NetFirewallRule -DisplayName "WSL Broker Port 30000" -Direction Inbound -Action Allow -Protocol TCP -LocalPort 30000
```

4.  현재 설정된 목록 보기
```command
netsh interface portproxy show v4tov4
```

**참고**
- 포트 연결 설정 삭제
```command
netsh interface portproxy delete v4tov4 listenport=30000 listenaddress=0.0.0.0
```

---

### 2. 미러 모드 네트워킹 (Mirrored Networking Mode)

Windows 호스트와 동일한 네트워크 인터페이스를 공유하며 동일한 IP 주소 대역을 사용합니다. 가장 간편하며 권장되는 방식입니다.

**호환성:** Windows 11 22H2 버전 이상에서만 지원됩니다. Windows 10에서는 사용할 수 없습니다.

**설정 방법:**

1.  WSL 종료:
    ```command
    wsl --shutdown
    ```
2.  사용자 홈 디렉터리에서 `.wslconfig` 파일을 열거나 새로 생성합니다.
3.  파일에 다음 내용을 추가하고 저장합니다:
    ```command
    [wsl2]
    networkingMode = mirrored
    ```
4.  WSL 재시작 후 IP 주소를 확인합니다 (Windows와 동일).
    ```command
    wsl --shutdown
    wsl -u cubrid
    ip addr show
    ```

---

### 3. 브리지 모드 네트워킹 (Bridged Networking Mode)

WSL2가 가상 스위치를 통해 물리 네트워크에 직접 연결되어 별도의 고정 IP 주소를 할당받습니다. NAT를 거치지 않아 외부 접근이 용이합니다.

**호환성: Windows 10/11 모두에서 사용 가능하나, HOME 버전의 경우 추가 설치가 필요하며 수동으로 활성화를 해야 합니다.**

* 활성화 방법 (관리자 권한으로 PowerShell을 열고 아래 명령어를 복사하여 실행, 설치 완료 후 시스템을 재부팅 하세요.)
  ```command
  # 수동 활성화
  Get-ChildItem -Path "$env:SystemRoot\servicing\Packages\*Hyper-V*.mum" | ForEach-Object { Dism /online /norestart /add-package:"$($_.FullName)" }
  # 재부팅
  Restart-Computer
  ```

**설정 방법:**

1.  **가상 스위치 생성**: Hyper-V 관리자(Windows 기능 활성화 필요)에서 `외부(External)` 가상 스위치를 생성하고 실제 네트워크 어댑터에 연결합니다.
    * 물리적 가상 스위치 확인
    ```command
    Get-VMSwitch | Where-Object { $_.SwitchType -eq "External" }
    ```
       * 생성된 가상 스위치가 없거나 새로 생성하여 사용할 경우
          1. 어댑터 조회
          ```command
          Get-NetAdapter | Where-Object { $_.Status -eq "Up" -and $_.HardwareInterface -eq $True }
          ```
          2. 스위치 생성 (`wsl_switch` - 생성할 스위치 이름, `이더넷 3` - 조회된 물리적 가상 스위치 Name)
          ```command
          New-VMSwitch -Name `wsl_switch` -NetAdapterName `이더넷 3` -AllowManagement $true -ErrorAction Stop
          ```

2.  **`.wslconfig` 설정**:
    ```command
    # 사용할 가상 스위치 이름으로 변경해야 합니다. (예: wsl_switch)
    [wsl2]
    vmSwitch = wsl_switch
    networkingMode = bridged
    ```
3.  WSL 재시작 후 IP 주소를 확인합니다 (Windows와 동일).
    ```command
    wsl --shutdown
    wsl -u cubrid
    ip addr
    ```
